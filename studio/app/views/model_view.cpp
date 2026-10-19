// Models: a mod's FBX or glTF model on one of the game's models -- side by side or overlaid, joints
// paired by name as the port pairs them, by position as proposals or by hand, scale, back faces, the
// game's animations -- saved where the port reads them; and an animation replaced by a glTF clip,
// saved as the whole game file in the mod's files/ (docs/STUDIO.md).
#include "../app.h"
#include "../save_dialog.h"

#include "core/models/anim_replace.h"
#include "core/models/bones.h"
#include "core/models/clip_import.h"
#include "core/files/file_write.h"
#include "core/models/model_export.h"
#include "core/models/model_files.h"
#include "core/files/paths.h"
#include "../preview3d.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>

namespace studio {

namespace {

namespace assets = khdays::assets;

struct Texture {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
};

struct Session {
    int item = -1;
    std::string mod;  // the mod it was opened with
    bool game_ok = false;
    GameModel game;
    std::string game_error;
    std::map<std::string, Texture> game_textures;

    fs::path source;  // the mod's model file
    bool have_model = false;
    assets::ImportedModel model;
    std::string model_error;
    std::map<std::string, std::size_t> model_texture;  // name -> model.textures index
    std::vector<std::string> not_found;                // companions not found

    std::map<std::string, std::string> pairs;  // the pairs bones.txt will hold
    std::vector<std::string> comments;
    float scale = 1.0f;
    Cull cull = Cull::own;
    Rig rig;
    std::map<int, int> proposals;

    int animation = -1;
    assets::SkeletalAnimation clip;  // the one shown
    float frame = 0.0f;
    bool playing = false;

    // the shown animation replaced by a clip
    int clip_for = -1;  // the animation it replaces, -1: none
    fs::path clip_file;
    Clip new_clip;
    std::string clip_error;
    assets::SkeletalAnimation game_animation, new_animation;  // as the game has it, as it will read the new one
    bool show_new = false;
    std::vector<std::string> not_plain;

    int view = 0;  // 0 side by side, 1 over the game's skeleton
    bool bones = true;
    bool export_animations = false;  // the export asked for writes them
    float yaw = 30.0f, pitch = 10.0f, distance = 4.0f;
    std::array<float, 3> target{0, 0.8f, 0};
    float offset = 0.5f;
    int pick_joint = -1, pick_node = -1;
    std::unique_ptr<Preview> preview;
};

Session s;

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

const Texture* game_texture(const std::string& name) {
    const auto found = s.game_textures.find(name);
    if (found != s.game_textures.end())
        return found->second.rgba.empty() ? nullptr : &found->second;
    Texture t;
    const auto file = s.game.textures.find(name);
    if (file != s.game.textures.end()) {
        const char* error = nullptr;
        std::uint8_t* px = khdays_png_read(u8(file->second).c_str(), &t.width, &t.height, &error);
        if (px != nullptr) {
            t.rgba.assign(px, px + static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4u);
            khdays_png_free(px);
        }
    }
    const auto it = s.game_textures.emplace(name, std::move(t)).first;
    return it->second.rgba.empty() ? nullptr : &it->second;
}

void rebuild() {
    if (!s.game_ok || !s.have_model)
        return;
    s.rig = build_rig(s.game, s.model, s.pairs, s.scale);
    s.proposals = propose_pairs(s.game, s.rig);
}

void frame_camera() {
    // around the game model's nodes as shown (at rest, or as its animation poses them)
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    std::vector<Mat> shown = s.game.rest;
    if (s.animation >= 0)
        pose_game_model(s.game, &s.clip, s.frame, &shown, nullptr);
    for (const auto& w : shown) {
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], w[static_cast<std::size_t>(12 + c)]);
            hi[c] = std::max(hi[c], w[static_cast<std::size_t>(12 + c)]);
        }
    }
    if (lo[0] > hi[0])
        return;
    const float height = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 0.1f});
    s.target = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    s.distance = height * 2.6f;
    s.offset = height * 0.4f;
}

void open_item(App& app, int item) {
    const Item& it = app.catalog.items[static_cast<std::size_t>(item)];
    const Mod* m = app.current_mod();
    Session fresh;
    fresh.item = item;
    fresh.mod = m != nullptr ? m->name : std::string();
    fresh.preview = std::move(s.preview);
    // how the views are shown stays as chosen from model to model
    fresh.view = s.view;
    fresh.bones = s.bones;
    s = std::move(fresh);
    s.game_ok =
        load_game_model(nitrofs(), resolve(app.settings, app.settings.graphics_dump), it.key, &s.game, &s.game_error);
    // shown as the game shows it, moved by its own first animation: its rest
    // pose (how it was built) can look nothing like it (the Darkside, DS01,
    // is built standing with its arms out)
    if (s.game_ok && !s.game.animations.empty() && s.game.animations.front().fits) {
        std::string why;
        if (load_game_animation(nitrofs(), s.game.animations.front(), &s.clip, &why)) {
            s.animation = 0;
            s.frame = 0.0f;
            s.playing = true;
        }
    }
    if (s.game_ok)
        frame_camera();
    // the mod's model already there, with its bones.txt
    if (m != nullptr) {
        for (const auto& f : files_in_mod(m->dir, it)) {
            if (model_extension(lower(u8(f.extension())))) {
                model_offer(app, it, f);
                break;
            }
        }
    }
}

