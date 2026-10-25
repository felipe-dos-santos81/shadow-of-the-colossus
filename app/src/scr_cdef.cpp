#include "scr_cdef.h"

#include <algorithm>
#include <cstdint>

#include "scr_library.h"

namespace {

// FUN_0062bfd0's word test: a negative value matches any bit of mask, else (attr & mask) == value.
bool masked(uint64_t attr, uint64_t mask, uint64_t value) {
    return static_cast<int64_t>(value) < 0 ? (attr & mask) != 0 : (attr & mask) == value;
}

// One record's result before the expect compare (binary FUN_0062bfd0 unless noted; R2 and
// census16 §4). A failed precondition is false.
bool result(const ScrCdefRecord& r, const std::vector<int>& requested, const ScrWorld& w) {
    const uint32_t kind = static_cast<uint32_t>(w.hitKind);
    switch (r.op) {
        case 0x04: return masked(w.bossGroundAttr, r.mask, r.value);  // his own attr word sub+0x8c0
        // Landing (R2 ruling: "his root is on the ground"): contact (sub+0x1328) and its surface
        // class (guess: the contact word is his ground word), slope (0x28) and landing-ready
        // (0x3b, guess) not modelled beyond the contact.
        case 0x27: return w.bossOnGround && masked(w.bossGroundAttr, r.mask, r.value);
        case 0x28: case 0x3b: return w.bossOnGround;
        case 0x64: return !w.bossOnGround;     // no ground under him (guess)
        case 0x3a: return true;                // ground probe valid; every export's mask is -1 (guess)
        case 0x45: return true;                // false only for a ridden boss without input (guess)
        case 0x36: return w.bossControlMode == scr_handle_index(r.handle.text);  // sub+0x9c8: his own table
        case 0x38: return std::find(requested.begin(), requested.end(), r.handle.ctrl) != requested.end();
        case 0x54: return w.bossMotion == r.handle.clip;
        case 0x56:  // any layer (FUN_006357f0; by clip, not motion-def: a labelled deviation); without layers the base
            return w.bossLayerPlaying ? w.bossLayerPlaying(r.handle.clip) : w.bossMotion == r.handle.clip;
        case 0x59: return w.hit && masked(static_cast<uint64_t>(w.hitRegion), r.mask, r.value);
        case 0x5c:  // the hit kind; the argument is the mask word
            if (!w.hit) return false;
            if (r.mask == 0) return kind < 6 && ((0x35u >> kind) & 1u) != 0;  // {0,2,4,5}
            if (r.mask == 1) return (kind | 2u) == 3u;                        // {1,3}
            return static_cast<int64_t>(r.mask) == w.hitKind;
        case 0x5d:  // census16 §4: 0x5c's twin
            if (!w.hit) return false;
            if (r.mask == 0) return kind == 0 || kind == 5;
            if (r.mask == 1) return kind - 1u < 4u;  // {1,2,3,4}
            return false;
        case 0x4c:  // a rider on his part whose attr matches (graph findings §2)
            return w.playerAttached && masked(static_cast<uint64_t>(w.playerAttachedRegion), r.mask, r.value);
        case 0x76: return !(w.hp > 0.0f);  // mortality: pad+0x134 bit 9 while HP <= 0
        // Stick X/Y (0x42/0x43): S12's reading — the AI's turn rows fire on their bits. The pad
        // buttons, d-pad and holds: no controller is attached to a boss in play (R2, guess).
        // Gaps: 0x02/0x4d contact floats sub+0x8e4/+0x8ec, 0x4a/0x4b clip-time windows.
        default: return false;
    }
}

}  // namespace

bool scr_cdef_eval(const ScrGraph& graph, std::string_view handle, const std::vector<int>& requested,
                   const ScrWorld& world, const ScrRuntime&) {
    const auto it = graph.controlDefs.find(std::string(handle));
    if (it == graph.controlDefs.end()) return false;
    for (const ScrCdefRecord& r : it->second) {
        if (static_cast<int>(result(r, requested, world)) != r.expect) return false;
    }
    return true;
}
