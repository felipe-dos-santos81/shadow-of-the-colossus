#include "asset_io.h"

#include <algorithm>
#include <cstddef>
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

}  // namespace

Scene load_scene(const std::filesystem::path& libraryDir, const std::string& glbRelPath) {
    const std::filesystem::path library = std::filesystem::weakly_canonical(libraryDir);
    const std::filesystem::path model_path = library / glbRelPath;
    const std::filesystem::path model_dir = model_path.parent_path();
    const std::string model_path_string = model_path.string();

    cgltf_options options{};
    CgltfDocument document;

    cgltf_result result = cgltf_parse_file(&options, model_path_string.c_str(), &document.data);
    if (result != cgltf_result_success) {
        throw std::runtime_error("load_scene: cannot parse " + model_path_string + ": " +
                                 cgltf_result_name(result));
    }
    // GLB buffers are embedded; .gltf fixtures carry data URIs. Both are handled
    // here, so no external .bin is required.
    result = cgltf_load_buffers(&options, document.data, model_path_string.c_str());
    if (result != cgltf_result_success) {
        throw std::runtime_error("load_scene: cannot load buffers of " + model_path_string + ": " +
                                 cgltf_result_name(result));
    }

    Scene scene;
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

    for (cgltf_size m = 0; m < document.data->meshes_count; ++m) {
        const cgltf_mesh& mesh = document.data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];

            const cgltf_accessor* position =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            if (position == nullptr) {
                throw std::runtime_error("load_scene: primitive without POSITION in " +
                                         model_path_string);
            }
            const cgltf_accessor* normal =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* texcoord =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);

            MeshCpu out;
            out.materialIndex =
                primitive.material != nullptr
                    ? static_cast<int>(cgltf_material_index(document.data, primitive.material))
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

                if (!have_vertex) {
                    bounds_min = Vec3{vertex.px, vertex.py, vertex.pz};
                    bounds_max = bounds_min;
                    have_vertex = true;
                } else {
                    bounds_min.x = std::min(bounds_min.x, vertex.px);
                    bounds_min.y = std::min(bounds_min.y, vertex.py);
                    bounds_min.z = std::min(bounds_min.z, vertex.pz);
                    bounds_max.x = std::max(bounds_max.x, vertex.px);
                    bounds_max.y = std::max(bounds_max.y, vertex.py);
                    bounds_max.z = std::max(bounds_max.z, vertex.pz);
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

    scene.boundsMin = have_vertex ? bounds_min : Vec3{0.0f, 0.0f, 0.0f};
    scene.boundsMax = have_vertex ? bounds_max : Vec3{0.0f, 0.0f, 0.0f};
    return scene;
}
