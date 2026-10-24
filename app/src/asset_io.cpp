#include "asset_io.h"

#include "anim.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

// Composes every joint's bind-pose world transform from the rest chain: the
// rest pose is the bind pose, so no matrix inverse is needed. Parent-first with
// the same order-independent chain walk as animation_pose, so a skeleton whose
// joints are not listed parent-first still composes correctly; the chain-length
// guard breaks a malformed cycle instead of hanging.
void compose_bind_world(SkinCpu& skin) {
    const std::size_t joint_count = skin.joints.size();
    std::vector<bool> resolved(joint_count, false);
    std::vector<int> chain;
    for (std::size_t i = 0; i < joint_count; ++i) {
        if (resolved[i]) {
            continue;
        }
        chain.clear();
        int j = static_cast<int>(i);
        while (j >= 0 && !resolved[static_cast<std::size_t>(j)]) {
            chain.push_back(j);
            if (chain.size() > joint_count) {
                break;
            }
            j = skin.joints[static_cast<std::size_t>(j)].parent;
        }
        Mat4 parent_world = (j >= 0 && resolved[static_cast<std::size_t>(j)])
                                ? skin.joints[static_cast<std::size_t>(j)].bindWorld
                                : mat4_identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::size_t index = static_cast<std::size_t>(*it);
            const JointCpu& joint = skin.joints[index];
            skin.joints[index].bindWorld =
                mat4_mul(parent_world, mat4_from_trs(joint.restT, joint.restR, joint.restS));
            parent_world = skin.joints[index].bindWorld;
            resolved[index] = true;
        }
    }
}

// The glb's skeleton — the converted models carry exactly one — stored on
// `rig`, returning the node -> joint-slot map its clips resolve channels
// against. `joint_of_node` maps a node index to its slot in the joint list, so
// both parent links and animation channels can be resolved against the skin's
// own order.
//
// `bindWorld` is composed here from the rest chain, parent first: the rest pose
// IS the bind pose, so the chain of local TRS matrices is the inverse of the
// glTF inverse bind without ever inverting one. Joints whose parent is listed
// after them are resolved by walking up to the first resolved ancestor (the
// same order-independent pass animation_pose uses); a malformed cycle stops at
// the chain-length guard with identity parents, never a hang.
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
    compose_bind_world(rig.skin);
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

// Decodes one PNG into RGBA8 (`sourceRelPath` is left to the caller). Returns
// false when the file is absent or undecodable, with stbi_failure_reason() set
// — thread-locally, so workers may call this concurrently.
bool decode_png(const std::filesystem::path& file, TextureCpu& out) {
    int width = 0;
    int height = 0;
    int channels = 0;
    StbPixels pixels;
    pixels.data = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
    if (pixels.data == nullptr) {
        return false;
    }
    out.width = width;
    out.height = height;
    const std::size_t pixel_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    out.rgba8.assign(pixels.data, pixels.data + pixel_bytes);
    return true;
}

// Library-relative PNG path -> its decoded texture, ready for load_glb_into to
// move into the scene instead of decoding it there.
using DecodedTextures = std::unordered_map<std::string, TextureCpu>;

