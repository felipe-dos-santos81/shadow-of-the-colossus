#pragma once

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
// instance.scale)` as its meshes' model matrix, then the player's glb is
// appended with the identity model. Materials keep their indices valid across
// instances (each glb's material indices are offset by what the earlier loads
// contributed; texture indices need no offset because images append to the
// shared texture list directly), and `boundsMin/boundsMax` cover every placed
// vertex in world space.
//
// The composed scene has exactly one skeleton and one clip list, and they are
// the player's: a rig on an instance glb is skipped with a warning (the app
// animates the player). Throws std::runtime_error like load_scene, naming the
// instance's manifest glb path when one cannot be loaded.
Scene compose_scene(const std::filesystem::path& libraryDir, const SceneManifest& manifest,
                    const std::string& playerGlbRelPath);
