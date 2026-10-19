// A mod model's two formats give the same neutral model: a small skinned quad written by this test as glTF (metres, Y up, its
// PNG embedded) and as ASCII FBX (centimetres, Z up, its PNG beside it under
// a stored path that does not exist), imported and compared corner by
// corner; the .glb and JPEG forms of glTF; a vertex with five weights; a
// texture that is missing; a room with its own animation, a sky and moving
// textures written by scene_to_gltf and read back. Every file is made here;
// nothing of a game.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"
#include "khdays/assets/gltf_export.h"

namespace {

namespace fs = std::filesystem;
using khdays::assets::ImportedModel;

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "model_import: failed: " << what << "\n";
        ++failures;
    }
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4F;
}

// A 2x2 RGBA PNG (red, green, blue, white).
const std::uint8_t kPng[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49,
                             0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06,
                             0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44,
                             0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x04, 0x59, 0x18,
                             0xc1, 0x34, 0x03, 0x03, 0x00, 0x3e, 0xff, 0x06, 0x00, 0xfa, 0x5c, 0x13, 0xb0,
                             0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

// The same picture as a 2x2 baseline JPEG.
const std::uint8_t kJpeg[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05,
    0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06, 0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0b, 0x08, 0x09, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x06, 0x08, 0x0b, 0x0c, 0x0b, 0x0a, 0x0c, 0x09, 0x0a, 0x0a, 0x0a, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x05, 0x03, 0x03, 0x05, 0x0a, 0x07, 0x06, 0x07, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x02, 0x00, 0x02,
    0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
    0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1,
    0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2,
    0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3,
    0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4,
    0x00, 0x1f, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00, 0x02,
    0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11,
    0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1,
    0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5,
    0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00,
    0xfd, 0x60, 0xff, 0x00, 0x82, 0x70, 0x7c, 0x08, 0xf8, 0x1f, 0xe2, 0x5f, 0xf8, 0x27, 0x8f, 0xc0, 0x5f, 0x11, 0x78,
    0x8b, 0xe0, 0xd7, 0x85, 0x35, 0x0d, 0x43, 0x50, 0xf8, 0x2f, 0xe1, 0x6b, 0x9b, 0xfb, 0xfb, 0xdf, 0x0e, 0xdb, 0x4b,
    0x35, 0xcc, 0xcf, 0xa4, 0xda, 0xb3, 0xc9, 0x23, 0xb2, 0x16, 0x77, 0x66, 0x25, 0x8b, 0x12, 0x49, 0x24, 0x93, 0x45,
    0x14, 0x57, 0xf3, 0x66, 0x7b, 0xff, 0x00, 0x23, 0xbc, 0x57, 0xfd, 0x7c, 0x9f, 0xfe, 0x94, 0xcf, 0xe4, 0x4e, 0x25,
    0xff, 0x00, 0x92, 0x8f, 0x1b, 0xff, 0x00, 0x5f, 0x6a, 0x7f, 0xe9, 0x6c, 0xff, 0xd9};

void write_file(const fs::path& path, const void* data, std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void write_text(const fs::path& path, const std::string& text) {
    write_file(path, text.data(), text.size());
}

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static const char* const table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned v = (bytes[i] << 16U) | (bytes[i + 1] << 8U) | bytes[i + 2];
        out += table[(v >> 18U) & 63U];
        out += table[(v >> 12U) & 63U];
        out += table[(v >> 6U) & 63U];
        out += table[v & 63U];
    }
    if (i + 1 == bytes.size()) {
        const unsigned v = bytes[i] << 16U;
        out += table[(v >> 18U) & 63U];
        out += table[(v >> 12U) & 63U];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const unsigned v = (bytes[i] << 16U) | (bytes[i + 1] << 8U);
        out += table[(v >> 18U) & 63U];
        out += table[(v >> 12U) & 63U];
        out += table[(v >> 6U) & 63U];
        out += '=';
    }
    return out;
}

template <typename T> void put(std::vector<std::uint8_t>& out, const T& value) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

