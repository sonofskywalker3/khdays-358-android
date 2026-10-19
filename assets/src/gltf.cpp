#include "khdays/assets/gltf.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/image.h"

// cgltf is third-party and not warning-clean; isolate it.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

using khdays::assets::ImportedAnimation;
using khdays::assets::ImportedTrack;
using khdays::assets::NeutralMesh;

const cgltf_accessor* find_attribute(const cgltf_primitive& primitive, const cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const auto& attr = primitive.attributes[i];
        if (attr.type == type && attr.index == 0) {
            return attr.data;
        }
    }
    return nullptr;
}

// The base-color image referenced by a primitive's material, or nullptr.
const cgltf_image* base_color_image(const cgltf_primitive& primitive) {
    const cgltf_material* material = primitive.material;
    if (material == nullptr || material->has_pbr_metallic_roughness == 0) {
        return nullptr;
    }
    const cgltf_texture* texture = material->pbr_metallic_roughness.base_color_texture.texture;
    if (texture == nullptr) {
        return nullptr;
    }
    return texture->image;
}

// A small JSON reader for the port's own extras (khdays_sky, khdays_scroll,
// khdays_frames, khdays_loop): cgltf keeps an object's extras as JSON text.
struct Json final {
    enum class Kind { null, boolean, number, string, array, object };
    Kind kind = Kind::null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* get(const char* key) const {
        for (const auto& [name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
    bool is_true() const {
        return kind == Kind::boolean && boolean;
    }
};

class JsonReader final {
public:
    explicit JsonReader(const char* text) : p_(text) {}

    Json value() {
        space();
        Json v;
        if (*p_ == '{') {
            v.kind = Json::Kind::object;
            ++p_;
            space();
            if (*p_ == '}') {
                ++p_;
                return v;
            }
            for (;;) {
                space();
                std::string key = string();
                space();
                expect(':');
                v.members.emplace_back(std::move(key), value());
                space();
                if (*p_ == ',') {
                    ++p_;
                    continue;
                }
                expect('}');
                return v;
            }
        }
        if (*p_ == '[') {
            v.kind = Json::Kind::array;
            ++p_;
            space();
            if (*p_ == ']') {
                ++p_;
                return v;
            }
            for (;;) {
                v.items.push_back(value());
                space();
                if (*p_ == ',') {
                    ++p_;
                    continue;
                }
                expect(']');
                return v;
            }
        }
        if (*p_ == '"') {
            v.kind = Json::Kind::string;
            v.text = string();
            return v;
        }
        for (const char* word : {"true", "false", "null"}) {
            const std::size_t n = std::strlen(word);
            if (std::strncmp(p_, word, n) == 0) {
                p_ += n;
                v.kind = word[0] == 'n' ? Json::Kind::null : Json::Kind::boolean;
                v.boolean = word[0] == 't';
                return v;
            }
        }
        char* end = nullptr;
        v.number = std::strtod(p_, &end);
        if (end == p_) {
            throw std::runtime_error("extras: not JSON");
        }
        v.kind = Json::Kind::number;
        p_ = end;
        return v;
    }

private:
    void space() {
        while (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r') {
            ++p_;
        }
    }
    void expect(const char c) {
        if (*p_ != c) {
            throw std::runtime_error("extras: not JSON");
        }
        ++p_;
    }
    std::string string() {
        expect('"');
        std::string s;
        while (*p_ != '\0' && *p_ != '"') {
            if (*p_ == '\\' && p_[1] != '\0') {
                ++p_;
            }
            s += *p_++;
        }
        expect('"');
        return s;
    }

    const char* p_;
};

// An object's extras, or null when it has none (or they are not JSON).
Json extras(const cgltf_extras& e) {
    if (e.data == nullptr) {
        return {};
    }
    try {
        return JsonReader(e.data).value();
    } catch (const std::exception&) {
        return {};
    }
}

// A material's moving textures (the port's extras khdays_scroll and khdays_frames) into `mesh`: a
// scroll in 0..1 a game frame, kept as texels of its `w` x `h` texture, and pictures shown in turn.
// `picture` loads a picture the frames name; one it cannot is drawn as the mesh's own texture.
template <typename LoadPicture>
void read_moving_textures(const Json& e, float w, float h, const std::string& own, const LoadPicture& picture,
                          NeutralMesh& mesh) {
    const Json* scroll = e.get("khdays_scroll");
    if (scroll != nullptr && scroll->items.size() == 2) {
        mesh.scroll = {static_cast<float>(scroll->items[0].number) * w,
                       static_cast<float>(scroll->items[1].number) * h};
    }
    const Json* frames = e.get("khdays_frames");
    const Json* pictures = frames != nullptr ? frames->get("pictures") : nullptr;
    const Json* steps = frames != nullptr ? frames->get("frames") : nullptr;
    if (pictures == nullptr || steps == nullptr) {
        return;
    }
    std::vector<std::string> names;
    for (const Json& name : pictures->items) {
        names.push_back(picture(name.text) ? name.text : own);
    }
    for (const Json& step : steps->items) {
        if (step.items.size() != 2) {
            continue;
        }
        const auto shown = static_cast<std::size_t>(step.items[0].number);
        mesh.texture_frames.emplace_back(shown < names.size() ? names[shown] : own,
                                         static_cast<int>(step.items[1].number));
    }
    const Json* loop = frames->get("loop");
    mesh.texture_frames_loop = loop == nullptr || loop->is_true();
    const Json* from = frames->get("loop_from");
    mesh.texture_frames_loop_from = from != nullptr ? static_cast<std::size_t>(from->number) : 0U;
}

// The file's own animations of its nodes (the port's extras khdays_loop and khdays_loop_from say how
// they loop; morph weights are not read).
std::vector<ImportedAnimation> read_animations(const cgltf_data* data) {
    std::vector<ImportedAnimation> animations;
    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& animation = data->animations[a];
        ImportedAnimation out;
        out.name = animation.name != nullptr ? animation.name : "animation_" + std::to_string(a);
        const Json e = extras(animation.extras);
        const Json* loop = e.get("khdays_loop");
        out.loop = loop != nullptr && loop->is_true();
        const Json* from = e.get("khdays_loop_from");
        out.loop_from = from != nullptr ? static_cast<float>(from->number) : 0.0F;
        for (cgltf_size c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            const cgltf_animation_sampler* sampler = channel.sampler;
            if (sampler == nullptr || channel.target_node == nullptr || sampler->input == nullptr ||
                sampler->output == nullptr) {
                continue;
            }
            ImportedTrack track;
            track.node = static_cast<int>(channel.target_node - data->nodes);
            cgltf_size n = 3;
            switch (channel.target_path) {
            case cgltf_animation_path_type_translation: track.path = ImportedTrack::Path::translation; break;
            case cgltf_animation_path_type_rotation:
                track.path = ImportedTrack::Path::rotation;
                n = 4;
                break;
            case cgltf_animation_path_type_scale: track.path = ImportedTrack::Path::scale; break;
            default: continue;
            }
            track.step = sampler->interpolation == cgltf_interpolation_type_step;
            // a cubic spline's keys hold (in tangent, value, out tangent): its values, drawn linear
            const bool cubic = sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            const cgltf_size keys = sampler->input->count;
            track.times.resize(keys);
            track.values.resize(keys * n);
            for (cgltf_size k = 0; k < keys; ++k) {
                cgltf_accessor_read_float(sampler->input, k, &track.times[k], 1);
                cgltf_accessor_read_float(sampler->output, cubic ? k * 3 + 1 : k, &track.values[k * n], n);
                out.duration = std::max(out.duration, track.times[k]);
            }
            out.tracks.push_back(std::move(track));
        }
        if (!out.tracks.empty()) {
            animations.push_back(std::move(out));
        }
    }
    return animations;
}

std::string image_key(const cgltf_image* image, std::size_t fallback_index) {
    if (image != nullptr && image->name != nullptr && image->name[0] != '\0') {
        return image->name;
    }
    if (image != nullptr && image->uri != nullptr) {
        return image->uri;
    }
    return "gltf_image_" + std::to_string(fallback_index);
}

// Column-major 4x4 multiply (result = a * b), matching the neutral palette's
// m[col * 4 + row] layout. glTF stores matrices column-major, so cgltf's
// transforms and inverse binds drop straight in.
std::array<float, 16> matrix_multiply(const float* a, const float* b) {
    std::array<float, 16> result{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return result;
}

constexpr std::array<float, 16> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

}  // namespace

namespace khdays::assets {

ImportedModel import_gltf(const std::filesystem::path& input_path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const auto path_string = input_path.string();

    if (cgltf_parse_file(&options, path_string.c_str(), &data) != cgltf_result_success) {
        throw std::runtime_error("cannot parse glTF: " + path_string);
    }
    if (cgltf_load_buffers(&options, data, path_string.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cannot load glTF buffers: " + path_string);
    }

    ImportedModel result;
    result.model.name = input_path.stem().string();
    // Palette entry 0 is the identity, used by static (unskinned) meshes.
    result.model.palette.push_back(kIdentity);

    const auto base_dir = input_path.parent_path();

    // Load each referenced image once, keyed by name; remember its size. An
    // image is embedded (a buffer view -- a .glb's -- or a data URI) or a file
    // found by find_texture; one that cannot be had is drawn white and told
    // to the caller.
    std::map<std::string, DecodedTexture> loaded;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& image = data->images[i];
        const auto key = image_key(&image, i);
        if (loaded.count(key) != 0U) {
            continue;
        }
        try {
            DecodedTexture texture;
            if (image.buffer_view != nullptr) {
                const cgltf_buffer_view& view = *image.buffer_view;
                if (view.buffer == nullptr || view.buffer->data == nullptr) {
                    throw std::runtime_error("its buffer was not loaded");
                }
                texture = read_image(static_cast<const std::uint8_t*>(view.buffer->data) + view.offset, view.size, key);
            } else if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0) {
                const char* comma = std::strchr(image.uri, ',');
                if (comma == nullptr || std::strstr(image.uri, ";base64,") == nullptr) {
                    throw std::runtime_error("a data URI that is not base64");
                }
                const std::size_t length = std::strlen(comma + 1);
                std::size_t size = length / 4 * 3;
                if (length >= 1 && comma[length] == '=')
                    --size;
                if (length >= 2 && comma[length - 1] == '=')
                    --size;
                void* bytes = nullptr;
                if (cgltf_load_buffer_base64(&options, size, comma + 1, &bytes) != cgltf_result_success) {
                    throw std::runtime_error("its base64 cannot be decoded");
                }
                try {
                    texture = read_image(static_cast<const std::uint8_t*>(bytes), size, key);
                } catch (...) {
                    std::free(bytes);
                    throw;
                }
                std::free(bytes);
            } else if (image.uri != nullptr) {
                std::string uri = image.uri;
                cgltf_decode_uri(uri.data());  // %20 and the like
                uri.resize(std::strlen(uri.c_str()));
                const auto file = find_texture(base_dir, uri);
                if (!file) {
                    throw std::runtime_error("not found");
                }
                texture = read_image(*file);
            } else {
                throw std::runtime_error("no data and no file");
            }
            texture.name = key;
            loaded.emplace(key, std::move(texture));
        } catch (const std::exception& error) {
            // The mesh falls back to white; the caller says so.
            result.missing_textures.emplace_back(key, error.what());
        }
    }

    // The nodes at rest, and which are in a sky (a node marked khdays_sky, or under one).
    const auto node_index = [&](const cgltf_node* node) {
        return node != nullptr ? static_cast<int>(node - data->nodes) : -1;
    };
    std::vector<char> sky(data->nodes_count, 0);
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        ImportedNode out;
        out.name = node.name != nullptr ? node.name : "";
        out.parent = node_index(node.parent);
        if (node.has_translation != 0) {
            std::copy(node.translation, node.translation + 3, out.translation.begin());
        }
        if (node.has_rotation != 0) {
            std::copy(node.rotation, node.rotation + 4, out.rotation.begin());
        }
        if (node.has_scale != 0) {
            std::copy(node.scale, node.scale + 3, out.scale.begin());
        }
        cgltf_node_transform_local(&node, out.local.data());
        result.nodes.push_back(std::move(out));
        for (const cgltf_node* up = &node; up != nullptr && sky[n] == 0; up = up->parent) {
            const Json e = extras(up->extras);
            const Json* flag = e.get("khdays_sky");
            sky[n] = flag != nullptr && flag->is_true() ? 1 : 0;
        }
    }

    // One palette block per skin, joint j's jointGlobalTransform[j] * inverseBind[j]: the identity in
    // the authored pose, re-posed by animated joints. A skinned mesh's node transform is ignored, as
    // glTF says.
    std::map<const cgltf_skin*, std::uint32_t> skin_base;
    for (cgltf_size s = 0; s < data->skins_count; ++s) {
        const cgltf_skin& skin = data->skins[s];
        skin_base.emplace(&skin, static_cast<std::uint32_t>(result.model.palette.size()));
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            cgltf_float world[16];
            cgltf_node_transform_world(skin.joints[j], world);
            std::array<float, 16> inverse_bind = kIdentity;
            if (skin.inverse_bind_matrices != nullptr) {
                cgltf_accessor_read_float(skin.inverse_bind_matrices, j, inverse_bind.data(), 16);
            }
            ImportedJoint joint;
            joint.palette_index = static_cast<std::uint32_t>(result.model.palette.size());
            const cgltf_node* joint_node = skin.joints[j];
            if (joint_node != nullptr && joint_node->name != nullptr) {
                joint.name = joint_node->name;
            }
            joint.inverse_bind = inverse_bind;
            joint.node = node_index(joint_node);
            std::copy(world, world + 16, joint.world.begin());
            result.joints.push_back(std::move(joint));
            result.model.palette.push_back(matrix_multiply(world, inverse_bind.data()));
        }
        // Each joint's parent: its nearest ancestor among the skin's joints.
        const std::size_t first = result.joints.size() - skin.joints_count;
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            for (const cgltf_node* up = skin.joints[j]->parent; up != nullptr && result.joints[first + j].parent < 0;
                 up = up->parent) {
                for (cgltf_size k = 0; k < skin.joints_count; ++k) {
                    if (skin.joints[k] == up) {
                        result.joints[first + j].parent = static_cast<int>(first + k);
                        break;
                    }
                }
            }
        }
    }

    // A picture a material's frames name (not one of the file's images): loaded beside it once.
    const auto picture = [&](const std::string& uri) {
        if (loaded.count(uri) != 0U) {
            return true;
        }
        try {
            const auto file = find_texture(base_dir, uri);
            if (!file) {
                throw std::runtime_error("not found");
            }
            DecodedTexture texture = read_image(*file);
            texture.name = uri;
            loaded.emplace(uri, std::move(texture));
            return true;
        } catch (const std::exception& error) {
            result.missing_textures.emplace_back(uri, error.what());
            return false;
        }
    };

    std::array<float, 4> position{};
    std::array<float, 2> uv{};

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (node.mesh == nullptr) {
            continue;
        }
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        const cgltf_mesh& mesh = *node.mesh;
        const cgltf_size m = n;  // index used for names

        // A skinned node places its geometry through the skeleton, so its own
        // world transform is ignored and each vertex carries bone indices.
        const cgltf_skin* skin = node.skin;
        const auto skin_it = skin != nullptr ? skin_base.find(skin) : skin_base.end();
        const std::uint32_t joint_base = skin_it != skin_base.end() ? skin_it->second : 0U;

        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) {
                continue;
            }
            const cgltf_accessor* positions = find_attribute(primitive, cgltf_attribute_type_position);
            if (positions == nullptr) {
                continue;
            }
            const cgltf_accessor* texcoords = find_attribute(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* colors = find_attribute(primitive, cgltf_attribute_type_color);
            const cgltf_accessor* normals = find_attribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* joints =
                skin != nullptr ? find_attribute(primitive, cgltf_attribute_type_joints) : nullptr;
            const cgltf_accessor* weights =
                skin != nullptr ? find_attribute(primitive, cgltf_attribute_type_weights) : nullptr;
            const bool skinned = joints != nullptr && weights != nullptr;

            const cgltf_image* image = base_color_image(primitive);
            const auto texture_key = image != nullptr ? image_key(image, m) : std::string{};
            float tex_w = 1.0F;
            float tex_h = 1.0F;
            const auto texture_it = loaded.find(texture_key);
            if (texture_it != loaded.end()) {
                tex_w = static_cast<float>(texture_it->second.width);
                tex_h = static_cast<float>(texture_it->second.height);
            }

            NeutralMesh out_mesh;
            out_mesh.name = mesh.name != nullptr ? mesh.name : ("mesh_" + std::to_string(m));
            out_mesh.texture_name = texture_key;
            out_mesh.material_name =
                primitive.material != nullptr && primitive.material->name != nullptr ? primitive.material->name : "";
            out_mesh.double_sided = primitive.material != nullptr && primitive.material->double_sided != 0;
            if (primitive.material != nullptr) {
                switch (primitive.material->alpha_mode) {
                case cgltf_alpha_mode_mask:
                    out_mesh.alpha_mode = NeutralMesh::AlphaMode::mask;
                    out_mesh.alpha_cutoff = primitive.material->alpha_cutoff;
                    break;
                case cgltf_alpha_mode_blend: out_mesh.alpha_mode = NeutralMesh::AlphaMode::blend; break;
                default: out_mesh.alpha_mode = NeutralMesh::AlphaMode::opaque; break;
                }
            } else {
                out_mesh.alpha_mode = NeutralMesh::AlphaMode::opaque;  // glTF's default material
            }
            out_mesh.sky = sky[n] != 0;
            if (primitive.material != nullptr) {
                read_moving_textures(extras(primitive.material->extras), tex_w, tex_h, texture_key, picture, out_mesh);
            }

            const auto vertex_count = positions->count;
            out_mesh.vertices.reserve(vertex_count);
            for (cgltf_size v = 0; v < vertex_count; ++v) {
                NeutralVertex vertex;
                cgltf_accessor_read_float(positions, v, position.data(), 3);
                if (skinned) {
                    // The skeleton (palette) places the geometry; keep the raw
                    // position, exactly like a DS vertex.
                    vertex.position = {position[0], position[1], position[2]};
                } else {
                    // Static mesh: bake in the node's world transform.
                    vertex.position = {
                        world[0] * position[0] + world[4] * position[1] + world[8] * position[2] + world[12],
                        world[1] * position[0] + world[5] * position[1] + world[9] * position[2] + world[13],
                        world[2] * position[0] + world[6] * position[1] + world[10] * position[2] + world[14]};
                }

                std::array<float, 3> normal{};
                if (normals != nullptr && cgltf_accessor_read_float(normals, v, normal.data(), 3)) {
                    // Like the position: raw when skinned, else through the
                    // node's transform (its 3x3; renormalize before lighting).
                    vertex.normal =
                        skinned
                            ? normal
                            : std::array<float, 3>{world[0] * normal[0] + world[4] * normal[1] + world[8] * normal[2],
                                                   world[1] * normal[0] + world[5] * normal[1] + world[9] * normal[2],
                                                   world[2] * normal[0] + world[6] * normal[1] + world[10] * normal[2]};
                }

                if (texcoords != nullptr && cgltf_accessor_read_float(texcoords, v, uv.data(), 2)) {
                    // Store as texels so the renderer normalizes like DS UVs.
                    vertex.texcoord = {uv[0] * tex_w, uv[1] * tex_h};
                }

                std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
                if (colors != nullptr) {
                    cgltf_accessor_read_float(colors, v, color.data(), 4);
                }
                vertex.color = {
                    static_cast<std::uint8_t>(color[0] * 255.0F), static_cast<std::uint8_t>(color[1] * 255.0F),
                    static_cast<std::uint8_t>(color[2] * 255.0F), static_cast<std::uint8_t>(color[3] * 255.0F)};

                if (skinned) {
                    cgltf_uint joint_indices[4] = {0U, 0U, 0U, 0U};
                    cgltf_accessor_read_uint(joints, v, joint_indices, 4);
                    cgltf_accessor_read_float(weights, v, vertex.weights.data(), 4);
                    for (int i = 0; i < 4; ++i) {
                        vertex.joints[static_cast<std::size_t>(i)] = joint_base + joint_indices[i];
                    }
                }
                // Static vertices keep the defaults: palette 0 (identity),
                // weight 1.
                out_mesh.vertices.push_back(vertex);
            }

            if (primitive.indices != nullptr) {
                out_mesh.indices.reserve(primitive.indices->count);
                for (cgltf_size k = 0; k < primitive.indices->count; ++k) {
                    out_mesh.indices.push_back(
                        static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, k)));
                }
            } else {
                for (cgltf_size k = 0; k < vertex_count; ++k) {
                    out_mesh.indices.push_back(static_cast<std::uint32_t>(k));
                }
            }

            result.model.header_vertex_count += static_cast<std::uint16_t>(out_mesh.vertices.size());
            result.model.meshes.push_back(std::move(out_mesh));
        }
    }

    result.animations = read_animations(data);

    for (auto& [key, texture] : loaded) {
        result.textures.emplace_back(key, std::move(texture));
    }

    cgltf_free(data);
    return result;
}

}  // namespace khdays::assets