std::string pair_how(App& app, int j, ImVec4* colour) {
    const RigJoint& joint = s.rig.joints[static_cast<std::size_t>(j)];
    if (s.pairs.count(joint.name) != 0) {
        *colour = ImVec4(0.45f, 0.85f, 0.45f, 1);
        return tr(app.language, S::how_chosen);
    }
    if (joint.node >= 0) {
        *colour = ImVec4(0.55f, 0.75f, 0.95f, 1);
        return tr(app.language, S::how_name);
    }
    const auto p = s.proposals.find(j);
    if (p != s.proposals.end()) {
        *colour = ImVec4(0.95f, 0.80f, 0.35f, 1);
        return fmt(app.language, S::how_proposed, {s.game.model.object_names[static_cast<std::size_t>(p->second)]});
    }
    *colour = ImVec4(0.6f, 0.6f, 0.6f, 1);
    return tr(app.language, S::how_none);
}

void pair(int joint, int node) {
    const std::string& jn = s.rig.joints[static_cast<std::size_t>(joint)].name;
    if (node < 0) {
        s.pairs.erase(jn);
    } else {
        s.pairs[jn] = s.game.model.object_names[static_cast<std::size_t>(node)];
    }
    rebuild();
}

// The picture: both models posed, the bones over them, picking.
void draw_preview(App& app, ImVec2 size) {
    if (!s.preview)
        s.preview = std::make_unique<Preview>(app.device);
    Preview& p = *s.preview;
    p.collect();
    const int w = std::max(64, static_cast<int>(size.x));
    const int h = std::max(64, static_cast<int>(size.y));
    const Mat vp =
        orbit_view_projection(s.target, s.yaw, s.pitch, s.distance, static_cast<float>(w) / static_cast<float>(h));
    p.begin(w, h, vp, orbit_eye_direction(s.yaw, s.pitch));

    std::vector<Mat> node_world, palette;
    pose_game_model(s.game, s.animation >= 0 ? &s.clip : nullptr, s.frame, &node_world, &palette);
    const bool side = s.view == 0 && s.have_model;
    const std::array<float, 3> game_at{side ? -s.offset : 0.0f, 0, 0};
    const std::array<float, 3> mod_at{side ? s.offset : 0.0f, 0, 0};
    auto shift = [](std::array<float, 3> v, const std::array<float, 3>& by) {
        for (int c = 0; c < 3; ++c)
            v[static_cast<std::size_t>(c)] += by[static_cast<std::size_t>(c)];
        return v;
    };

    // the game's model (drawn both sides: the preview does not claim the DS's winding)
    if (s.view == 0 || !s.have_model) {
        for (const auto& mesh : s.game.model.meshes) {
            if (mesh.material_alpha <= 0.0f)
                continue;  // the DS draws nothing of it
            const Texture* t = mesh.texture_name.empty() ? nullptr : game_texture(mesh.texture_name);
            PreviewImage image;
            if (t != nullptr)
                image = {t->rgba.data(), t->width, t->height, mesh.repeat, mesh.mirror};
            const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
            const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
            auto vertex = [&](const assets::NeutralVertex& v) {
                PreviewVertex o;
                std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const float wgt = v.weights[static_cast<std::size_t>(k)];
                    const std::uint32_t jt = v.joints[static_cast<std::size_t>(k)];
                    if (wgt <= 0.0f || jt >= palette.size())
                        continue;
                    const auto q = mat_point(palette[jt], v.position);
                    const auto n = mat_vec(palette[jt], v.normal);
                    for (int c = 0; c < 3; ++c) {
                        pos[static_cast<std::size_t>(c)] += wgt * q[static_cast<std::size_t>(c)];
                        nrm[static_cast<std::size_t>(c)] += wgt * n[static_cast<std::size_t>(c)];
                    }
                }
                o.position = shift(pos, game_at);
                o.normal = nrm;
                o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f,
                            v.color[3] / 255.0f * mesh.material_alpha};
                return o;
            };
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                           vertex(mesh.vertices[mesh.indices[i + 2]]), image, false, {1, 1, 1});
            }
        }
    }

    // the mod's model, by the port's rules (models.cpp draw(), lines 497-516)
    std::vector<Mat> skin, place;
    Mat unskinned{};
    if (s.have_model) {
        pose_rig(s.game, s.rig, node_world, &skin, &unskinned, &place);
        for (const auto& mesh : s.model.model.meshes) {
            const auto ti = s.model_texture.find(mesh.texture_name);
            PreviewImage image;
            float tw = 1.0f, th = 1.0f;
            if (ti != s.model_texture.end()) {
                const auto& tex = s.model.textures[ti->second].second;
                image = {tex.rgba.data(), tex.width, tex.height};
                tw = static_cast<float>(tex.width);
                th = static_cast<float>(tex.height);
            }
            const bool cull = s.cull == Cull::back || (s.cull == Cull::own && !mesh.double_sided);
            auto vertex = [&](const assets::NeutralVertex& v) {
                PreviewVertex o;
                std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                float total = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    const float wgt = v.weights[static_cast<std::size_t>(k)];
                    if (wgt <= 0.0f)
                        continue;
                    const auto found = s.rig.joint_of_palette.find(v.joints[static_cast<std::size_t>(k)]);
                    const Mat& m = found != s.rig.joint_of_palette.end() ? skin[static_cast<std::size_t>(found->second)]
                                                                         : unskinned;
                    const auto q = mat_point(m, v.position);
                    const auto n = mat_vec(m, v.normal);
                    for (int c = 0; c < 3; ++c) {
                        pos[static_cast<std::size_t>(c)] += wgt * q[static_cast<std::size_t>(c)];
                        nrm[static_cast<std::size_t>(c)] += wgt * n[static_cast<std::size_t>(c)];
                    }
                    total += wgt;
                }
                if (total <= 0.0f) {
                    pos = mat_point(unskinned, v.position);
                    nrm = mat_vec(unskinned, v.normal);
                } else if (std::fabs(total - 1.0f) > 1e-4f) {
                    for (auto& c : pos)
                        c /= total;
                }
                o.position = shift(pos, mod_at);
                o.normal = nrm;
                o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                return o;
            };
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                           vertex(mesh.vertices[mesh.indices[i + 2]]), image, cull, {1, 1, 1});
            }
        }
    }

    SDL_GPUTexture* texture = p.finish();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (texture != nullptr) {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture))),
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));
    } else {
        ImGui::Dummy(ImVec2(static_cast<float>(w), static_cast<float>(h)));
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* list = ImGui::GetWindowDrawList();
    // the bones and points stay on the picture, not over the controls
    list->PushClipRect(at, ImVec2(at.x + static_cast<float>(w), at.y + static_cast<float>(h)), true);

    // the bones and the points to pick
    struct Point {
        float x, y;
        bool joint;
        int index;
    };
    std::vector<Point> points;
    auto project = [&](const std::array<float, 3>& q, float* x, float* y) {
        if (!p.project(q, x, y))
            return false;
        *x += at.x;
        *y += at.y;
        return true;
    };
    if (s.bones) {
        for (std::size_t g = 0; g < node_world.size(); ++g) {
            float x0, y0;
            const std::array<float, 3> head{node_world[g][12], node_world[g][13], node_world[g][14]};
            if (!project(shift(head, game_at), &x0, &y0))
                continue;
            const int parent = s.game.parents[g];
            float x1, y1;
            if (parent >= 0 && static_cast<std::size_t>(parent) < node_world.size()) {
                const auto& pw = node_world[static_cast<std::size_t>(parent)];
                if (project(shift({pw[12], pw[13], pw[14]}, game_at), &x1, &y1)) {
                    list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(90, 170, 255, 220), 2.0f);
                }
            }
            points.push_back({x0, y0, false, static_cast<int>(g)});
        }
    }
    if (s.have_model && s.bones) {
        for (std::size_t j = 0; j < s.rig.joints.size() && j < place.size(); ++j) {
            float x0, y0;
            if (!project(shift(mat_point(place[j], s.rig.joints[j].head), mod_at), &x0, &y0))
                continue;
            const int parent = s.rig.joints[j].parent;
            float x1, y1;
            if (parent >= 0 && project(shift(mat_point(place[static_cast<std::size_t>(parent)],
                                                       s.rig.joints[static_cast<std::size_t>(parent)].head),
                                             mod_at),
                                       &x1, &y1)) {
                list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 170, 60, 200), 1.5f);
            }
            points.push_back({x0, y0, true, static_cast<int>(j)});
        }
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int near = -1;
    float best = 10.0f * ImGui::GetStyle().FontScaleDpi;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float d = std::hypot(points[i].x - mouse.x, points[i].y - mouse.y);
        if (hovered && d < best) {
            best = d;
            near = static_cast<int>(i);
        }
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& pt = points[i];
        const bool picked = pt.joint ? pt.index == s.pick_joint : pt.index == s.pick_node;
        const ImU32 c = picked     ? IM_COL32(80, 255, 80, 255)
                        : pt.joint ? IM_COL32(255, 190, 90, 255)
                                   : IM_COL32(120, 190, 255, 255);
        list->AddCircleFilled(ImVec2(pt.x, pt.y), static_cast<int>(i) == near ? 6.0f : 3.0f, c);
    }
    list->PopClipRect();
    if (near >= 0) {
        const Point& pt = points[static_cast<std::size_t>(near)];
        const std::string name = pt.joint ? s.rig.joints[static_cast<std::size_t>(pt.index)].name
                                          : s.game.model.object_names[static_cast<std::size_t>(pt.index)];
        ImGui::SetTooltip("%s", name.c_str());
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && near >= 0) {
        const Point& pt = points[static_cast<std::size_t>(near)];
        if (pt.joint)
            s.pick_joint = pt.index;
        else
            s.pick_node = pt.index;
        if (s.pick_joint >= 0 && s.pick_node >= 0 && s.have_model) {
            if (writable_name(s.rig.joints[static_cast<std::size_t>(s.pick_joint)].name) &&
                writable_name(s.game.model.object_names[static_cast<std::size_t>(s.pick_node)])) {
                pair(s.pick_joint, s.pick_node);
            }
            s.pick_joint = s.pick_node = -1;
        }
    } else if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        s.yaw -= d.x * 0.4f;
        s.pitch = std::clamp(s.pitch + d.y * 0.4f, -85.0f, 85.0f);
    }
    orbit_move(s.target, s.yaw, s.pitch, s.distance, static_cast<float>(h), hovered);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        s.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
    }
}

