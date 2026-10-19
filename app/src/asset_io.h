#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "manifest.h"
#include "scene.h"

// Loads a converted model (`.glb`, or `.gltf` with a data-URI buffer) together
// with every PNG its materials reference. `glbRelPath` is relative to
// `libraryDir`; texture URIs are relative to the model's own directory. The
// scene also carries the model's skeleton and its animation clips (channels to
// non-joint nodes are skipped).
//
// Throws std::runtime_error when the source file cannot be parsed, a buffer
// cannot be read, or an animation uses CUBICSPLINE interpolation (its keys
// cannot be read as LINEAR without mis-animating). A texture the converter could
// not produce (missing or undecodable PNG) is skipped — the material keeps its
// factors — so partial conversion degrades instead of failing the load.
Scene load_scene(const std::filesystem::path& libraryDir, const std::string& glbRelPath);

// Assembles a composed scene from a parsed manifest: every instance's glb is
// loaded with `mat4_from_trs(instance.position, instance.rotation,
// instance.scale)` as its meshes' model matrix (static: rig = -1), then the
// player's glb is appended with the identity model and rig 0, then each
// entity's glb with its own manifest placement and its own rig (1, 2, ... in
// manifest order). Materials keep their indices valid across instances (each
// glb's material indices are offset by what the earlier loads contributed;
// texture indices need no offset because images append to the shared texture
// list directly), and `boundsMin/boundsMax` cover every placed vertex in world
// space.
//
// Every rigged glb keeps its own skeleton and clips as one RigCpu; the player's
// rig is dynamic, an entity's plays the exact clip the manifest names. An
// entity whose named clip the model does not carry (or a model that has no
// clips at all) is a loud load error naming the glb path and the clip — never a
// silent rest pose. Throws std::runtime_error like load_scene, naming the
// instance's or entity's manifest glb path when one cannot be loaded.
//
// `playerMeshBegin` (optional) receives the player's rig's first mesh index:
// the static instances precede it and the entities follow it. Kept for the S3
// callers; the rig records carry the authoritative ranges now.
Scene compose_scene(const std::filesystem::path& libraryDir, const SceneManifest& manifest,
                    const std::string& playerGlbRelPath, std::size_t* playerMeshBegin = nullptr);