// Decodes every PNG the materials of `glbRelPaths` name, each once, on up to
// hardware_concurrency threads (S7 task 11: the shrine's ~150 unique PNGs).
// Only the pixels are made here; the serial loads still assign the texture
// indices, so the order is the serial one whoever finishes first. A glb that
// does not parse or a PNG that does not decode is simply left out: its serial
// load reports it exactly as before. The calling thread decodes too, and calls
// `onProgress` (when set) after each of its PNGs.
DecodedTextures decode_textures(const std::filesystem::path& libraryDir,
                                const std::vector<std::string>& glbRelPaths,
                                const std::function<void()>& onProgress) {
    const std::filesystem::path library = std::filesystem::weakly_canonical(libraryDir);
    std::vector<std::string> paths;
    std::unordered_set<std::string> seen_glbs;
    std::unordered_set<std::string> seen_paths;
    for (const std::string& glb : glbRelPaths) {
        if (!seen_glbs.insert(glb).second) {
            continue;
        }
        const std::filesystem::path model_path = library / glb;
        cgltf_options options{};
        CgltfDocument document;
        if (cgltf_parse_file(&options, model_path.string().c_str(), &document.data) !=
            cgltf_result_success) {
            continue;
        }
        for (cgltf_size m = 0; m < document.data->materials_count; ++m) {
            const cgltf_material& material = document.data->materials[m];
            // The three slots load_glb_into resolves.
            for (const cgltf_texture* texture :
                 {material.pbr_metallic_roughness.base_color_texture.texture,
                  material.normal_texture.texture, material.emissive_texture.texture}) {
                if (texture == nullptr || texture->image == nullptr ||
                    texture->image->uri == nullptr) {
                    continue;
                }
                // load_image's resolution; an escaping URI is its to report.
                const std::filesystem::path rel =
                    (model_path.parent_path() / std::filesystem::path(texture->image->uri))
                        .lexically_normal()
                        .lexically_relative(library);
                if (!rel.empty() && *rel.begin() != ".." &&
                    seen_paths.insert(rel.generic_string()).second) {
                    paths.push_back(rel.generic_string());
                }
            }
        }
    }

    std::vector<TextureCpu> textures(paths.size());
    std::vector<char> decoded_ok(paths.size(), 0);
    std::atomic<std::size_t> next{0};
    const auto worker = [&](const std::function<void()>* progress) {
        for (std::size_t i = next++; i < paths.size(); i = next++) {
            decoded_ok[i] = decode_png(library / paths[i], textures[i]) ? 1 : 0;
            if (progress != nullptr && *progress) {
                (*progress)();
            }
        }
    };
    const std::size_t thread_count = std::min<std::size_t>(
        std::max(1u, std::thread::hardware_concurrency()), paths.size());
    std::vector<std::thread> threads;
    for (std::size_t t = 1; t < thread_count; ++t) {
        threads.emplace_back(worker, nullptr);
    }
    worker(&onProgress);
    for (std::thread& thread : threads) {
        thread.join();
    }

    DecodedTextures decoded;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (decoded_ok[i] != 0) {
            textures[i].sourceRelPath = paths[i];
            decoded.emplace(paths[i], std::move(textures[i]));
        }
    }
    return decoded;
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
// the scene's shared texture list, or reuse the entry an earlier glb decoded
// from the same library path, so their index already is global).
// `have_bounds` is the caller's running "some vertex was seen" flag: the
// scene's bounds are set from the first glb that has vertices and grown by each
// later one, always in world space. `decoded` (optional) holds pixels decoded
// ahead of time; a PNG found there is moved into the scene, not decoded again.
void load_glb_into(Scene& scene, const std::filesystem::path& libraryDir,
                   const std::string& glbRelPath, const GlbPlacement& placement,
                   bool& have_bounds, DecodedTextures* decoded = nullptr) {
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

    // Library-relative path -> Scene::textures index, across every glb already in
    // the scene: a PNG another glb decoded is reused, never decoded again (S7 task
    // 11 — the shrine's ruins share their textures across dozens of glbs).
    std::unordered_map<std::string, int> scene_textures;
    for (std::size_t i = 0; i < scene.textures.size(); ++i) {
        scene_textures.emplace(scene.textures[i].sourceRelPath, static_cast<int>(i));
    }

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

        const std::string key = rel.generic_string();
        const auto shared = scene_textures.find(key);
        if (shared != scene_textures.end()) {
            return shared->second;
        }

        TextureCpu texture;
        DecodedTextures::iterator ready;
        if (decoded != nullptr && (ready = decoded->find(key)) != decoded->end()) {
            texture = std::move(ready->second);
            decoded->erase(ready);
        } else if (!decode_png(resolved, texture)) {
            std::fprintf(stderr, "sotc-viewer: skipping texture %s: %s\n", key.c_str(),
                         stbi_failure_reason());
            return -1;
        }
        texture.sourceRelPath = key;
        scene.textures.push_back(std::move(texture));
        const int index = static_cast<int>(scene.textures.size()) - 1;
        scene_textures.emplace(scene.textures.back().sourceRelPath, index);
        return index;
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

            // The cull box: the raw vertex bounds, before `model` applies.
            if (!out.vertices.empty()) {
                out.localMin = out.localMax =
                    Vec3{out.vertices[0].px, out.vertices[0].py, out.vertices[0].pz};
                for (const Vertex& vertex : out.vertices) {
                    out.localMin = Vec3{std::min(out.localMin.x, vertex.px),
                                        std::min(out.localMin.y, vertex.py),
                                        std::min(out.localMin.z, vertex.pz)};
                    out.localMax = Vec3{std::max(out.localMax.x, vertex.px),
                                        std::max(out.localMax.y, vertex.py),
                                        std::max(out.localMax.z, vertex.pz)};
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
        out.emissiveTexture = resolve_texture(material.emissive_texture.texture);

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
                    const std::string& playerGlbRelPath, std::size_t* playerMeshBegin,
                    const std::function<void()>& onProgress) {
    Scene scene;
    bool have_bounds = false;

    // Every PNG the scene will use, decoded in parallel up front, in load order.
    std::vector<std::string> glbs;
    for (const ManifestInstance& instance : manifest.instances) {
        glbs.push_back(instance.glbRelPath);
    }
    for (const ManifestInstance& floor : manifest.floors) {
        glbs.push_back(floor.glbRelPath);
    }
    glbs.push_back(playerGlbRelPath);
    for (const ManifestEntity& entity : manifest.entities) {
        glbs.push_back(entity.placement.glbRelPath);
    }
    for (const ManifestHorse& horse : manifest.horses) {
        glbs.push_back(horse.placement.glbRelPath);
    }
    for (const ManifestInstance& backdrop : manifest.backdrops) {
        glbs.push_back(backdrop.glbRelPath);
    }
    for (const ManifestPosed& posed : manifest.posed) {
        glbs.push_back(posed.glb);
    }
    DecodedTextures decoded = decode_textures(libraryDir, glbs, onProgress);
    const auto progress = [&onProgress] {
        if (onProgress) {
            onProgress();
        }
    };

    // Static instances: no rig record, meshes keep rig = -1.
    for (const ManifestInstance& instance : manifest.instances) {
        progress();
        const GlbPlacement placement{
            .model = mat4_from_trs(instance.position, instance.rotation, instance.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
        };
        try {
            load_glb_into(scene, libraryDir, instance.glbRelPath, placement, have_bounds, &decoded);
        } catch (const std::runtime_error& error) {
            // Name the manifest's own glb token: the resolved path alone is
            // harder to match back to the instance that placed it.
            throw std::runtime_error("compose_scene: instance \"" + instance.glbRelPath +
                                     "\": " + error.what());
        }
    }

    // S13 floors (arena_04's ruins): static ground like an instance, in the scene
    // bounds, but rasterized floors-only (up-facing triangles, no wall flags).
    for (const ManifestInstance& floor : manifest.floors) {
        progress();
        const std::size_t mesh_begin = scene.meshes.size();
        const GlbPlacement placement{
            .model = mat4_from_trs(floor.position, floor.rotation, floor.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
        };
        try {
            load_glb_into(scene, libraryDir, floor.glbRelPath, placement, have_bounds, &decoded);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("compose_scene: floor \"" + floor.glbRelPath + "\": " +
                                     error.what());
        }
        for (std::size_t m = mesh_begin; m < scene.meshes.size(); ++m) {
            scene.meshes[m].floorsOnly = true;
        }
    }

    // The player: identity model and rig 0, the dynamic rig. Its materials
    // follow every instance's, so they carry the same offset.
    const GlbPlacement player_placement{
        .materialOffset = static_cast<int>(scene.materials.size()),
        .includeRig = true,
        .dynamic = true,
    };
    progress();
    load_glb_into(scene, libraryDir, playerGlbRelPath, player_placement, have_bounds, &decoded);

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
        progress();
        const GlbPlacement placement{
            .model = mat4_from_trs(entity.placement.position, entity.placement.rotation,
                                   entity.placement.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
            .includeRig = true,
        };
        try {
            load_glb_into(scene, libraryDir, entity.placement.glbRelPath, placement, have_bounds,
                          &decoded);
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

    // The horse: its own rig like an entity's (skeleton and clips kept, no clip
    // required), recorded as Scene::horseRig and never an encounter entity.
    for (const ManifestHorse& horse : manifest.horses) {
        progress();
        const GlbPlacement placement{
            .model = mat4_from_trs(horse.placement.position, horse.placement.rotation,
                                   horse.placement.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
            .includeRig = true,
        };
        try {
            load_glb_into(scene, libraryDir, horse.placement.glbRelPath, placement, have_bounds,
                          &decoded);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("compose_scene: horse \"" + horse.placement.glbRelPath +
                                     "\": " + error.what());
        }
        if (scene.horseRig < 0) {
            scene.horseRig = static_cast<int>(scene.rigs.size()) - 1;
        }
    }

    // Backdrops: static meshes (rig = -1) that are drawn but never ground, and
    // stay out of the scene bounds (the heightmap grid is sized by them).
    for (const ManifestInstance& backdrop : manifest.backdrops) {
        progress();
        const Vec3 saved_min = scene.boundsMin;
        const Vec3 saved_max = scene.boundsMax;
        const bool saved_have = have_bounds;
        const std::size_t mesh_begin = scene.meshes.size();
        const GlbPlacement placement{
            .model = mat4_from_trs(backdrop.position, backdrop.rotation, backdrop.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
        };
        try {
            load_glb_into(scene, libraryDir, backdrop.glbRelPath, placement, have_bounds, &decoded);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("compose_scene: backdrop \"" + backdrop.glbRelPath +
                                     "\": " + error.what());
        }
        scene.boundsMin = saved_min;
        scene.boundsMax = saved_max;
        have_bounds = saved_have;
        for (std::size_t m = mesh_begin; m < scene.meshes.size(); ++m) {
            scene.meshes[m].ground = false;
        }
    }

    // Posed props: load like an entity (so the glb's skeleton and clips are
    // available), sample the named clip once, CPU-skin every vertex with the
    // shader's weighted palette sum, then drop the rig — the meshes stay static
    // (rig = -1) with the TRS in `model` and the cull box of the posed vertices.
    // The load's bind-pose bounds are discarded and replaced by the posed ones.
    for (const ManifestPosed& posed : manifest.posed) {
        progress();
        const Vec3 saved_min = scene.boundsMin;
        const Vec3 saved_max = scene.boundsMax;
        const bool saved_have = have_bounds;
        const std::size_t mesh_begin = scene.meshes.size();
        const GlbPlacement placement{
            .model = mat4_from_trs(posed.position, posed.rotation, posed.scale),
            .materialOffset = static_cast<int>(scene.materials.size()),
            .includeRig = true,
        };
        try {
            load_glb_into(scene, libraryDir, posed.glb, placement, have_bounds, &decoded);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("compose_scene: posed \"" + posed.glb + "\": " + error.what());
        }

        const std::size_t rig_index = scene.rigs.size() - 1;
        const AnimationCpu* clip = nullptr;
        for (const AnimationCpu& candidate : scene.rigs[rig_index].animations) {
            if (candidate.name == posed.clip) {
                clip = &candidate;
                break;
            }
        }
        if (clip == nullptr) {
            throw std::runtime_error("compose_scene: posed \"" + posed.glb + "\": clip \"" +
                                     posed.clip + "\" not found");
        }
        const std::vector<Mat4> palette = animation_pose(
            scene, rig_index, *clip, animation_clamp_time(posed.seconds, clip->duration));

        scene.boundsMin = saved_min;
        scene.boundsMax = saved_max;
        have_bounds = saved_have;
        for (std::size_t m = mesh_begin; m < scene.meshes.size(); ++m) {
            MeshCpu& mesh = scene.meshes[m];
            mesh.rig = -1;
            for (Vertex& vertex : mesh.vertices) {
                Vec3 point{0.0f, 0.0f, 0.0f};
                Vec3 normal{0.0f, 0.0f, 0.0f};
                float total_weight = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    const float weight = vertex.w[i];
                    if (weight == 0.0f || vertex.j[i] >= palette.size()) {
                        continue;
                    }
                    const Mat4& skin = palette[vertex.j[i]];
                    const Vec3 p = transform_point(skin, Vec3{vertex.px, vertex.py, vertex.pz});
                    point = Vec3{point.x + weight * p.x, point.y + weight * p.y,
                                 point.z + weight * p.z};
                    // The palette is rigid-ish: its upper 3x3 turns the normal.
                    const Vec3 n{skin.m[0] * vertex.nx + skin.m[4] * vertex.ny + skin.m[8] * vertex.nz,
                                 skin.m[1] * vertex.nx + skin.m[5] * vertex.ny + skin.m[9] * vertex.nz,
                                 skin.m[2] * vertex.nx + skin.m[6] * vertex.ny + skin.m[10] * vertex.nz};
                    normal = Vec3{normal.x + weight * n.x, normal.y + weight * n.y,
                                  normal.z + weight * n.z};
                    total_weight += weight;
                }
                if (total_weight > 0.0f) {  // zero weights pass through, like the shader
                    vertex.px = point.x;
                    vertex.py = point.y;
                    vertex.pz = point.z;
                    const Vec3 unit = vec3_normalize(normal);
                    vertex.nx = unit.x;
                    vertex.ny = unit.y;
                    vertex.nz = unit.z;
                }
                // The vertex is posed for good: a static mesh binds the player's live
                // palette (slot 0), so any weight left here would re-skin it with
                // Wander's joints every frame. Zero weights pass through the shader.
                for (int i = 0; i < 4; ++i) {
                    vertex.j[i] = 0;
                    vertex.w[i] = 0.0f;
                }
            }
            if (!mesh.vertices.empty()) {
                mesh.localMin = mesh.localMax =
                    Vec3{mesh.vertices[0].px, mesh.vertices[0].py, mesh.vertices[0].pz};
            }
            for (const Vertex& vertex : mesh.vertices) {
                mesh.localMin = Vec3{std::min(mesh.localMin.x, vertex.px),
                                     std::min(mesh.localMin.y, vertex.py),
                                     std::min(mesh.localMin.z, vertex.pz)};
                mesh.localMax = Vec3{std::max(mesh.localMax.x, vertex.px),
                                     std::max(mesh.localMax.y, vertex.py),
                                     std::max(mesh.localMax.z, vertex.pz)};
                const Vec3 world = transform_point(mesh.model, Vec3{vertex.px, vertex.py, vertex.pz});
                if (!have_bounds) {
                    scene.boundsMin = scene.boundsMax = world;
                    have_bounds = true;
                } else {
                    scene.boundsMin = Vec3{std::min(scene.boundsMin.x, world.x),
                                           std::min(scene.boundsMin.y, world.y),
                                           std::min(scene.boundsMin.z, world.z)};
                    scene.boundsMax = Vec3{std::max(scene.boundsMax.x, world.x),
                                           std::max(scene.boundsMax.y, world.y),
                                           std::max(scene.boundsMax.z, world.z)};
                }
            }
        }
        scene.rigs.pop_back();  // static: no rig, no playback
    }

    if (!have_bounds) {
        scene.boundsMin = Vec3{0.0f, 0.0f, 0.0f};
        scene.boundsMax = Vec3{0.0f, 0.0f, 0.0f};
    }
    return scene;
}

std::size_t append_static_model(Scene& scene, const std::filesystem::path& libraryDir,
                                const std::string& glbRelPath, const Mat4& model) {
    const std::size_t mesh_begin = scene.meshes.size();
    // Material indices must stay global; an already-composed scene has some.
    const GlbPlacement placement{
        .model = model,
        .materialOffset = static_cast<int>(scene.materials.size()),
    };
    // A scene with geometry grows its existing bounds; a default-constructed
    // scene (no meshes) starts them from this glb rather than from the origin.
    bool have_bounds = !scene.meshes.empty();
    load_glb_into(scene, libraryDir, glbRelPath, placement, have_bounds);
    return mesh_begin;
}