// Writes the model, its companions and bones.txt into the mod.
void save(App& app, const Item& item) {
    const Mod* m = app.current_mod();
    if (m == nullptr || !s.have_model)
        return;
    const std::string ext = lower(u8(s.source.extension()));
    const fs::path destination = m->dir / path_u8(replacement_path(item, ext));
    const fs::path folder = destination.parent_path();
    int written = 0;
    // companions first, so the model checks against them
    std::vector<std::string> not_found;
    for (const auto& c : model_companions(s.source, &not_found)) {
        const fs::path to = folder / path_u8(c.relative);
        std::error_code ec;
        if (fs::exists(to, ec) && fs::equivalent(to, c.source, ec)) {
            ++written;
            continue;
        }
        const WriteResult r =
            write_into_mod(m->dir, to, [&](const fs::path& t) { return copy_bytes(c.source, t); }, nullptr);
        if (!r.ok) {
            app.tell(r.locked ? tr(app.language, S::save_locked) : fmt(app.language, S::save_failed, {r.why}), false);
            return;
        }
        ++written;
    }
    const fs::path source = s.source;
    std::error_code ec;
    if (!(fs::exists(destination, ec) && fs::equivalent(destination, source, ec))) {
        const WriteResult r = write_into_mod(
            m->dir, destination, [&](const fs::path& t) { return copy_bytes(source, t); },
            [&](const fs::path& t) {
                assets::ImportedModel check;
                std::string why;
                return import_model(t, &check, &why) ? std::string() : why;
            });
        if (!r.ok) {
            app.tell(r.locked ? tr(app.language, S::save_locked) : fmt(app.language, S::save_failed, {r.why}), false);
            return;
        }
    }
    ++written;
    // bones.txt beside it
    BonesFile b;
    b.scale = s.scale;
    b.cull = s.cull;
    b.comments = s.comments;
    for (const auto& [joint, node] : s.pairs) {
        if (writable_name(joint) && writable_name(node))
            b.pairs.emplace_back(joint, node);
    }
    fs::path bones_path = destination;
    bones_path.replace_extension(".bones.txt");
    const std::string text = bones_text(b);
    const WriteResult r = write_into_mod(
        m->dir, bones_path,
        [&](const fs::path& t) {
            std::ofstream out(t, std::ios::binary);
            out << text;
            return out ? std::string() : std::string("cannot write");
        },
        nullptr);
    if (!r.ok) {
        app.tell(fmt(app.language, S::save_failed, {r.why}), false);
        return;
    }
    ++written;
    // a model of another kind of file for the same name in this mod would
    // come first or stay behind (the port takes .gltf, .glb, .fbx in order)
    for (const auto& f : files_in_mod(m->dir, item)) {
        if (!fs::equivalent(f, destination, ec) && !fs::equivalent(f, bones_path, ec))
            fs::remove(f, ec);
    }
    model_offer(app, item, destination);  // what the mod holds now
    app.tell(fmt(app.language, S::model_saved,
                 {u8(destination.lexically_relative(app.settings.root)), std::to_string(written)}) +
                 "  " + tr(app.language, S::restart_note),
             true);
}

