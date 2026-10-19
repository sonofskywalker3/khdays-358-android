#include "core/rooms/room_export.h"

#include "core/models/bones.h"
#include "core/models/game_model.h"
#include "core/files/paths.h"

#include "khdays/assets/gltf_export.h"

extern "C" {
#include "render/png_io.h"
}

#include <fstream>
#include <map>

namespace studio {

namespace {

namespace assets = khdays::assets;

constexpr float kFx = 1.0f / 4096.0f;

}  // namespace

std::vector<std::array<float, 3>> room_points(const fs::path& nitrofs, const fs::path& graphics_dump, const World& w,
                                              int room) {
    std::vector<std::array<float, 3>> out;
    if (room < 0 || static_cast<std::size_t>(room) >= w.rooms.size())
        return out;
    std::error_code ec;
    for (const auto& o : w.rooms[static_cast<std::size_t>(room)].objects) {
        const std::string label = world_path(w.code) + "#" + std::to_string(o.member);
        std::string key;
        for (const auto& e : fs::directory_iterator(graphics_dump / path_u8(label) / "mdl", ec)) {
            if (lower(u8(e.path().extension())) == ".gltf")
                key = label + "/mdl/" + u8(e.path().stem());
        }
        GameModel g;
        std::string problem;
        if (key.empty() || !load_game_model(nitrofs, graphics_dump, key, &g, &problem))
            continue;
        std::vector<Mat> node_world, palette;
        pose_game_model(g, nullptr, 0.0f, &node_world, &palette);
        for (const auto& mesh : g.model.meshes) {
            for (const auto& v : mesh.vertices) {
                std::array<float, 3> pos{0, 0, 0};
                for (std::size_t k = 0; k < 4; ++k) {
                    if (v.weights[k] <= 0.0f || v.joints[k] >= palette.size())
                        continue;
                    const auto q = mat_point(palette[v.joints[k]], v.position);
                    for (std::size_t c = 0; c < 3; ++c)
                        pos[c] += v.weights[k] * q[c];
                }
                out.push_back(pos);
            }
        }
    }
    return out;
}

bool export_room(const fs::path& nitrofs, const fs::path& graphics_dump, const World& w, int room, const fs::path& out,
                 std::vector<std::string>* notes, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    if (room < 0 || static_cast<std::size_t>(room) >= w.rooms.size())
        return fail("no room " + std::to_string(room));
    const WorldRoom& r = w.rooms[static_cast<std::size_t>(room)];
    {
        std::error_code made;
        if (!out.parent_path().empty())
            fs::create_directories(out.parent_path(), made);
        if (made)
            return fail("cannot make the folder " + u8(out.parent_path()) + ": " + made.message());
    }
    // one model, one bone: every mesh already in the room's space
    assets::NeutralModel model;
    model.name = "wd_" + w.code + "_room" + std::to_string(room);
    model.palette = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    model.object_matrices = model.palette;
    model.object_names = {"room"};
    model.object_parents = {-1};
    model.palette_bones = {{{0u, 1.0f}}};
    std::map<std::string, assets::GltfTextureRef> textures;
    std::error_code ec;
    for (const auto& o : r.objects) {
        const std::string label = world_path(w.code) + "#" + std::to_string(o.member);
        const fs::path dir = graphics_dump / path_u8(label) / "mdl";
        std::string key;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (lower(u8(e.path().extension())) == ".gltf")
                key = label + "/mdl/" + u8(e.path().stem());
        }
        GameModel g;
        std::string problem;
        if (key.empty() || !load_game_model(nitrofs, graphics_dump, key, &g, &problem)) {
            if (notes != nullptr)
                notes->push_back(label + ": " + (key.empty() ? std::string("not in the graphics dump") : problem));
            continue;
        }
        std::vector<Mat> node_world, palette;
        pose_game_model(g, nullptr, 0.0f, &node_world, &palette);
        for (const auto& mesh : g.model.meshes) {
            assets::NeutralMesh m = mesh;
            m.name = g.name + "_" + mesh.name;
            for (auto& v : m.vertices) {
                std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                for (std::size_t k = 0; k < 4; ++k) {
                    if (v.weights[k] <= 0.0f || v.joints[k] >= palette.size())
                        continue;
                    const auto q = mat_point(palette[v.joints[k]], v.position);
                    const auto n = mat_vec(palette[v.joints[k]], v.normal);
                    for (std::size_t c = 0; c < 3; ++c)
                        pos[c] += v.weights[k] * q[c], nrm[c] += v.weights[k] * n[c];
                }
                v.position = pos;
                v.normal = nrm;
                v.joints = {0, 0, 0, 0};
                v.weights = {1, 0, 0, 0};
            }
            model.meshes.push_back(std::move(m));
            const auto t = g.textures.find(mesh.texture_name);
            if (!mesh.texture_name.empty() && t != g.textures.end() && textures.count(mesh.texture_name) == 0) {
                int tw = 0, th = 0;
                const char* error = nullptr;
                std::uint8_t* px = khdays_png_read(u8(t->second).c_str(), &tw, &th, &error);
                if (px != nullptr) {
                    const bool translucent =
                        assets::texels_translucent(px, static_cast<std::size_t>(tw) * static_cast<std::size_t>(th));
                    khdays_png_free(px);
                    const fs::path copy = out.parent_path() / t->second.filename();
                    fs::copy_file(t->second, copy, fs::copy_options::overwrite_existing, ec);
                    textures[mesh.texture_name] = {u8(t->second.filename()), tw, th, translucent};
                }
            }
        }
    }
    // the collision: a mesh per kind and set of names
    const auto& data = w.archive.entries[static_cast<std::size_t>(r.collision_member)].data;
    RoomCollision c;
    if (!data.empty()) {
        std::string problem;
        if (!parse_collision(data, &c, &problem))
            return fail(problem);
    }
    static const char* const kinds[3] = {"ground", "wall", "ceiling"};
    std::map<std::string, std::size_t> mesh_of;
    for (int l = 0; l < 3; ++l) {
        for (const auto& f : c.faces[static_cast<std::size_t>(l)]) {
            if (f.corners < 3)
                continue;
            std::string name = kinds[l];
            for (std::size_t p = 0; p < 4; ++p) {
                const auto s = f.slots[p];
                if (s == 0xff)
                    continue;
                if (!slot_names(l, p))
                    name += " kind" + std::to_string(s);
                else if (s < c.named.size())
                    name += " " + c.named[s].text();
            }
            auto found = mesh_of.find(name);
            if (found == mesh_of.end()) {
                // a one-texel picture of its colour, so the material carries the name and shows
                std::uint8_t px[4] = {90, 220, 90, 255};
                if (l == kWall)
                    px[0] = 255, px[1] = 140, px[2] = 40;
                if (l == kCeiling)
                    px[0] = 90, px[1] = 140, px[2] = 255;
                if (name.find(" gate") != std::string::npos)
                    px[0] = 255, px[1] = 220, px[2] = 40;
                if (name.find(" col_wall") != std::string::npos)
                    px[0] = 230, px[1] = 80, px[2] = 230;
                std::string file = "collision-" + name + ".png";
                for (auto& ch : file)
                    ch = ch == ' ' ? '_' : ch;
                if (const char* e = khdays_png_write(u8(out.parent_path() / path_u8(file)).c_str(), px, 1, 1)) {
                    return fail(std::string("could not write ") + file + ": " + e);
                }
                textures[name] = {file, 1, 1};
                assets::NeutralMesh m;
                m.name = "collision " + name;
                m.texture_name = name;
                m.material_name = name;
                m.double_sided = true;
                found = mesh_of.emplace(name, model.meshes.size()).first;
                model.meshes.push_back(std::move(m));
            }
            assets::NeutralMesh& m = model.meshes[found->second];
            const auto first = static_cast<std::uint32_t>(m.vertices.size());
            for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                assets::NeutralVertex nv;
                nv.position = {static_cast<float>(f.v[v][0]) * kFx, static_cast<float>(f.v[v][1]) * kFx,
                               static_cast<float>(f.v[v][2]) * kFx};
                m.vertices.push_back(nv);
            }
            m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
            if (f.corners == 4)
                m.indices.insert(m.indices.end(), {first, first + 2, first + 3});
        }
    }
    const std::string text = assets::to_gltf(model, textures);
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    file << text;
    if (!file)
        return fail("could not write " + u8(out));
    return true;
}

}  // namespace studio