// The quad in glTF's frame (metres, Y up): corners, their UVs (V down from the
// top), the joints root (at the origin) and tip (1 m up, root's child), the
// weights; the triangles 0 1 2 and 0 2 3.
constexpr std::array<std::array<float, 3>, 4> kPositions{{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
constexpr std::array<std::array<float, 2>, 4> kUvs{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
constexpr std::array<std::array<std::uint16_t, 4>, 4> kJoints{{{0, 0, 0, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}}};
constexpr std::array<std::array<float, 4>, 4> kWeights{
    {{1, 0, 0, 0}, {1, 0, 0, 0}, {0.75F, 0.25F, 0, 0}, {1, 0, 0, 0}}};
constexpr std::array<std::uint16_t, 6> kIndices{0, 1, 2, 0, 2, 3};

// The glTF's binary buffer; with the PNG after it when `with_image`.
std::vector<std::uint8_t> gltf_buffer(bool with_image) {
    std::vector<std::uint8_t> b;
    for (const auto& p : kPositions)
        for (float v : p)
            put(b, v);  // 0, 48
    for (int i = 0; i < 4; ++i) {
        put(b, 0.0F);
        put(b, 0.0F);
        put(b, 1.0F);
    }  // 48, 48
    for (const auto& t : kUvs)
        for (float v : t)
            put(b, v);  // 96, 32
    for (const auto& j : kJoints)
        for (auto v : j)
            put(b, v);  // 128, 32
    for (const auto& w : kWeights)
        for (float v : w)
            put(b, v);  // 160, 64
    for (auto v : kIndices)
        put(b, v);  // 224, 12
    while (b.size() < 240U)
        b.push_back(0);
    const float root[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float tip[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    for (float v : root)
        put(b, v);  // 240, 128
    for (float v : tip)
        put(b, v);
    if (with_image) {
        b.insert(b.end(), kPng, kPng + sizeof(kPng));  // 368, 77
    }
    return b;
}

std::string gltf_json(const std::string& image, const std::string& buffer, std::size_t buffer_size, bool image_view) {
    std::ostringstream j;
    j << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],)"
      << R"("nodes":[{"name":"quad","mesh":0,"skin":0},{"name":"root","children":[2]},)"
      << R"({"name":"tip","translation":[0,1,0]}],)"
      << R"("meshes":[{"name":"quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,)"
      << R"("JOINTS_0":3,"WEIGHTS_0":4},"indices":5,"material":0}]}],)"
      << R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
      << R"("textures":[{"source":0}],"images":[)" << image << "],"
      << R"("skins":[{"joints":[1,2],"inverseBindMatrices":6}],)"
      << R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},)"
      << R"({"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},)"
      << R"({"bufferView":2,"componentType":5126,"count":4,"type":"VEC2"},)"
      << R"({"bufferView":3,"componentType":5123,"count":4,"type":"VEC4"},)"
      << R"({"bufferView":4,"componentType":5126,"count":4,"type":"VEC4"},)"
      << R"({"bufferView":5,"componentType":5123,"count":6,"type":"SCALAR"},)"
      << R"({"bufferView":6,"componentType":5126,"count":2,"type":"MAT4"}],)"
      << R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":48},)"
      << R"({"buffer":0,"byteOffset":96,"byteLength":32},{"buffer":0,"byteOffset":128,"byteLength":32},)"
      << R"({"buffer":0,"byteOffset":160,"byteLength":64},{"buffer":0,"byteOffset":224,"byteLength":12},)"
      << R"({"buffer":0,"byteOffset":240,"byteLength":128})"
      << (image_view ? R"(,{"buffer":0,"byteOffset":368,"byteLength":77})" : "") << "],"
      << R"("buffers":[{"byteLength":)" << buffer_size << buffer << "}]}";
    return j.str();
}

