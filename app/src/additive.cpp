#include "additive.h"

// S13 Task 1 stubs: Task 6 implements the layer.

void additive_apply(LocalPose&, const LocalPose&, const LocalPose&, float) {}  // S13 Task 6

void additive_request(AddLayers&, const std::vector<AddEdge>&, std::string_view) {}  // S13 Task 6

void additive_step(AddLayers&, const std::vector<float>&, float) {}  // S13 Task 6

bool additive_playing(const AddLayers&, int) { return false; }  // S13 Task 6
