#include "core/models/model_export.h"

#include "core/files/paths.h"

#include "khdays/assets/gltf_export.h"
#include "khdays/assets/image.h"

#include "cgltf.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>

namespace studio {

namespace {

namespace assets = khdays::assets;

const Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// The rotation nearest a 3x3 matrix (column-major), its polar decomposition
// by averaging with the inverse transpose; the matrix as it is when it does
// not settle (nearly singular).
std::array<float, 9> nearest_rotation(const std::array<float, 9>& m) {
    std::array<double, 9> r{};
    for (std::size_t i = 0; i < 9; ++i)
        r[i] = m[i];
    for (int step = 0; step < 30; ++step) {
        // inverse transpose = cofactor matrix / determinant
        const std::array<double, 9> c{r[4] * r[8] - r[5] * r[7], r[5] * r[6] - r[3] * r[8], r[3] * r[7] - r[4] * r[6],
                                      r[2] * r[7] - r[1] * r[8], r[0] * r[8] - r[2] * r[6], r[1] * r[6] - r[0] * r[7],
                                      r[1] * r[5] - r[2] * r[4], r[2] * r[3] - r[0] * r[5], r[0] * r[4] - r[1] * r[3]};
        const double det = r[0] * c[0] + r[1] * c[1] + r[2] * c[2];
        if (std::fabs(det) < 1e-9)
            return m;
        double moved = 0.0;
        for (std::size_t i = 0; i < 9; ++i) {
            const double next = 0.5 * (r[i] + c[i] / det);
            moved = std::max(moved, std::fabs(next - r[i]));
            r[i] = next;
        }
        if (moved < 1e-9)
            break;
    }
    std::array<float, 9> out{};
    for (std::size_t i = 0; i < 9; ++i)
        out[i] = static_cast<float>(r[i]);
    return out;
}

// The animation with a rotation at each frame. Between two of a curve's
// samples the reader blends their matrices entry by entry, as the DS does,
// which shortens the bone's axes there; glTF keys are rotations, so each
// frame's blend becomes the rotation nearest it -- a curve of one sample per
// frame -- and the keys do not shrink the bones between samples.
assets::SkeletalAnimation rotations_per_frame(const assets::SkeletalAnimation& a, std::size_t frames) {
    assets::SkeletalAnimation rotations = a;  // the rotations alone: no translation, scale 1
    for (auto& b : rotations.bones) {
        b.translation = {};
        b.scale = {};
    }
    assets::SkeletalAnimation out = a;
    const std::vector<Mat> none(a.bones.size(), kIdentity);
    for (std::size_t f = 0; f < frames; ++f) {
        const std::vector<Mat> m = assets::sample_animation(rotations, static_cast<float>(f), none);
        for (std::size_t b = 0; b < out.bones.size() && b < m.size(); ++b) {
            assets::AnimationRotationCurve& r = out.bones[b].rotation;
            if (r.kind != assets::AnimationRotationCurve::Kind::Samples)
                continue;
            if (f == 0) {
                r.samples.clear();
                r.start_frame = 0;
                r.end_frame = static_cast<std::uint16_t>(frames);
            }
            const Mat& x = m[b];
            r.samples.push_back(nearest_rotation({x[0], x[1], x[2], x[4], x[5], x[6], x[8], x[9], x[10]}));
        }
    }
    return out;
}

// The pictures the dump's glTF of the model draws each texture with: its
// materials are named after the textures.
std::map<std::string, fs::path> dump_pictures(const fs::path& gltf) {
    std::map<std::string, fs::path> out;
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string path = u8(gltf);
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
        return out;
    const std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        const cgltf_texture* t = m.pbr_metallic_roughness.base_color_texture.texture;
        if (m.name == nullptr || t == nullptr || t->image == nullptr || t->image->uri == nullptr)
            continue;
        std::string relative(t->image->uri);
        cgltf_decode_uri(relative.data());
        relative.resize(std::strlen(relative.c_str()));
        if (const auto file = assets::find_texture(gltf.parent_path(), relative))
            out.emplace(m.name, *file);
    }
    return out;
}

}  // namespace