// The clip chosen for the shown animation: sampled onto the model's nodes and
// put in the game's file in memory, then read back as the port reads it --
// what the preview shows is what the game will read.
void clip_offer_now(const fs::path& file) {
    if (s.animation < 0)
        return;
    const GameAnimation& a = s.game.animations[static_cast<std::size_t>(s.animation)];
    s.clip_for = s.animation;
    s.clip_file = file;
    s.clip_error.clear();
    s.show_new = false;
    std::string why;
    if (!load_game_animation(nitrofs(), a, &s.game_animation, &why)) {
        s.clip_error = why;
        return;
    }
    s.clip = s.game_animation;
    if (!load_clip(file, s.game.model.object_names, animation_pose(s.game, s.game_animation), &s.new_clip, &why,
                   export_animation_name(s.game, a))) {
        s.clip_error = why;
        return;
    }
    std::vector<bool> keep(s.new_clip.moved.size());
    for (std::size_t n = 0; n < keep.size(); ++n)
        keep[n] = !s.new_clip.moved[n];
    std::ifstream in(nitrofs() / path_u8(label_path(a.label)), std::ios::binary);
    const std::vector<std::uint8_t> original{std::istreambuf_iterator<char>(in), {}};
    std::vector<std::uint8_t> rebuilt;
    std::size_t at = 0;
    if (!rebuild_with_animation(original, original, a.label, a.offset, a.index, s.new_clip.pose, keep, &rebuilt, &at,
                                &why) ||
        !read_animation(rebuilt, a.label, at, a.index, &s.new_animation, &why)) {
        s.clip_error = why;
        return;
    }
    s.not_plain = nodes_not_plain(s.game);
    s.show_new = true;
    s.clip = s.new_animation;
    s.frame = 0.0f;
    s.playing = true;
}

