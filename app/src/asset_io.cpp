#include "asset_io.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

// The single-header implementations live here, exactly once per binary.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

// The vendored cgltf has no message helper: name the result codes so a load
// failure reports something readable.
const char* cgltf_result_name(cgltf_result result) {
    switch (result) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy glTF 1.0";
        case cgltf_result_max_enum: break;
    }
    return "unknown error";
}

// cgltf owns the parsed document and its buffers; free it on every exit path.
struct CgltfDocument {
    cgltf_data* data = nullptr;

    CgltfDocument() = default;
    CgltfDocument(const CgltfDocument&) = delete;
    CgltfDocument& operator=(const CgltfDocument&) = delete;
    ~CgltfDocument() { cgltf_free(data); }
};

// stb_image owns the decoded pixels.
struct StbPixels {
    stbi_uc* data = nullptr;

    StbPixels() = default;
    StbPixels(const StbPixels&) = delete;
    StbPixels& operator=(const StbPixels&) = delete;
    ~StbPixels() { stbi_image_free(data); }
};

// A node's local transform, decomposed: the converter emits `matrix`, but TRS
// nodes are equally valid glTF.
struct LocalTrs {
    Vec3 t;
    Quat r;
    Vec3 s;
};

// Rotation (three column-major basis vectors) -> quaternion. The branch on the
// largest diagonal term keeps the divisor away from zero for every rotation;
// normalizing the result keeps a sheared basis from scaling the rotation.
Quat quat_from_basis(const Vec3 basis[3]) {
    const float m00 = basis[0].x, m10 = basis[0].y, m20 = basis[0].z;
    const float m01 = basis[1].x, m11 = basis[1].y, m21 = basis[1].z;
    const float m02 = basis[2].x, m12 = basis[2].y, m22 = basis[2].z;

    const float trace = m00 + m11 + m22;
    Quat q{};
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = Quat{(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = Quat{0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = Quat{(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = Quat{(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return quat_normalize(q);
}

// Decomposes a column-major local matrix: T = column 3, S = the basis column
// lengths, R = the normalized basis. A zero-length column (degenerate scale)
// cannot be normalized, so it keeps S = 1 and contributes a zero basis vector:
// the rotation is then arbitrary, but finite, instead of dividing by zero.
LocalTrs decompose_matrix(const float* m) {
    LocalTrs out;
    out.t = Vec3{m[12], m[13], m[14]};

    Vec3 basis[3] = {Vec3{m[0], m[1], m[2]}, Vec3{m[4], m[5], m[6]}, Vec3{m[8], m[9], m[10]}};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        const float length = std::sqrt(vec3_dot(basis[i], basis[i]));
        if (length > 0.0f) {
            scale[i] = length;
            basis[i] = Vec3{basis[i].x / length, basis[i].y / length, basis[i].z / length};
        } else {
            basis[i] = Vec3{0.0f, 0.0f, 0.0f};
        }
    }
    out.s = Vec3{scale[0], scale[1], scale[2]};
    out.r = quat_from_basis(basis);
    return out;
}

// The node's local transform. cgltf leaves an absent rotation/scale zeroed, so
// the glTF defaults (identity rotation, unit scale) are applied here.
LocalTrs node_local_trs(const cgltf_node& node) {
    if (node.has_matrix) {
        return decompose_matrix(node.matrix);
    }
    LocalTrs out;
    out.t = Vec3{node.translation[0], node.translation[1], node.translation[2]};
    out.r = node.has_rotation
                ? Quat{node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]}
                : Quat{0.0f, 0.0f, 0.0f, 1.0f};
    out.s = node.has_scale ? Vec3{node.scale[0], node.scale[1], node.scale[2]}
                           : Vec3{1.0f, 1.0f, 1.0f};
    return out;
}

// The glb's skeleton — the converted models carry exactly one — stored on
// `rig`, returning the node -> joint-slot map its clips resolve channels
// against. `joint_of_node` maps a node index to its slot in the joint list, so
// both parent links and animation channels can be resolved against the skin's
// own order.
std::vector<int> load_skin_into(RigCpu& rig, const cgltf_data& data) {
    std::vector<int> joint_of_node(data.nodes_count, -1);
    if (data.skins_count == 0) {
        return joint_of_node;
    }

    const cgltf_skin& skin = data.skins[0];
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        if (skin.joints[i] != nullptr) {
            joint_of_node[cgltf_node_index(&data, skin.joints[i])] = static_cast<int>(i);
        }
    }

    rig.skin.joints.resize(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        JointCpu& joint = rig.skin.joints[i];
        joint.parent = -1;
        joint.inverseBind = mat4_identity();

        const cgltf_node* node = skin.joints[i];
        if (node == nullptr) {
            continue;  // malformed joint entry: keep the slot, not the data
        }
        joint.name = node->name != nullptr ? node->name : "";
        const LocalTrs rest = node_local_trs(*node);
        joint.restT = rest.t;
        joint.restR = rest.r;
        joint.restS = rest.s;
        // Only a joint can be a joint's parent: an enclosing armature node
        // (or the scene) leaves this joint a skeleton root.
        if (node->parent != nullptr) {
            joint.parent = joint_of_node[cgltf_node_index(&data, node->parent)];
        }
    }

    // Inverse bind matrices; a skin without them binds in rest pose.
    if (skin.inverse_bind_matrices != nullptr) {
        const cgltf_accessor* inverse_bind = skin.inverse_bind_matrices;
        for (cgltf_size i = 0; i < skin.joints_count && i < inverse_bind->count; ++i) {
            float matrix[16] = {};
            if (cgltf_accessor_read_float(inverse_bind, i, matrix, 16)) {
                for (int k = 0; k < 16; ++k) {
                    rig.skin.joints[i].inverseBind.m[k] = matrix[k];
                }
            }
        }
    }
    return joint_of_node;
}

// The glb's clips, appended to `rig`: only channels that drive a skin joint
// survive — a clip's helper-bone or camera channels have no joint to land on.
// What is dropped is counted once per clip, so the loader never discards data
// silently.
void load_animations_into(RigCpu& rig, const cgltf_data& data,
                          const std::vector<int>& joint_of_node,
                          const std::string& model_path_string) {
    for (cgltf_size a = 0; a < data.animations_count; ++a) {
        const cgltf_animation& animation = data.animations[a];

        AnimationCpu clip;
        clip.name = animation.name != nullptr ? animation.name : "";
        clip.duration = 0.0f;
        int skipped_non_joint = 0;
        int skipped_unsupported = 0;

        for (cgltf_size c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            const cgltf_animation_sampler* sampler = channel.sampler;

            if (sampler == nullptr || sampler->input == nullptr || sampler->output == nullptr) {
                ++skipped_unsupported;
                continue;
            }
            // A CUBICSPLINE key holds three values (in-tangent, value,
            // out-tangent): reading them as LINEAR would mis-animate silently,
            // so refuse the whole load instead.
            if (sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
                throw std::runtime_error("animation \"" + clip.name + "\" channel " +
                                         std::to_string(c) + " in " + model_path_string +
                                         " uses CUBICSPLINE interpolation, which is not supported");
            }
            if (channel.target_node == nullptr) {
                ++skipped_unsupported;
                continue;
            }
            const int joint = joint_of_node[cgltf_node_index(&data, channel.target_node)];
            if (joint < 0) {
                ++skipped_non_joint;
                continue;
            }

            int path = -1;
            switch (channel.target_path) {
                case cgltf_animation_path_type_translation: path = 0; break;
                case cgltf_animation_path_type_rotation: path = 1; break;
                case cgltf_animation_path_type_scale: path = 2; break;
                default: break;  // morph-target weights are not TRS
            }
            if (path < 0) {
                ++skipped_unsupported;
                continue;
            }

            AnimationChannelCpu out;
            out.joint = joint;
            out.path = path;
            out.interpolation =
                sampler->interpolation == cgltf_interpolation_type_step ? 1 : 0;

            const cgltf_accessor* input = sampler->input;
            out.times.resize(input->count);
            for (cgltf_size k = 0; k < input->count; ++k) {
                float time = 0.0f;
                cgltf_accessor_read_float(input, k, &time, 1);
                out.times[k] = time;
            }

            // Rotations carry 4 floats per key, translations and scales 3.
            const std::size_t components = path == 1 ? 4u : 3u;
            const cgltf_accessor* output = sampler->output;
            out.values.resize(output->count * components);
            for (cgltf_size k = 0; k < output->count; ++k) {
                float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(output, k, value, components);
                for (std::size_t i = 0; i < components; ++i) {
                    out.values[k * components + i] = value[i];
                }
            }

            if (!out.times.empty()) {
                clip.duration =
                    std::max(clip.duration, *std::max_element(out.times.begin(), out.times.end()));
            }
            clip.channels.push_back(std::move(out));
        }

        if (skipped_non_joint > 0 || skipped_unsupported > 0) {
            std::fprintf(stderr,
                         "sotc-viewer: animation \"%s\": skipped %d channel(s) "
                         "(non-joint targets: %d, unsupported: %d)\n",
                         clip.name.c_str(), skipped_non_joint + skipped_unsupported,
                         skipped_non_joint, skipped_unsupported);
        }
        rig.animations.push_back(std::move(clip));
    }
}

// A point through a column-major matrix (w = 1): the world position a composed
// instance places a vertex at.
Vec3 transform_point(const Mat4& matrix, Vec3 point) {
    return Vec3{
        matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
        matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
        matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]};
}

// How one glb lands in the target scene: the world matrix baked into every mesh
// it contributes, the index its materials start at, and whether it contributes
// a rig record (`includeRig`) — and, if so, whether that record is the player's
// (`dynamic`). A static instance contributes meshes with rig = -1; a rigged
// glb's meshes carry its new rig index and its skeleton/clips land on one
// RigCpu.
struct GlbPlacement {
    Mat4 model = mat4_identity();
    int materialOffset = 0;
    bool includeRig = false;
    bool dynamic = false;
};

// Loads one glb into `scene`, appending its meshes, materials and textures —
// and, when `placement.includeRig`, one rig record holding its skeleton and
// clips. Every contributed mesh carries `placement.model` and its rig index
// (`placement.includeRig` ? the new record : -1); material indices shift by
// `placement.materialOffset` (texture indices need no shift: images append to
// the scene's shared texture list, so their index already is global).
// `have_bounds` is the caller's running "some vertex was seen" flag: the
// scene's bounds are set from the first glb that has vertices and grown by each
// later one, always in world space.
void load_glb_into(Scene& scene, const std::filesystem::path& libraryDir,
                   const std::string& glbRelPath, const GlbPlacement& placement,
                   bool& have_bounds) {
    const std::filesystem::path library = std::filesystem::weakly_canonical(libraryDir);
    const std::filesystem::path model_path = library / glbRelPath;
    const std::filesystem::path model_dir = model_path.parent_path();
    const std::string model_path_string = model_path.string();

    cgltf_options options{};
    CgltfDocument document;

    cgltf_result result = cgltf_parse_file(&options, model_path_string.c_str(), &document.data);
    if (result != cgltf_result_success) {
        throw std::runtime_error("cannot parse " + model_path_string + ": " +
                                 cgltf_result_name(result));
    }
    // GLB buffers are embedded; .gltf fixtures carry data URIs. Both are handled
    // here, so no external .bin is required.
    result = cgltf_load_buffers(&options, document.data, model_path_string.c_str());
    if (result != cgltf_result_success) {
        throw std::runtime_error("cannot load buffers of " + model_path_string + ": " +
                                 cgltf_result_name(result));
    }

    // This glb's own vertex extent; merged into the scene's bounds at the end.
    Vec3 bounds_min{};
    Vec3 bounds_max{};
    bool have_vertex = false;

    // Image index -> Scene::textures index (-1 = failed). Shared images (and the
    // two texture slots of a material) decode only once.
    std::unordered_map<cgltf_size, int> loaded_images;

    // Decodes one glTF image into RGBA8 and appends it to the scene. Returns -1,
    // without throwing, when the PNG is absent or undecodable: the converter
    // drops textures it cannot convert (e.g. BC6H/HDR), and such a material
    // degrades to factor-only.
    auto load_image = [&](const cgltf_image* image) -> int {
        if (image == nullptr || image->uri == nullptr) {
            return -1;
        }
        // Image URIs are relative to the model's directory; sourceRelPath is the
        // resolved library-relative path with '/' separators. A URI that escapes
        // the library (relative `..` or absolute) is skipped: lexically_relative
        // reports escapes as `..`-prefixed paths (or empty when the roots differ),
        // never as an in-library path.
        const std::filesystem::path resolved =
            (model_dir / std::filesystem::path(image->uri)).lexically_normal();
        const std::filesystem::path rel = resolved.lexically_relative(library);
        if (rel.empty() || *rel.begin() == "..") {
            std::fprintf(stderr, "sotc-viewer: skipping texture %s: outside the library\n",
                         resolved.string().c_str());
            return -1;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        StbPixels pixels;
        pixels.data = stbi_load(resolved.string().c_str(), &width, &height, &channels, 4);
        if (pixels.data == nullptr) {
            std::fprintf(stderr, "sotc-viewer: skipping texture %s: %s\n",
                         rel.generic_string().c_str(), stbi_failure_reason());
            return -1;
        }

        TextureCpu texture;
        texture.width = width;
        texture.height = height;
        const std::size_t pixel_bytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        texture.rgba8.assign(pixels.data, pixels.data + pixel_bytes);
        texture.sourceRelPath = rel.generic_string();
        scene.textures.push_back(std::move(texture));
        return static_cast<int>(scene.textures.size()) - 1;
    };

    auto resolve_texture = [&](const cgltf_texture* texture) -> int {
        if (texture == nullptr || texture->image == nullptr) {
            return -1;
        }
        const cgltf_size image_index =
            static_cast<cgltf_size>(texture->image - document.data->images);
        const auto found = loaded_images.find(image_index);
        if (found != loaded_images.end()) {
            return found->second;
        }
        const int loaded = load_image(texture->image);
        loaded_images.emplace(image_index, loaded);
        return loaded;
    };

    // The rig record this glb will land on (its meshes carry the index even
    // though the record is pushed after the geometry). An instance glb stays
    // static: its meshes keep rig = -1.
    const int rig_index = placement.includeRig ? static_cast<int>(scene.rigs.size()) : -1;
    const std::size_t mesh_begin = scene.meshes.size();

    for (cgltf_size m = 0; m < document.data->meshes_count; ++m) {
        const cgltf_mesh& mesh = document.data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];

            const cgltf_accessor* position =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            if (position == nullptr) {
                throw std::runtime_error("primitive without POSITION in " + model_path_string);
            }
            const cgltf_accessor* normal =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* texcoord =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* joints =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* weights =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);

            MeshCpu out;
            out.model = placement.model;
            out.rig = rig_index;
            out.materialIndex =
                primitive.material != nullptr
                    ? static_cast<int>(cgltf_material_index(document.data, primitive.material)) +
                          placement.materialOffset
                    : -1;

            out.vertices.resize(position->count);
            for (cgltf_size v = 0; v < position->count; ++v) {
                Vertex& vertex = out.vertices[v];

                float xyz[3] = {0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(position, v, xyz, 3);
                vertex.px = xyz[0];
                vertex.py = xyz[1];
                vertex.pz = xyz[2];

                // Absent NORMAL: Y-up, like the adapter's fallback.
                vertex.nx = 0.0f;
                vertex.ny = 1.0f;
                vertex.nz = 0.0f;
                if (normal != nullptr) {
                    float n[3] = {0.0f, 0.0f, 0.0f};
                    cgltf_accessor_read_float(normal, v, n, 3);
                    vertex.nx = n[0];
                    vertex.ny = n[1];
                    vertex.nz = n[2];
                }

                // Absent TEXCOORD_0: zero UV.
                vertex.u = 0.0f;
                vertex.v = 0.0f;
                if (texcoord != nullptr) {
                    float uv[2] = {0.0f, 0.0f};
                    cgltf_accessor_read_float(texcoord, v, uv, 2);
                    vertex.u = uv[0];
                    vertex.v = uv[1];
                }

                // Absent JOINTS_0/WEIGHTS_0: zero weights, which the shader's
                // skinning path reads as "unskinned" and passes through
                // untransformed. (Full weight on joint 0 would instead select
                // the palette's first joint — in a composed scene that is the
                // player's animated root bone, which would warp every unskinned
                // prop with the player's animation.) JOINTS_0 arrives as
                // unsigned byte or short; both become the u16 the pipeline uses.
                for (int i = 0; i < 4; ++i) {
                    vertex.j[i] = 0;
                    vertex.w[i] = 0.0f;
                }
                if (joints != nullptr && v < joints->count) {
                    cgltf_uint indices[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(joints, v, indices, 4);
                    for (int i = 0; i < 4; ++i) {
                        vertex.j[i] = static_cast<uint16_t>(indices[i]);
                    }
                }
                if (weights != nullptr && v < weights->count) {
                    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    cgltf_accessor_read_float(weights, v, values, 4);
                    for (int i = 0; i < 4; ++i) {
                        vertex.w[i] = values[i];
                    }
                }

                // Bounds are world-space: the instance's placement, not the raw
                // vertex (the identity model leaves a single-glb load unchanged).
                const Vec3 world =
                    transform_point(placement.model, Vec3{vertex.px, vertex.py, vertex.pz});
                if (!have_vertex) {
                    bounds_min = world;
                    bounds_max = world;
                    have_vertex = true;
                } else {
                    bounds_min.x = std::min(bounds_min.x, world.x);
                    bounds_min.y = std::min(bounds_min.y, world.y);
                    bounds_min.z = std::min(bounds_min.z, world.z);
                    bounds_max.x = std::max(bounds_max.x, world.x);
                    bounds_max.y = std::max(bounds_max.y, world.y);
                    bounds_max.z = std::max(bounds_max.z, world.z);
                }
            }

            if (primitive.indices != nullptr) {
                out.indices.resize(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                    out.indices[i] = static_cast<uint32_t>(
                        cgltf_accessor_read_index(primitive.indices, i));
                }
            } else {
                // Non-indexed primitive: 0..n-1.
                out.indices.resize(out.vertices.size());
                for (std::size_t i = 0; i < out.indices.size(); ++i) {
                    out.indices[i] = static_cast<uint32_t>(i);
                }
            }

            scene.meshes.push_back(std::move(out));
        }
    }

    for (cgltf_size m = 0; m < document.data->materials_count; ++m) {
        const cgltf_material& material = document.data->materials[m];

        MaterialCpu out;
        out.name = material.name != nullptr ? material.name : "";
        // cgltf applies the glTF defaults on top of zeroed storage, so absent
        // factors already read (1,1,1,1) and (0,0,0).
        const float* base_color = material.pbr_metallic_roughness.base_color_factor;
        for (int i = 0; i < 4; ++i) {
            out.baseColorFactor[i] = base_color[i];
        }
        for (int i = 0; i < 3; ++i) {
            out.emissiveFactor[i] = material.emissive_factor[i];
        }
        out.baseColorTexture =
            resolve_texture(material.pbr_metallic_roughness.base_color_texture.texture);
        out.normalTexture = resolve_texture(material.normal_texture.texture);

        scene.materials.push_back(std::move(out));
    }

    // The rig: each rigged glb contributes exactly one record, and its meshes
    // carry that record's index. An instance glb is static — its skeleton and
    // clips are skipped, loudly, rather than folded in with joint indices that
    // mean something else (its JOINTS_0/WEIGHTS_0 stay on the vertices but are
    // drawn as authored).
    if (placement.includeRig) {
        RigCpu rig;
        rig.meshBegin = mesh_begin;
        rig.meshEnd = scene.meshes.size();
        rig.dynamic = placement.dynamic;
        const std::vector<int> joint_of_node = load_skin_into(rig, *document.data);
        load_animations_into(rig, *document.data, joint_of_node, model_path_string);
        scene.rigs.push_back(std::move(rig));
    } else if (document.data->skins_count > 0 || document.data->animations_count > 0) {
        std::fprintf(stderr,
                     "sotc-viewer: %s: skeleton/clips skipped (a static instance has no rig)\n",
                     glbRelPath.c_str());
    }

    // Merge this glb's world-space extent into the scene's; a caller whose
    // scene saw no vertices at all zeroes the bounds itself.
    if (have_vertex) {
        if (!have_bounds) {
            scene.boundsMin = bounds_min;
            scene.boundsMax = bounds_max;
            have_bounds = true;
        } else {
            scene.boundsMin.x = std::min(scene.boundsMin.x, bounds_min.x);
            scene.boundsMin.y = std::min(scene.boundsMin.y, bounds_min.y);
            scene.boundsMin.z = std::min(scene.boundsMin.z, bounds_min.z);
            scene.boundsMax.x = std::max(scene.boundsMax.x, bounds_max.x);
            scene.boundsMax.y = std::max(scene.boundsMax.y, bounds_max.y);
            scene.boundsMax.z = std::max(scene.boundsMax.z, bounds_max.z);
        }
    }
}

}  // namespace

Scene load_scene(const std::filesystem::path& libraryDir, const std::string& glbRelPath) {
    Scene scene;
    bool have_bounds = false;
    // A single glb is the scene's player: one rig record (possibly empty when
    // the model has no skeleton), dynamic.
    load_glb_into(scene, libraryDir, glbRelPath,
                  GlbPlacement{.includeRig = true, .dynamic = true}, have_bounds);
    if (!have_bounds) {
        scene.boundsMin = Vec3{0.0f, 0.0f, 0.0f};
        scene.boundsMax = Vec3{0.0f, 0.0f, 0.0f};
    }
    return scene;
}

Scene compose_scene(const std::filesystem::path& libraryDir, const SceneManifest& manifest,
                    const std::string& playerGlbRelPath, std::size_t* playerMeshBegin) {
    Scene scene;
    bool have_bounds = false;

    // Static instances: no rig record, meshes keep rig = -1.
    for (const ManifestInstance& instance : manifest.instances) {
        const GlbPlacement placement{
            .model = mat4_from_trs(instance.position, instance.rotation, instance.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
        };
        try {
            load_glb_into(scene, libraryDir, instance.glbRelPath, placement, have_bounds);
        } catch (const std::runtime_error& error) {
            // Name the manifest's own glb token: the resolved path alone is
            // harder to match back to the instance that placed it.
            throw std::runtime_error("compose_scene: instance \"" + instance.glbRelPath +
                                     "\": " + error.what());
        }
    }

    // The player: identity model and rig 0, the dynamic rig. Its materials
    // follow every instance's, so they carry the same offset.
    const GlbPlacement player_placement{
        .materialOffset = static_cast<int>(scene.materials.size()),
        .includeRig = true,
        .dynamic = true,
    };
    load_glb_into(scene, libraryDir, playerGlbRelPath, player_placement, have_bounds);

    // The player's meshes start here (rig 0's meshBegin): everything before is
    // the manifest's instances, everything after is an entity's.
    if (playerMeshBegin != nullptr) {
        *playerMeshBegin = scene.rigs[scene.playerRig].meshBegin;
    }

    // Entities: each one keeps its skeleton and clips as its own rig, placed by
    // the manifest TRS, and must carry the clip it names — a manifest that
    // asks for a clip the model does not have is a load error, not a silent
    // rest pose.
    for (const ManifestEntity& entity : manifest.entities) {
        const GlbPlacement placement{
            .model = mat4_from_trs(entity.placement.position, entity.placement.rotation,
                                   entity.placement.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
            .includeRig = true,
        };
        try {
            load_glb_into(scene, libraryDir, entity.placement.glbRelPath, placement, have_bounds);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("compose_scene: entity \"" + entity.placement.glbRelPath +
                                     "\": " + error.what());
        }

        const RigCpu& rig = scene.rigs.back();
        const bool clip_found =
            std::any_of(rig.animations.begin(), rig.animations.end(),
                        [&entity](const AnimationCpu& clip) { return clip.name == entity.clip; });
        if (!clip_found) {
            std::string message = "compose_scene: entity \"" + entity.placement.glbRelPath +
                                  "\": clip \"" + entity.clip + "\" not found";
            if (rig.animations.empty()) {
                message += " (the model has no animation clips)";
            } else {
                message += "; available clips:";
                for (const AnimationCpu& clip : rig.animations) {
                    message += " " + clip.name;
                }
            }
            throw std::runtime_error(message);
        }
    }

    if (!have_bounds) {
        scene.boundsMin = Vec3{0.0f, 0.0f, 0.0f};
        scene.boundsMax = Vec3{0.0f, 0.0f, 0.0f};
    }
    return scene;
}
