#pragma once

// A decoded DS model written out as glTF 2.0 with its skeleton, to make
// replacement models from (the port's graphics dump).

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/model_import.h"

namespace khdays::assets {

// A texture the glTF refers to: its image's URI (relative to the .gltf) and
// its size in texels, the unit the DS's texture coordinates count in; whether
// its texels carry an alpha of their own to blend by (the DS's A3I5 and A5I3).
struct GltfTextureRef final {
    std::string uri;
    int width = 0;
    int height = 0;
    bool translucent = false;
};

// Whether `pixels` RGBA texels have an alpha of their own: some neither transparent nor opaque
// (between 8 and 247, the DS's 1 to 30 of 31), as an A3I5 or A5I3 texture's dumped picture has.
bool texels_translucent(const std::uint8_t* rgba, std::size_t pixels);

// An animation of the model's skeleton to write with it, under `name`: the
// bones' object matrices (as NeutralModel::object_matrices) at each of its
// frames, each a translation, a rotation and a scale -- not a blend of two
// rotations' matrices, which glTF's rotation keys cannot hold.
struct GltfAnimation final {
    std::string name;
    std::vector<std::vector<std::array<float, 16>>> frames;
};

// `model` as one .gltf with its buffer embedded: a node per bone under its parent, one skin (the bones,
// their rest world matrices inverted), the meshes in their rest pose weighted to their palette's bones,
// textured over `textures`' sizes (untextured when missing). Each of `animations` becomes a glTF
// animation: every bone's matrix under its parent at each frame as linear TRS keys, a constant track
// one key.
std::string to_gltf(const NeutralModel& model, const std::map<std::string, GltfTextureRef>& textures,
                    const std::vector<GltfAnimation>& animations = {}, float frames_per_second = 30.0F);

// A scene of several nodes to write as glTF (a KH2 room): each node in its own
// place under its parent; meshes hung from a node or skinned to several (each
// vertex to one joint, its position in that joint's space); animations of the
// nodes; materials whose textures scroll or change picture. The port's own
// glTF extras say what glTF cannot: khdays_sky on a node, khdays_scroll and
// khdays_frames on a material, khdays_loop on an animation.
struct SceneNode final {
    std::string name;
    int parent = -1;  // node 0, the root, has none
    std::array<float, 3> translation{0.0F, 0.0F, 0.0F};
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};  // quaternion x y z w
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    bool sky = false;  // drawn around the camera, behind everything
};

struct SceneMaterial final {
    std::string name;
    std::string texture;  // the picture's URI beside the .gltf; empty: vertex colours only
    NeutralMesh::AlphaMode alpha_mode = NeutralMesh::AlphaMode::opaque;
    float alpha_cutoff = 0.5F;
    bool double_sided = false;
    std::array<bool, 2> repeat{true, true};   // past 0..1: repeated, else clamped
    std::array<float, 2> scroll{0.0F, 0.0F};  // texture coordinates added per game frame (60 a second)
    // Pictures shown in turn: frame_pictures[frames[i][0]] for frames[i][1] game
    // frames; after the last, from frames[loop_from] again when looping.
    std::vector<std::string> frame_pictures;
    std::vector<std::array<int, 2>> frames;
    bool frames_loop = true;
    int frames_loop_from = 0;
};

struct ScenePrimitive final {
    int material = 0;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;  // 0..1 over the material's picture
    std::vector<std::array<std::uint8_t, 4>> colors;
    std::vector<int> joint_of;  // skinned: each vertex's joint, an index into SceneMesh::joints
    std::vector<std::uint32_t> indices;
};

struct SceneMesh final {
    std::string name;
    int node = 0;             // the node it hangs from
    std::vector<int> joints;  // the nodes it is skinned to; empty: rigid on `node`
    std::vector<ScenePrimitive> primitives;
};

struct SceneTrack final {
    int node = 0;
    enum class Path { translation, rotation, scale };
    Path path = Path::translation;
    std::vector<float> times;   // seconds
    std::vector<float> values;  // 3 per key, 4 for rotations
};

struct SceneAnimation final {
    std::string name;
    bool loop = false;
    float loop_from = 0.0F;  // seconds: where a loop starts again
    std::vector<SceneTrack> tracks;
};

struct Scene final {
    std::string name;
    std::vector<SceneNode> nodes;
    std::vector<SceneMaterial> materials;
    std::vector<SceneMesh> meshes;
    std::vector<SceneAnimation> animations;
};

// `scene` as one .gltf with its buffer embedded (its extras: docs/RENDERING.md, "Mods' graphics").
std::string scene_to_gltf(const Scene& scene);

// Whether an imported model moves by itself: its own animations, a scrolling or changing texture, a sky.
bool moves_by_itself(const ImportedModel& model);

// An imported model that moves by itself back as a scene, to write again: its nodes at rest, each skinned
// mesh on its joints' nodes (each vertex to its heaviest joint), the meshes loading made still on nodes
// of their own (`still`, `sky`), its animations and moving textures; `textures` its textures' files.
Scene scene_of(const ImportedModel& model, const std::map<std::string, GltfTextureRef>& textures);

}  // namespace khdays::assets