void draw_clip_panel(App& app) {
    const GameAnimation& a = s.game.animations[static_cast<std::size_t>(s.clip_for)];
    ImGui::SeparatorText(fmt(app.language, S::anim_replacing, {a.name, u8(s.clip_file.filename())}).c_str());
    if (!s.clip_error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s",
                           fmt(app.language, S::anim_clip_error, {s.clip_error}).c_str());
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app.language, S::anim_preview));
    same_line_if_room(check_width(tr(app.language, S::anim_preview_game)));
    if (ImGui::RadioButton(tr(app.language, S::anim_preview_game), !s.show_new)) {
        s.show_new = false;
        s.clip = s.game_animation;
    }
    same_line_if_room(check_width(tr(app.language, S::anim_preview_new)));
    if (ImGui::RadioButton(tr(app.language, S::anim_preview_new), s.show_new)) {
        s.show_new = true;
        s.clip = s.new_animation;
    }
    if (s.new_clip.animations_in_file > 1) {
        ImGui::TextWrapped("%s", fmt(app.language, S::anim_taken_from,
                                     {s.new_clip.name, std::to_string(s.new_clip.animations_in_file)})
                                     .c_str());
    }
    ImGui::TextWrapped(
        "%s", fmt(app.language, S::anim_moves,
                  {std::to_string(s.new_clip.animated.size()), std::to_string(s.game.model.object_names.size())})
                  .c_str());
    if (!s.new_clip.left_alone.empty() && ImGui::TreeNode(tr(app.language, S::anim_left_alone))) {
        std::string list;
        for (const auto& n : s.new_clip.left_alone)
            list += (list.empty() ? "" : ", ") + n;
        ImGui::TextWrapped("%s", list.c_str());
        ImGui::TreePop();
    }
    ImGui::TextWrapped("%s", fmt(app.language, S::anim_frames, {std::to_string(a.frames)}).c_str());
    if (!s.not_plain.empty()) {
        std::string list;
        for (const auto& n : s.not_plain)
            list += (list.empty() ? "" : ", ") + n;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.35f, 1));
        ImGui::TextWrapped("%s", fmt(app.language, S::anim_not_plain, {list}).c_str());
        ImGui::PopStyleColor();
    }

    const Mod* m = app.current_mod();
    if (m == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(app.language, S::choose_mod_first));
        return;
    }
    const std::string game_path = label_path(a.label);
    std::error_code ec;
    if (fs::is_regular_file(m->dir / "files" / path_u8(game_path), ec)) {
        ImGui::TextWrapped("%s", fmt(app.language, S::anim_on_copy, {"files/" + game_path}).c_str());
    }
    Item whole;
    whole.kind = Kind::text_file;
    whole.key = game_path;
    const auto held = holders(app.mods_root, whole);
    if (!held.empty() && held.front().mod != m->name) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s",
                           fmt(app.language, S::anim_other_mod, {held.front().mod, game_path}).c_str());
    }
    if (ImGui::Button(tr(app.language, S::anim_save))) {
        AnimationReplacement r;
        std::string why;
        if (replace_animation_in_mod(nitrofs(), m->dir, s.game, a, s.clip_file, &r, &why)) {
            app.tell(fmt(app.language, S::anim_saved, {u8(r.written.lexically_relative(app.settings.root))}) + "  " +
                         tr(app.language, S::restart_note),
                     true);
        } else {
            app.tell(fmt(app.language, S::save_failed, {why}), false);
        }
    }
}

}  // namespace

void clip_offer(App& app, const fs::path& file) {
    if (s.item != app.selected)
        return;
    clip_offer_now(file);
}

void choose_clip(App& app) {
    static const SDL_DialogFileFilter clips[] = {{"glTF, glb", "gltf;glb"}};
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            if (files == nullptr || files[0] == nullptr)
                return;
            App* a = static_cast<App*>(user);
            std::lock_guard lock(a->dialog_mutex);
            a->dialog_clips.emplace_back(files[0]);
        },
        &app, app.window, clips, 1, nullptr, false);
}