void write_glb(const fs::path& path) {
    std::string json =
        gltf_json(R"({"name":"tex","bufferView":7,"mimeType":"image/png"})", "", 368 + sizeof(kPng), true);
    while (json.size() % 4U != 0U)
        json += ' ';
    std::vector<std::uint8_t> bin = gltf_buffer(true);
    while (bin.size() % 4U != 0U)
        bin.push_back(0);
    std::vector<std::uint8_t> out;
    put(out, std::uint32_t{0x46546C67});  // glTF
    put(out, std::uint32_t{2});
    put(out, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    put(out, static_cast<std::uint32_t>(json.size()));
    put(out, std::uint32_t{0x4E4F534A});  // JSON
    out.insert(out.end(), json.begin(), json.end());
    put(out, static_cast<std::uint32_t>(bin.size()));
    put(out, std::uint32_t{0x004E4942});  // BIN
    out.insert(out.end(), bin.begin(), bin.end());
    write_file(path, out.data(), out.size());
}

struct Cluster {
    std::string name;
    float link_z_cm;  // the bone's place at bind, up (Z) in centimetres
    std::vector<int> indexes;
    std::vector<float> weights;
};

// An ASCII FBX in centimetres and Z up (front -Y: Blender's), one mesh of
// triangles, its UVs (V up from the bottom), a skin, a material whose
// diffuse texture is stored as `stored_texture`.
std::string fbx_text(const std::vector<std::array<float, 3>>& yup_metres, const std::vector<int>& triangles,
                     const std::vector<std::array<float, 2>>& gltf_uvs, const std::vector<Cluster>& clusters,
                     const std::string& stored_texture) {
    std::ostringstream f;
    const auto list = [&](const auto& values) {
        std::ostringstream s;
        for (std::size_t i = 0; i < values.size(); ++i)
            s << (i ? "," : "") << values[i];
        return s.str();
    };
    std::vector<float> vertices;  // Y up metres -> Z up centimetres: (x, -z, y) * 100
    for (const auto& p : yup_metres) {
        vertices.push_back(p[0] * 100.0F);
        vertices.push_back(-p[2] * 100.0F);
        vertices.push_back(p[1] * 100.0F);
    }
    std::vector<int> polygon;
    std::vector<float> normals;
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        polygon.push_back((i % 3U) == 2U ? -triangles[i] - 1 : triangles[i]);
        normals.insert(normals.end(), {0.0F, -1.0F, 0.0F});  // glTF's +Z
    }
    std::vector<float> uvs;
    for (const auto& uv : gltf_uvs) {
        uvs.push_back(uv[0]);
        uvs.push_back(1.0F - uv[1]);
    }
    f << "; FBX 7.4.0 project file\n"
         "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n\tCreator: \"khdays test\"\n}\n"
         "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
         "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",2\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
         "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
         "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
         "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n\t}\n}\n"
         "Objects:  {\n"
      << "\tGeometry: 100, \"Geometry::quad\", \"Mesh\" {\n"
      << "\t\tVertices: *" << vertices.size() << " {\n\t\t\ta: " << list(vertices) << "\n\t\t}\n"
      << "\t\tPolygonVertexIndex: *" << polygon.size() << " {\n\t\t\ta: " << list(polygon) << "\n\t\t}\n"
      << "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
         "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"Direct\"\n"
      << "\t\t\tNormals: *" << normals.size() << " {\n\t\t\t\ta: " << list(normals) << "\n\t\t\t}\n\t\t}\n"
      << "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n"
         "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
      << "\t\t\tUV: *" << uvs.size() << " {\n\t\t\t\ta: " << list(uvs) << "\n\t\t\t}\n"
      << "\t\t\tUVIndex: *" << triangles.size() << " {\n\t\t\t\ta: " << list(triangles) << "\n\t\t\t}\n\t\t}\n"
      << "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
         "\t\t\tMappingInformationType: \"AllSame\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
         "\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n"
         "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
         "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
         "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
         "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
         "\t\t}\n\t}\n"
         "\tModel: 200, \"Model::quad\", \"Mesh\" {\n\t\tVersion: 232\n\t}\n";
    for (std::size_t c = 0; c < clusters.size(); ++c) {
        const float parent_z = c == 0 ? 0.0F : clusters[c - 1].link_z_cm;
        f << "\tModel: " << 300 + c << ", \"Model::" << clusters[c].name << "\", \"LimbNode\" {\n\t\tVersion: 232\n"
          << "\t\tProperties70:  {\n\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,"
          << clusters[c].link_z_cm - parent_z << "\n\t\t}\n\t}\n";
    }
    f << "\tDeformer: 400, \"Deformer::skin\", \"Skin\" {\n\t\tVersion: 101\n\t\tLink_DeformAcuracy: 50\n\t}\n";
    for (std::size_t c = 0; c < clusters.size(); ++c) {
        f << "\tDeformer: " << 410 + c << ", \"SubDeformer::" << clusters[c].name << "\", \"Cluster\" {\n"
          << "\t\tVersion: 100\n"
          << "\t\tIndexes: *" << clusters[c].indexes.size() << " {\n\t\t\ta: " << list(clusters[c].indexes)
          << "\n\t\t}\n"
          << "\t\tWeights: *" << clusters[c].weights.size() << " {\n\t\t\ta: " << list(clusters[c].weights)
          << "\n\t\t}\n"
          // FBX's Transform: the geometry in the bone at bind (inverse(TransformLink)
          // times the mesh's global, the identity here)
          << "\t\tTransform: *16 {\n\t\t\ta: 1,0,0,0,0,1,0,0,0,0,1,0,0,0," << -clusters[c].link_z_cm << ",1\n\t\t}\n"
          << "\t\tTransformLink: *16 {\n\t\t\ta: 1,0,0,0,0,1,0,0,0,0,1,0,0,0," << clusters[c].link_z_cm
          << ",1\n\t\t}\n\t}\n";
    }
    f << "\tMaterial: 500, \"Material::mat\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n\t}\n"
      << "\tTexture: 600, \"Texture::tex\", \"\" {\n\t\tType: \"TextureVideoClip\"\n\t\tVersion: 202\n"
      << "\t\tTextureName: \"Texture::tex\"\n\t\tFileName: \"C:\\nowhere\\" << stored_texture << "\"\n"
      << "\t\tRelativeFilename: \"..\\gone\\" << stored_texture << "\"\n\t}\n}\n"
      << "Connections:  {\n\tC: \"OO\",200,0\n\tC: \"OO\",100,200\n\tC: \"OO\",400,100\n";
    for (std::size_t c = 0; c < clusters.size(); ++c) {
        f << "\tC: \"OO\"," << 300 + c << "," << (c == 0 ? 0 : 300 + c - 1) << "\n"
          << "\tC: \"OO\"," << 410 + c << ",400\n"
          << "\tC: \"OO\"," << 300 + c << "," << 410 + c << "\n";
    }
    f << "\tC: \"OO\",500,200\n\tC: \"OP\",600,500, \"DiffuseColor\"\n}\n";
    return f.str();
}

