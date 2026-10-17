#pragma once

#include <filesystem>
#include <string>

#include "scene.h"

// Loads a converted model (`.glb`, or `.gltf` with a data-URI buffer) together
// with every PNG its materials reference. `glbRelPath` is relative to
// `libraryDir`; texture URIs are relative to the model's own directory.
//
// Throws std::runtime_error when the source file cannot be parsed or a buffer
// cannot be read. A texture the converter could not produce (missing or
// undecodable PNG) is skipped — the material keeps its factors — so partial
// conversion degrades instead of failing the load.
Scene load_scene(const std::filesystem::path& libraryDir, const std::string& glbRelPath);
