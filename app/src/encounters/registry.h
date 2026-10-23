#pragma once

#include <string_view>

#include "valus.h"

// Every encounter description, by the manifest's `encounter <id>` (S11 Task 7).
// A colossus adds its table's header and one row.
inline constexpr const EncounterDesc* kEncounters[] = {&encounters::kValus};

// The table whose id is `id` exactly, or nullptr (the caller's load error).
inline const EncounterDesc* find_encounter(std::string_view id) {
    for (const EncounterDesc* desc : kEncounters) {
        if (id == desc->id) {
            return desc;
        }
    }
    return nullptr;
}