void model_offer(App& app, const Item& item, const fs::path& file) {
    (void)item;
    // what the modder set on the model being replaced -- its joints' nodes, size and culling --
    // carried over to the new one wherever its joints are named alike
    const bool carry = s.item == app.selected && s.have_model && !s.pairs.empty();
    const std::map<std::string, std::string> before = carry ? s.pairs : std::map<std::string, std::string>{};
    const float before_scale = s.scale;
    const Cull before_cull = s.cull;
    if (s.item != app.selected)
        open_item(app, app.selected);
    s.source = file;
    s.have_model = false;
    s.model_error.clear();
    s.model_texture.clear();
    s.not_found.clear();
    s.pairs.clear();
    s.comments.clear();
    s.scale = 1.0f;
    s.cull = Cull::own;
    if (!model_extension(lower(u8(file.extension())))) {
        s.model_error = fmt(app.language, S::mod_model_error, {u8(file.filename())});
        return;
    }
    std::string why;
    if (!import_model(file, &s.model, &why)) {
        s.model_error = fmt(app.language, S::mod_model_error, {why});
        return;
    }
    for (std::size_t i = 0; i < s.model.textures.size(); ++i)
        s.model_texture[s.model.textures[i].first] = i;
    model_companions(file, &s.not_found);
    // its bones.txt, when it has one
    fs::path bones = file;
    bones.replace_extension(".bones.txt");
    std::error_code ec;
    const bool own_bones = fs::is_regular_file(bones, ec);
    if (own_bones) {
        const BonesFile b = read_bones_file(bones);
        for (const auto& [joint, node] : b.pairs)
            s.pairs[joint] = node;
        s.scale = b.scale;
        s.cull = b.cull;
        s.comments = b.comments;
    }
    if (carry) {
        // the file's own bones.txt names its joints first; the rest kept from before where the new
        // model has a joint of that name, the others said
        const CarriedPairs c = carry_pairs(before, s.model, &s.pairs);
        const int kept = c.kept;
        const int lost_count = static_cast<int>(c.lost.size());
        std::string lost;
        for (const auto& j : c.lost)
            lost += (lost.empty() ? "" : ", ") + j;
        if (!own_bones) {
            s.scale = before_scale;
            s.cull = before_cull;
        }
        if (lost_count == 0 && kept > 0) {
            app.tell(fmt(app.language, S::bones_kept, {std::to_string(kept)}), true);
        } else if (lost_count > 0) {
            app.tell(fmt(app.language, S::bones_kept_some, {std::to_string(kept), std::to_string(lost_count), lost}),
                     false);
        }
    }
    s.have_model = true;
    rebuild();
}