bool identity(const std::array<float, 16>& m) {
    for (int i = 0; i < 16; ++i) {
        if (!near(m[static_cast<std::size_t>(i)], (i % 5) == 0 ? 1.0F : 0.0F))
            return false;
    }
    return true;
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "khdays-model-import-test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // glTF, its PNG embedded as a data URI
    const auto buffer = gltf_buffer(false);
    const std::string png_uri = "data:image/png;base64," + base64(std::vector<std::uint8_t>(kPng, kPng + sizeof(kPng)));
    write_text(dir / "quad.gltf", gltf_json(R"({"name":"tex","uri":")" + png_uri + R"("})",
                                            R"(,"uri":"data:application/octet-stream;base64,)" + base64(buffer) + "\"",
                                            buffer.size(), false));
    // FBX, its PNG beside it though stored elsewhere
    const std::vector<std::array<float, 3>> positions(kPositions.begin(), kPositions.end());
    const std::vector<std::array<float, 2>> uvs(kUvs.begin(), kUvs.end());
    const std::vector<int> triangles(kIndices.begin(), kIndices.end());
    write_text(dir / "quad.fbx",
               fbx_text(positions, triangles, uvs,
                        {{"root", 0.0F, {0, 1, 2}, {1, 1, 0.25F}}, {"tip", 100.0F, {2, 3}, {0.75F, 1}}}, "tex.png"));
    write_file(dir / "tex.png", kPng, sizeof(kPng));

    try {
        const ImportedModel gltf = khdays::assets::import_gltf(dir / "quad.gltf");
        const ImportedModel fbx = khdays::assets::import_fbx(dir / "quad.fbx");
        check(gltf.model.meshes.size() == 1 && fbx.model.meshes.size() == 1, "one mesh each");
        check(gltf.missing_textures.empty(), "glTF: its embedded texture read");
        check(fbx.missing_textures.empty(), "FBX: its texture found beside it");
        check(gltf.textures.size() == 1 && gltf.textures[0].second.width == 2, "glTF texture 2x2");
        check(fbx.textures.size() == 1 && fbx.textures[0].second.width == 2, "FBX texture 2x2");
        check(gltf.joints.size() == 2 && fbx.joints.size() == 2, "two joints each");
        // how the material takes the texture's alpha: glTF's default is opaque, FBX does not say
        using AlphaMode = khdays::assets::NeutralMesh::AlphaMode;
        check(gltf.model.meshes[0].alpha_mode == AlphaMode::opaque, "glTF: no alphaMode is opaque");
        check(fbx.model.meshes[0].alpha_mode == AlphaMode::unknown, "FBX: alpha mode unknown");
        {
            // written back as glTF, a mesh that does not say is cut out at half unless its texture's texels
            // or its vertex colours have an alpha of their own; one that says keeps it
            auto mode_of = [&](const khdays::assets::NeutralModel& model, bool translucent) {
                std::map<std::string, khdays::assets::GltfTextureRef> refs;
                for (const auto& mesh : model.meshes)
                    refs[mesh.texture_name] = {"tex.png", 2, 2, translucent};
                const std::string text = khdays::assets::to_gltf(model, refs);
                const auto at = text.find("\"alphaMode\":\"");
                return at == std::string::npos ? std::string()
                                               : text.substr(at + 13, text.find('"', at + 13) - at - 13);
            };
            check(mode_of(fbx.model, false) == "MASK", "export: an FBX mesh with an opaque texture cut out");
            check(mode_of(fbx.model, true) == "BLEND", "export: an FBX mesh with a translucent texture blended");
            khdays::assets::NeutralModel faded = fbx.model;
            faded.meshes[0].vertices[0].color[3] = 13;  // a vertex colour at alpha 0.05
            check(mode_of(faded, false) == "BLEND", "export: an FBX mesh with vertex alpha blended");
            khdays::assets::NeutralModel said = faded;
            said.meshes[0].alpha_mode = AlphaMode::mask;
            check(mode_of(said, true) == "MASK", "export: a mesh that says MASK keeps it");
            const std::uint8_t solid[8] = {9, 9, 9, 255, 9, 9, 9, 0}, soft[8] = {9, 9, 9, 255, 9, 9, 9, 128};
            check(!khdays::assets::texels_translucent(solid, 2), "texels: opaque and transparent only");
            check(khdays::assets::texels_translucent(soft, 2), "texels: one at half");
        }
        {
            std::string masked = gltf_json(R"({"name":"tex","uri":")" + png_uri + R"("})",
                                           R"(,"uri":"data:application/octet-stream;base64,)" + base64(buffer) + "\"",
                                           buffer.size(), false);
            const std::string plain = R"("materials":[{"pbrMetallicRoughness")";
            masked.replace(masked.find(plain), plain.size(),
                           R"("materials":[{"alphaMode":"MASK","alphaCutoff":0.3,"pbrMetallicRoughness")");
            write_text(dir / "mask.gltf", masked);
            const ImportedModel mask = khdays::assets::import_gltf(dir / "mask.gltf");
            check(mask.model.meshes.size() == 1 && mask.model.meshes[0].alpha_mode == AlphaMode::mask &&
                      near(mask.model.meshes[0].alpha_cutoff, 0.3F),
                  "glTF: MASK and its cutoff");
        }
        for (std::size_t j = 0; j < 2 && j < fbx.joints.size() && j < gltf.joints.size(); ++j) {
            check(fbx.joints[j].name == gltf.joints[j].name, "joint " + std::to_string(j) + " name");
            check(fbx.joints[j].parent == gltf.joints[j].parent, "joint " + std::to_string(j) + " parent");
            // where each joint is at rest; its frame's turn is the file's own (a
            // bone authored in Z-up axes keeps that orientation: data, not units)
            for (int k = 12; k < 15; ++k) {
                check(near(fbx.joints[j].world[static_cast<std::size_t>(k)],
                           gltf.joints[j].world[static_cast<std::size_t>(k)]),
                      "joint " + std::to_string(j) + " rest place [" + std::to_string(k) + "]");
            }
        }
        if (failures > 0) {
            // what the two gave, to see a difference
            for (const auto* m : {&gltf, &fbx}) {
                std::cerr << (m == &gltf ? "glTF" : "FBX") << ":\n";
                for (const auto& j : m->joints) {
                    std::cerr << "  joint " << j.name << " parent " << j.parent << " world";
                    for (float v : j.world)
                        std::cerr << " " << v;
                    std::cerr << "\n";
                }
                if (!m->model.meshes.empty()) {
                    for (const auto& v : m->model.meshes[0].vertices) {
                        std::cerr << "  vertex " << v.position[0] << " " << v.position[1] << " " << v.position[2]
                                  << " n " << v.normal[0] << " " << v.normal[1] << " " << v.normal[2] << " t "
                                  << v.texcoord[0] << " " << v.texcoord[1] << "\n";
                    }
                }
            }
        }
        check(fbx.model.palette.size() == gltf.model.palette.size(), "palette sizes");
        for (const auto& m : fbx.model.palette)
            check(identity(m), "FBX palette entry the identity at rest");
        if (gltf.model.meshes.size() == 1 && fbx.model.meshes.size() == 1) {
            const auto& g = gltf.model.meshes[0];
            const auto& f = fbx.model.meshes[0];
            check(f.vertices.size() == g.indices.size(), "FBX: a vertex per corner");
            for (std::size_t c = 0; c < g.indices.size() && c < f.vertices.size(); ++c) {
                const auto& gv = g.vertices[g.indices[c]];
                const auto& fv = f.vertices[c];
                const std::string at = "corner " + std::to_string(c);
                for (int k = 0; k < 3; ++k) {
                    check(near(fv.position[static_cast<std::size_t>(k)], gv.position[static_cast<std::size_t>(k)]),
                          at + " position " + std::to_string(k));
                    check(near(fv.normal[static_cast<std::size_t>(k)], gv.normal[static_cast<std::size_t>(k)]),
                          at + " normal " + std::to_string(k));
                }
                check(near(fv.texcoord[0], gv.texcoord[0]) && near(fv.texcoord[1], gv.texcoord[1]), at + " texel");
                for (int k = 0; k < 4; ++k) {
                    const auto s = static_cast<std::size_t>(k);
                    check(near(fv.weights[s], gv.weights[s]), at + " weight " + std::to_string(k));
                    if (gv.weights[s] > 0.0F)
                        check(fv.joints[s] == gv.joints[s], at + " joint " + std::to_string(k));
                }
            }
        }
    } catch (const std::exception& e) {
        check(false, std::string("glTF/FBX import threw: ") + e.what());
    }

    // .glb, the image in a buffer view; .gltf with a JPEG beside it
    write_glb(dir / "quad.glb");
    write_file(dir / "tex.jpg", kJpeg, sizeof(kJpeg));
    write_text(dir / "jpeg.gltf", gltf_json(R"({"name":"tex","uri":"tex.jpg"})",
                                            R"(,"uri":"data:application/octet-stream;base64,)" + base64(buffer) + "\"",
                                            buffer.size(), false));
    try {
        const ImportedModel glb = khdays::assets::import_gltf(dir / "quad.glb");
        check(glb.missing_textures.empty() && glb.textures.size() == 1 && glb.textures[0].second.height == 2,
              ".glb: its embedded texture read");
        check(glb.model.meshes.size() == 1 && glb.model.meshes[0].vertices.size() == 4, ".glb: its mesh");
        const ImportedModel jpeg = khdays::assets::import_gltf(dir / "jpeg.gltf");
        check(jpeg.missing_textures.empty() && jpeg.textures.size() == 1 && jpeg.textures[0].second.width == 2,
              ".gltf: its JPEG texture read");
    } catch (const std::exception& e) {
        check(false, std::string(".glb/JPEG import threw: ") + e.what());
    }

    // five weights on one corner: the strongest four, renormalised; a missing texture
    {
        std::vector<Cluster> five;
        const float w[5] = {0.3F, 0.25F, 0.2F, 0.15F, 0.1F};
        for (int c = 0; c < 5; ++c) {
            five.push_back({"b" + std::to_string(c), static_cast<float>(c) * 10.0F, {0}, {w[c]}});
        }
        five[0].indexes = {0, 1, 2};
        five[0].weights = {w[0], 1, 1};
        write_text(dir / "five.fbx", fbx_text({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2}, {{0, 0}, {1, 0}, {0, 1}},
                                              five, "absent.png"));
        try {
            const ImportedModel m = khdays::assets::import_fbx(dir / "five.fbx");
            check(m.missing_textures.size() == 1, "a missing texture reported");
            if (!m.model.meshes.empty() && !m.model.meshes[0].vertices.empty()) {
                const auto& v = m.model.meshes[0].vertices[0];
                const float total = w[0] + w[1] + w[2] + w[3];
                for (int k = 0; k < 4; ++k) {
                    check(near(v.weights[static_cast<std::size_t>(k)], w[k] / total),
                          "five weights: weight " + std::to_string(k));
                    check(v.joints[static_cast<std::size_t>(k)] == 1U + static_cast<std::uint32_t>(k),
                          "five weights: joint " + std::to_string(k));
                }
            } else {
                check(false, "five weights: a vertex");
            }
        } catch (const std::exception& e) {
            check(false, std::string("five-weight import threw: ") + e.what());
        }
    }

    // A room's own animation and moving textures, written and read back
    {
        using namespace khdays::assets;
        for (const char* name : {"a.png", "b.png"})
            write_file(dir / name, kPng, sizeof(kPng));
        Scene scene;
        scene.name = "room";
        scene.nodes = {{"room"}, {"sky", 0}, {"piece", 0, {1.0F, 2.0F, 3.0F}}, {"piece_j0", 2}};
        scene.nodes[1].sky = true;
        SceneMaterial still{"still", "a.png"};
        SceneMaterial scrolling{"scrolling", "a.png"};
        scrolling.scroll = {0.25F, -0.5F};
        SceneMaterial framed{"framed", "a.png"};
        framed.alpha_mode = NeutralMesh::AlphaMode::mask;
        framed.double_sided = true;
        framed.frame_pictures = {"a.png", "b.png"};
        framed.frames = {{1, 10}, {0, 20}};
        framed.frames_loop_from = 1;
        scene.materials = {still, scrolling, framed};
        ScenePrimitive tri;
        tri.positions = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        tri.uvs = {{0, 0}, {1, 0}, {0, 1}};
        tri.colors = {{255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 128}};
        tri.indices = {0, 1, 2};
        auto with = [&](int material) {
            ScenePrimitive p = tri;
            p.material = material;
            return p;
        };
        SceneMesh sky_mesh{"sky", 1, {}, {with(0)}};
        SceneMesh ground{"ground", 0, {}, {with(1), with(2)}};
        ScenePrimitive skinned = with(0);
        skinned.joint_of = {0, 0, 0};
        SceneMesh piece{"piece", 2, {3}, {skinned}};
        scene.meshes = {sky_mesh, ground, piece};
        SceneAnimation move{"piece", true, 0.5F};
        move.tracks.push_back({3, SceneTrack::Path::translation, {0.0F, 1.0F}, {0, 0, 0, 10, 0, 0}});
        move.tracks.push_back(
            {3, SceneTrack::Path::rotation, {0.0F, 1.0F}, {0, 0, 0, 1, 0, 0.7071068F, 0, 0.7071068F}});
        scene.animations = {move};
        write_text(dir / "room.gltf", scene_to_gltf(scene));
        try {
            const ImportedModel m = import_gltf(dir / "room.gltf");
            check(m.missing_textures.empty(), "room: every picture found");
            check(m.nodes.size() == 7 && m.nodes[2].name == "piece" && near(m.nodes[2].translation[1], 2.0F) &&
                      m.nodes[3].parent == 2,
                  "room: nodes and their rest");
            check(m.joints.size() == 1 && m.joints[0].node == 3, "room: the piece's joint is its node");
            check(m.animations.size() == 1, "room: one animation");
            if (m.animations.size() == 1) {
                const auto& a = m.animations[0];
                check(a.name == "piece" && a.loop && near(a.loop_from, 0.5F) && near(a.duration, 1.0F),
                      "room: the animation's loop");
                check(a.tracks.size() == 2 && a.tracks[0].node == 3 &&
                          a.tracks[0].path == ImportedTrack::Path::translation && near(a.tracks[0].values[3], 10.0F) &&
                          a.tracks[1].path == ImportedTrack::Path::rotation && near(a.tracks[1].values[5], 0.7071068F),
                      "room: the keys");
            }
            check(m.model.meshes.size() == 4, "room: a mesh per primitive");
            if (m.model.meshes.size() == 4) {
                const auto& meshes = m.model.meshes;
                check(meshes[0].sky && !meshes[1].sky && !meshes[3].sky, "room: the sky's mesh");
                check(near(meshes[1].scroll[0], 0.5F) && near(meshes[1].scroll[1], -1.0F), "room: scroll in texels");
                const std::vector<std::pair<std::string, int>> frames{{"b.png", 10}, {"a.png", 20}};
                check(meshes[2].texture_frames == frames && meshes[2].texture_frames_loop &&
                          meshes[2].texture_frames_loop_from == 1U && meshes[2].double_sided &&
                          meshes[2].alpha_mode == NeutralMesh::AlphaMode::mask,
                      "room: framed texture");
                check(meshes[3].vertices.size() == 3 && meshes[3].vertices[0].joints[0] == m.joints[0].palette_index,
                      "room: the piece skinned to its bone");
            }
            bool second = false;
            for (const auto& [name, texture] : m.textures)
                second = second || name == "b.png";
            check(second, "room: a frame's own picture loaded");

            // written again as a room change writes a model that moves by itself, and read back the same
            check(moves_by_itself(m), "room: moves by itself");
            std::map<std::string, GltfTextureRef> refs;
            for (const auto& [name, texture] : m.textures)
                refs[name] = {name, texture.width, texture.height, false};
            write_text(dir / "again.gltf", scene_to_gltf(scene_of(m, refs)));
            const ImportedModel again = import_gltf(dir / "again.gltf");
            check(again.animations.size() == 1 && again.animations[0].loop &&
                      near(again.animations[0].loop_from, 0.5F) && again.animations[0].tracks.size() == 2,
                  "again: the animation");
            check(again.joints.size() == 1 && again.joints[0].node >= 0 &&
                      again.nodes[static_cast<std::size_t>(again.joints[0].node)].name == "piece_j0",
                  "again: the piece's joint");
            check(again.model.meshes.size() == 4, "again: the meshes");
            if (again.model.meshes.size() == 4 && m.model.meshes.size() == 4) {
                const auto& meshes = again.model.meshes;
                const std::vector<std::pair<std::string, int>> frames{{"b.png", 10}, {"a.png", 20}};
                check(meshes[0].sky && near(meshes[1].scroll[0], 0.5F) && meshes[2].texture_frames == frames &&
                          meshes[2].texture_frames_loop_from == 1U,
                      "again: sky, scroll and frames");
                const auto& v = meshes[3].vertices[1];
                const auto& was = m.model.meshes[3].vertices[1];
                check(near(v.position[0], was.position[0]) && near(v.position[2], was.position[2]) &&
                          v.joints[0] == again.joints[0].palette_index,
                      "again: the piece's vertex on its joint");
            }
        } catch (const std::exception& e) {
            check(false, std::string("room import threw: ") + e.what());
        }
    }

    fs::remove_all(dir);
    if (failures == 0) {
        std::cout << "model_import: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