std::vector<std::string> export_animation_names(const GameModel& g) {
    std::vector<std::string> out(g.animations.size());
    std::map<std::string, int> given;
    for (std::size_t i = 0; i < g.animations.size(); ++i) {
        if (!g.animations[i].fits)
            continue;
        const std::string& name = g.animations[i].name;
        const int n = ++given[name];
        out[i] = n == 1 ? name : name + "_" + std::to_string(n);
    }
    return out;
}

std::string export_animation_name(const GameModel& g, const GameAnimation& a) {
    const std::vector<std::string> names = export_animation_names(g);
    for (std::size_t i = 0; i < g.animations.size(); ++i) {
        const GameAnimation& b = g.animations[i];
        if (b.label == a.label && b.offset == a.offset && b.index == a.index)
            return names[i];
    }
    return a.name;
}

int exported_animation_count(const GameModel& g) {
    int n = 0;
    for (const auto& a : g.animations)
        n += a.fits ? 1 : 0;
    return n;
}

bool export_game_model(const fs::path& nitrofs, const fs::path& graphics_dump, const GameModel& g, bool animations,
                       const fs::path& out, ModelExport* result, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    ModelExport done;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);

    // the textures, beside the glTF
    const std::map<std::string, fs::path> pictures =
        dump_pictures(graphics_dump / path_u8(g.label + "/mdl/" + g.name + ".gltf"));
    std::map<std::string, assets::GltfTextureRef> refs;
    for (const auto& mesh : g.model.meshes) {
        if (mesh.texture_name.empty() || refs.count(mesh.texture_name) != 0)
            continue;
        fs::path picture;
        if (const auto p = pictures.find(mesh.texture_name); p != pictures.end()) {
            picture = p->second;
        } else if (const auto t = g.textures.find(mesh.texture_name); t != g.textures.end()) {
            picture = t->second;
        } else {
            continue;  // not dumped: drawn untextured, as the dump's glTF would
        }
        int w = 0, h = 0;
        const char* error = nullptr;
        std::uint8_t* px = khdays_png_read(u8(picture).c_str(), &w, &h, &error);
        if (px == nullptr)
            continue;
        const bool translucent =
            assets::texels_translucent(px, static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        khdays_png_free(px);
        const fs::path copy = out.parent_path() / picture.filename();
        if (!(fs::exists(copy, ec) && fs::equivalent(copy, picture, ec))) {
            fs::copy_file(picture, copy, fs::copy_options::overwrite_existing, ec);
            if (ec)
                return fail("could not write " + u8(copy) + ": " + ec.message());
        }
        ++done.files;
        refs[mesh.texture_name] = {u8(picture.filename()), w, h, translucent};
    }

    // the animations made for its skeleton, each file's data read once
    std::vector<assets::GltfAnimation> list;
    if (animations) {
        const std::vector<std::string> names = export_animation_names(g);
        std::map<std::string, std::vector<std::uint8_t>> data;
        for (std::size_t i = 0; i < g.animations.size(); ++i) {
            const GameAnimation& a = g.animations[i];
            if (!a.fits)
                continue;
            auto d = data.find(a.label);
            if (d == data.end())
                d = data.emplace(a.label, game_label_data(nitrofs, a.label)).first;
            if (a.offset + a.size > d->second.size())
                return fail("no data for " + a.label);
            assets::SkeletalAnimation animation;
            try {
                animation = assets::load_nsbca(&d->second[a.offset], a.size, a.index);
            } catch (const std::exception& e) {
                return fail(a.label + ": " + e.what());
            }
            const std::size_t frames = std::max<std::size_t>(1, animation.frame_count);
            const assets::SkeletalAnimation rotated = rotations_per_frame(animation, frames);
            assets::GltfAnimation out_animation;
            out_animation.name = names[i];
            for (std::size_t f = 0; f < frames; ++f) {
                out_animation.frames.push_back(
                    assets::sample_animation(rotated, static_cast<float>(f), g.model.object_matrices));
            }
            list.push_back(std::move(out_animation));
        }
    }

    const std::string text = assets::to_gltf(g.model, refs, list);
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    if (!file)
        return fail("could not write " + u8(out));
    ++done.files;
    done.animations = g.model.skinning != nullptr ? static_cast<int>(list.size()) : 0;
    if (result != nullptr)
        *result = done;
    return true;
}

}  // namespace studio