void model_export_to(App& app, const fs::path& file) {
    if (!s.game_ok)
        return;
    fs::path out = file;
    if (lower(u8(out.extension())) != ".gltf")
        out += ".gltf";
    ModelExport done;
    std::string why;
    if (!export_game_model(nitrofs(), resolve(app.settings, app.settings.graphics_dump), s.game, s.export_animations,
                           out, &done, &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    if (done.animations > 0) {
        app.tell(fmt(app.language, S::model_exported_animations,
                     {u8(out), std::to_string(done.files), std::to_string(done.animations)}),
                 true);
    } else {
        app.tell(fmt(app.language, S::model_exported, {u8(out), std::to_string(done.files)}), true);
    }
}

void model_view_shutdown() {
    s = Session{};
}

void draw_model_view(App& app, const Item& item) {
    if (s.item != app.selected || s.mod != (app.current_mod() != nullptr ? app.current_mod()->name : std::string())) {
        open_item(app, app.selected);
    }
    if (!s.game_ok) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s",
                           fmt(app.language, S::game_model_error, {s.game_error}).c_str());
        return;
    }

    // the controls above the picture
    if (s.animation >= 0 && s.playing) {
        s.frame += ImGui::GetIO().DeltaTime * 30.0f;
        if (s.clip.frame_count > 0)
            s.frame = std::fmod(s.frame, static_cast<float>(s.clip.frame_count));
    }
    ImGui::SetNextItemWidth(260.0f * ImGui::GetStyle().FontScaleDpi);
    const std::string current = s.animation < 0 ? tr(app.language, S::anim_rest)
                                                : s.game.animations[static_cast<std::size_t>(s.animation)].name;
    if (ImGui::BeginCombo(tr(app.language, S::animation), current.c_str())) {
        if (ImGui::Selectable(tr(app.language, S::anim_rest), s.animation < 0)) {
            s.animation = -1;
            s.playing = false;
            s.clip_for = -1;
            frame_camera();
        }
        for (std::size_t i = 0; i < s.game.animations.size(); ++i) {
            const auto& a = s.game.animations[i];
            // another model's animations with as many bones, said apart: they pull this one apart
            if (!a.fits && (i == 0 || s.game.animations[i - 1].fits)) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", tr(app.language, S::anim_others));
            }
            const std::string label =
                a.name + "  (" + a.label + ", " + std::to_string(a.frames) + ")##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == s.animation)) {
                std::string why;
                if (load_game_animation(nitrofs(), a, &s.clip, &why)) {
                    s.animation = static_cast<int>(i);
                    s.frame = 0.0f;
                    s.playing = true;
                    s.clip_for = -1;
                    frame_camera();
                } else {
                    app.tell(why, false);
                }
            }
        }
        ImGui::EndCombo();
    }
    if (s.game.animations.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr(app.language, S::animations_none));
    }
    if (s.animation >= 0) {
        same_line_if_room(button_width(s.playing ? tr(app.language, S::stop) : tr(app.language, S::play)));
        if (ImGui::Button(s.playing ? tr(app.language, S::stop) : tr(app.language, S::play)))
            s.playing = !s.playing;
        same_line_if_room(220.0f * ImGui::GetStyle().FontScaleDpi);
        ImGui::SetNextItemWidth(220.0f * ImGui::GetStyle().FontScaleDpi);
        ImGui::SliderFloat("##frame", &s.frame, 0.0f, static_cast<float>(std::max<int>(1, s.clip.frame_count)), "%.0f");
        // on this line when it fits
        const char* replace = tr(app.language, S::anim_replace);
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(replace).x + ImGui::GetStyle().FramePadding.x * 2) {
            ImGui::NewLine();
        }
        if (ImGui::Button(replace))
            choose_clip(app);
    }
    if (s.animation >= 0 && s.clip_for == s.animation)
        draw_clip_panel(app);
    ImGui::SetNextItemWidth(200.0f * ImGui::GetStyle().FontScaleDpi);
    const char* views[] = {tr(app.language, S::view_side), tr(app.language, S::view_over)};
    ImGui::Combo(tr(app.language, S::view_mode), &s.view, views, 2);
    same_line_if_room(check_width(tr(app.language, S::show_bones)));
    ImGui::Checkbox(tr(app.language, S::show_bones), &s.bones);
    ImGui::SameLine();
    // asked first whether its animations go with it, when it has some
    const auto choose_export_file = [&app, &item]() {
        choose_save_file(app, export_name(item), "glTF", "gltf", app.dialog_model_exports);
    };
    const int animations = exported_animation_count(s.game);
    if (ImGui::Button(tr(app.language, S::model_export))) {
        if (animations > 0) {
            ImGui::OpenPopup("###export_animations");
        } else {
            s.export_animations = false;
            choose_export_file();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr(app.language, S::model_export_hint));
    if (ImGui::BeginPopupModal((std::string(tr(app.language, S::model_export)) + "###export_animations").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            fmt(app.language, S::model_export_ask, {s.game.name, std::to_string(animations)}).c_str());
        if (ImGui::Button(tr(app.language, S::model_export_with))) {
            s.export_animations = true;
            ImGui::CloseCurrentPopup();
            choose_export_file();
        }
        same_line_if_room(button_width(tr(app.language, S::model_export_only)));
        if (ImGui::Button(tr(app.language, S::model_export_only))) {
            s.export_animations = false;
            ImGui::CloseCurrentPopup();
            choose_export_file();
        }
        same_line_if_room(button_width(tr(app.language, S::cancel)));
        if (ImGui::Button(tr(app.language, S::cancel)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (s.have_model) {
        same_line_if_room(160.0f * ImGui::GetStyle().FontScaleDpi + check_width(tr(app.language, S::scale)));
        ImGui::SetNextItemWidth(160.0f * ImGui::GetStyle().FontScaleDpi);
        if (ImGui::DragFloat(tr(app.language, S::scale), &s.scale, 0.005f, 0.01f, 100.0f, "%.3f")) {
            s.scale = std::max(0.001f, s.scale);
            rebuild();
        }
        bool any_double = false;
        for (const auto& mesh : s.model.model.meshes)
            any_double = any_double || mesh.double_sided;
        const std::string own =
            fmt(app.language, S::cull_own, {tr(app.language, any_double ? S::both_sides : S::front_only)});
        const char* culls[] = {own.c_str(), tr(app.language, S::cull_back), tr(app.language, S::cull_none)};
        int cull = static_cast<int>(s.cull);
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(own.c_str()).x + ImGui::GetFrameHeight() * 2);
        if (ImGui::Combo(tr(app.language, S::cull), &cull, culls, 3))
            s.cull = static_cast<Cull>(cull);
    }

    // the picture
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    draw_preview(app, ImVec2(avail.x, std::max(200.0f, avail.y * 0.5f)));
    ImGui::TextDisabled("%s", tr(app.language, S::pick_hint));

    // where the model comes from
    if (!s.have_model) {
        if (app.current_mod() == nullptr) {
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(app.language, S::choose_mod_first));
            return;
        }
        ImGui::TextWrapped("%s", tr(app.language, S::mod_model_none));
        if (ImGui::Button(tr(app.language, S::choose_file)))
            choose_file(app, item);
        if (!s.model_error.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", s.model_error.c_str());
        return;
    }
    if (ImGui::SmallButton(tr(app.language, S::choose_file)))
        choose_file(app, item);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", u8(s.source).c_str());
    for (const auto& [name, why] : s.model.missing_textures) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", fmt(app.language, S::missing_texture, {name}).c_str());
    }
    for (const auto& node : s.rig.missing_nodes) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", fmt(app.language, S::missing_node, {node}).c_str());
    }
    if (s.model.joints.empty())
        ImGui::TextDisabled("%s", tr(app.language, S::no_joints));

    // save
    const Mod* m = app.current_mod();
    if (m != nullptr) {
        const auto existing = files_in_mod(m->dir, item);
        bool same = false;
        std::error_code ec;
        for (const auto& f : existing)
            same = same || fs::equivalent(f, s.source, ec);
        if (ImGui::Button(tr(app.language, S::save))) {
            if (existing.empty() || same)
                save(app, item);
            else
                ImGui::OpenPopup("###overwrite_model");
        }
        if (ImGui::BeginPopupModal((std::string(tr(app.language, S::replace)) + "###overwrite_model").c_str(), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(fmt(app.language, S::overwrite_confirm,
                                       {existing.empty() ? std::string() : u8(existing.front().filename())})
                                       .c_str());
            if (ImGui::Button(tr(app.language, S::replace))) {
                save(app, item);
                ImGui::CloseCurrentPopup();
            }
            same_line_if_room(button_width(tr(app.language, S::cancel)));
            if (ImGui::Button(tr(app.language, S::cancel)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // the joints
    int following = 0;
    for (const auto& j : s.rig.joints)
        following += j.node >= 0 ? 1 : 0;
    ImGui::SeparatorText(
        fmt(app.language, S::pairs_title,
            {std::to_string(following), std::to_string(s.rig.joints.size() - static_cast<std::size_t>(following))})
            .c_str());
    if (!s.proposals.empty() && ImGui::Button(tr(app.language, S::accept_all))) {
        for (const auto& [j, node] : s.proposals) {
            const auto& jn = s.rig.joints[static_cast<std::size_t>(j)].name;
            const auto& nn = s.game.model.object_names[static_cast<std::size_t>(node)];
            if (writable_name(jn) && writable_name(nn))
                s.pairs[jn] = nn;
        }
        rebuild();
    }
    if (s.pick_joint >= 0 || s.pick_node >= 0) {
        ImGui::SameLine();
        const std::string picked = s.pick_joint >= 0 ? s.rig.joints[static_cast<std::size_t>(s.pick_joint)].name
                                                     : s.game.model.object_names[static_cast<std::size_t>(s.pick_node)];
        ImGui::TextUnformatted(fmt(app.language, S::picked, {picked}).c_str());
    }
    if (ImGui::BeginTable("##joints", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr(app.language, S::col_joint));
        ImGui::TableSetupColumn(tr(app.language, S::col_node));
        ImGui::TableSetupColumn(tr(app.language, S::col_how));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        int change_joint = -1, change_node = -2;
        for (std::size_t j = 0; j < s.rig.joints.size(); ++j) {
            const RigJoint& joint = s.rig.joints[j];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableSetColumnIndex(0);
            const bool picked = static_cast<int>(j) == s.pick_joint;
            if (ImGui::Selectable(joint.name.c_str(), picked,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                s.pick_joint = static_cast<int>(j);
            }
            ImGui::TableSetColumnIndex(1);
            const bool writable = writable_name(joint.name);
            if (!writable) {
                ImGui::TextDisabled("%s", tr(app.language, S::name_unwritable));
            } else {
                ImGui::SetNextItemWidth(-1);
                const char* shown = joint.node >= 0
                                        ? s.game.model.object_names[static_cast<std::size_t>(joint.node)].c_str()
                                        : tr(app.language, S::node_none);
                if (ImGui::BeginCombo("##node", shown)) {
                    if (ImGui::Selectable(tr(app.language, S::node_none), joint.node < 0)) {
                        change_joint = static_cast<int>(j);
                        change_node = -1;
                    }
                    for (std::size_t g = 0; g < s.game.model.object_names.size(); ++g) {
                        const auto& nn = s.game.model.object_names[g];
                        if (!writable_name(nn))
                            continue;
                        if (ImGui::Selectable(nn.c_str(), joint.node == static_cast<int>(g))) {
                            change_joint = static_cast<int>(j);
                            change_node = static_cast<int>(g);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::TableSetColumnIndex(2);
            ImVec4 colour;
            const std::string how = pair_how(app, static_cast<int>(j), &colour);
            ImGui::TextColored(colour, "%s", how.c_str());
            ImGui::TableSetColumnIndex(3);
            const auto proposal = s.proposals.find(static_cast<int>(j));
            if (proposal != s.proposals.end() && writable &&
                writable_name(s.game.model.object_names[static_cast<std::size_t>(proposal->second)]) &&
                ImGui::SmallButton(tr(app.language, S::accept))) {
                change_joint = static_cast<int>(j);
                change_node = proposal->second;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (change_joint >= 0 && change_node >= -1)
            pair(change_joint, change_node);
    }
}

}  // namespace studio
