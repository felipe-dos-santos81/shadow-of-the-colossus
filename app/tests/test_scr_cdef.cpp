// The generic control-def evaluator (S13 Task 4). The fixtures are real exports:
// tools/re/scr/graph_export.py runs at test time into the build directory from
// out/re/eboot.elf (a case fails, never skips, without it); nothing derived is in git.
#include <doctest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_reader.h"
#include "knight_fixture.h"
#include "scr_cdef.h"
#include "scr_graph.h"
#include "scr_predicates.h"

namespace {

namespace fs = std::filesystem;
using namespace knight_fixture;

bool p_any(const ScrClause&, const ScrWorld&, const ScrRuntime&) { return false; }
void a_any(const ScrAction&, ScrCtx&) {}

// `boss`'s export, generated once per run into the build directory.
const std::string& export_json(const std::string& boss) {
    static std::map<std::string, std::string> cache;
    auto it = cache.find(boss);
    if (it != cache.end()) return it->second;
    const fs::path out = fs::path(SOTC_TEST_BUILD_DIR) / "fixtures" / (boss + ".cdef.graph.json");
    fs::create_directories(out.parent_path());
    const std::string cmd = "python3 -I '" + (kRepoRoot / "tools/re/scr/graph_export.py").string() + "' export " +
                            boss + " '" + out.string() + "'";
    if (std::system(cmd.c_str()) != 0) throw std::runtime_error("graph_export.py failed (out/re/eboot.elf?): " + cmd);
    return cache.emplace(boss, read_file(out)).first->second;
}

// A registry that accepts every fn and kind the export names: these cases test the
// control-defs, not the handlers.
ScrRegistry permissive(const std::string& json) {
    ScrRegistry r;
    std::function<void(const Json&)> walk = [&](const Json& v) {
        if (v.kind == Json::Object) {
            for (const auto& [key, child] : v.object) {
                if (key == "fn" && child.kind == Json::String) {
                    const uint32_t fn = static_cast<uint32_t>(std::strtoul(child.string.c_str() + 2, nullptr, 16));
                    r.predicates[fn] = p_any;
                    r.actions[fn] = a_any;
                }
                walk(child);
            }
        } else if (v.kind == Json::Array) {
            for (const Json& child : v.array) walk(child);
        }
    };
    walk(json_parse(json));
    for (int k = -1; k <= 10; ++k) r.kinds.insert(k);
    return r;
}

std::string load_error(const std::string& json) {
    ScrGraph g;
    std::string error;
    scr_graph_load(json, export_clips(json), permissive(json), g, error);
    return error;
}

const ScrGraph& graph(const std::string& boss) {
    static std::map<std::string, ScrGraph> cache;
    auto it = cache.find(boss);
    if (it != cache.end()) return it->second;
    const std::string& json = export_json(boss);
    ScrGraph g;
    std::string error;
    const bool ok = scr_graph_load(json, export_clips(json), permissive(json), g, error);
    INFO(boss << ": " << error);
    REQUIRE(ok);
    return cache.emplace(boss, std::move(g)).first->second;
}

// The bosses whose exports between them use all 37 opcodes (census16 §4).
const char* const kBosses[] = {"KNIGHT", "KIRIN", "MAMMOTH", "EEL", "MINOTOUR_A", "YAMORI_B", "LEO"};

// Evaluates one record alone (its expect forced) in a one-def graph.
bool eval_one(ScrCdefRecord rec, int expect, const std::vector<int>& requested, const ScrWorld& w) {
    ScrGraph g;
    rec.expect = expect;
    g.controlDefs["T:control-def[0]"] = {rec};
    return scr_cdef_eval(g, "T:control-def[0]", requested, w, ScrRuntime{});
}

// An attribute word the record's (mask, value) test accepts (a negative value: any bit of mask).
uint64_t accepted(const ScrCdefRecord& r) {
    return static_cast<int64_t>(r.value) < 0 ? r.mask : r.value;
}

int bracket(const std::string& text) {
    const size_t open = text.rfind('[');
    return open == std::string::npos ? -1 : std::atoi(text.c_str() + open + 1);
}

using Setup = std::function<void(const ScrCdefRecord&, ScrWorld&, std::vector<int>&)>;
struct OpCase {
    int op;
    Setup makeTrue;   // null: never true in the app
    Setup makeFalse;  // null: always true in the app
    std::function<bool(const ScrCdefRecord&)> pick = nullptr;  // which exported record (null: the first)
};

void nothing(const ScrCdefRecord&, ScrWorld&, std::vector<int>&) {}

std::vector<OpCase> op_cases() {
    const Setup ground_off = [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) { w.bossOnGround = false; };
    std::vector<OpCase> cases = {
        {0x04, [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) { w.bossGroundAttr = accepted(r); },
         [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) { w.bossGroundAttr = 0; }},
        {0x27, [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) { w.bossGroundAttr = accepted(r); },
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             w.bossGroundAttr = accepted(r);
             w.bossOnGround = false;
         }},
        {0x28, nothing, ground_off},
        {0x36, [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) { w.bossControlMode = bracket(r.handle.text); },
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) { w.bossControlMode = bracket(r.handle.text) + 1; }},
        {0x38, [](const ScrCdefRecord& r, ScrWorld&, std::vector<int>& q) { q = {3, r.handle.ctrl}; },
         [](const ScrCdefRecord& r, ScrWorld&, std::vector<int>& q) { q = {r.handle.ctrl == 0 ? 1 : 0}; }},
        {0x3a, nothing, nullptr},
        {0x3b, nothing, ground_off},
        {0x45, nothing, nullptr},
        {0x4c,
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             w.playerAttached = true;
             w.playerAttachedRegion = static_cast<int>(accepted(r));
         },
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             w.playerAttached = false;  // the region alone does not hold
             w.playerAttachedRegion = static_cast<int>(accepted(r));
         }},
        {0x54, [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) { w.bossMotion = r.handle.clip; },
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) { w.bossMotion = r.handle.clip + 1; }},
        {0x56,
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             const int c = r.handle.clip;
             w.bossMotion = c + 1;  // an additive layer plays it, not the base
             w.bossLayerPlaying = [c](int clip) { return clip == c; };
         },
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             w.bossMotion = r.handle.clip;  // the base would hold, but the layers say no
             w.bossLayerPlaying = [](int) { return false; };
         }},
        {0x59,
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             w.hit = true;
             w.hitRegion = static_cast<int>(accepted(r));
         },
         [](const ScrCdefRecord& r, ScrWorld& w, std::vector<int>&) {
             w.hit = false;  // no hit this frame
             w.hitRegion = static_cast<int>(accepted(r));
         }},
        {0x5c,  // arg 0: kind in {0,2,4,5}
         [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) {
             w.hit = true;
             w.hitKind = 4;
         },
         [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) {
             w.hit = true;
             w.hitKind = 3;
         },
         [](const ScrCdefRecord& r) { return r.mask == 0; }},
        {0x5d,  // arg 0: kind in {0,5} (0x5c's arg 0 would also take 2)
         [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) {
             w.hit = true;
             w.hitKind = 5;
         },
         [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) {
             w.hit = true;
             w.hitKind = 2;
         },
         [](const ScrCdefRecord& r) { return r.mask == 0; }},
        {0x64, ground_off, nothing},
        {0x76, [](const ScrCdefRecord&, ScrWorld& w, std::vector<int>&) { w.hp = 0.0f; }, nothing},
    };
    // Pads, sticks (S12's reading: the AI's turn rows fire on their bits) and the gaps
    // (0x02/0x4d contact floats, 0x4a/0x4b clip-time windows): never true in the app.
    for (const int op : {0x02, 0x42, 0x43, 0x4a, 0x4b, 0x4d, 0x67, 0x68, 0x69, 0x6c, 0x6e, 0x71, 0x74, 0x78, 0x79,
                         0x7a, 0x7c, 0x7d, 0x7e, 0x80, 0x83}) {
        cases.push_back({op, nullptr, nothing});
    }
    return cases;
}

const ScrCdefRecord* find_record(int op, const std::function<bool(const ScrCdefRecord&)>& pick) {
    for (const char* boss : kBosses) {
        for (const auto& [key, recs] : graph(boss).controlDefs) {
            for (const ScrCdefRecord& r : recs) {
                if (r.op == op && (!pick || pick(r))) return &r;
            }
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("kScrCdefOps: the 37 opcodes, four of them gaps") {
    CHECK(kScrCdefOps.size() == 37);
    std::vector<int> gaps;
    for (const ScrCdefOp& op : kScrCdefOps) {
        if (op.gap) gaps.push_back(op.op);
    }
    CHECK(gaps == std::vector<int>{0x02, 0x4a, 0x4b, 0x4d});
    CHECK(op_cases().size() == kScrCdefOps.size());
}

TEST_CASE("scr_cdef_eval: one exported record per opcode, true and false") {
    for (const OpCase& c : op_cases()) {
        CAPTURE(c.op);
        const ScrCdefRecord* rec = find_record(c.op, c.pick);
        REQUIRE(rec != nullptr);
        ScrWorld tw, fw;
        std::vector<int> tq, fq;
        if (c.makeTrue) c.makeTrue(*rec, tw, tq);
        if (c.makeFalse) c.makeFalse(*rec, fw, fq);
        const bool canTrue = c.makeTrue != nullptr, canFalse = c.makeFalse != nullptr;
        CHECK(eval_one(*rec, 1, tq, tw) == canTrue);
        CHECK(eval_one(*rec, 0, tq, tw) == !canTrue);
        CHECK(eval_one(*rec, 1, fq, fw) == !canFalse);
        CHECK(eval_one(*rec, 0, fq, fw) == canFalse);
    }
}

TEST_CASE("scr_cdef_eval: 0x56 without bossLayerPlaying reads the base motion") {
    const ScrCdefRecord* rec = find_record(0x56, nullptr);
    REQUIRE(rec != nullptr);
    ScrWorld w;
    w.bossMotion = rec->handle.clip;
    CHECK(eval_one(*rec, 1, {}, w));
    w.bossMotion = rec->handle.clip + 1;
    CHECK_FALSE(eval_one(*rec, 1, {}, w));
}

TEST_CASE("scr_cdef_eval: the AND over records; an unexported handle is false") {
    const ScrGraph& g = graph("KNIGHT");
    const std::string fail = "KNIGHT_CONTROL:control-def[20] 'KNIGHT_BOSS_RIGHT_FOOT_ATTACK_FAILURE_CONDITIONS'";
    REQUIRE(g.controlDefs.count(fail) == 1);
    ScrWorld w;
    w.bossControlMode = 7;
    CHECK(scr_cdef_eval(g, fail, {6}, w, ScrRuntime{}));
    CHECK_FALSE(scr_cdef_eval(g, fail, {5}, w, ScrRuntime{}));
    w.bossControlMode = 8;
    CHECK_FALSE(scr_cdef_eval(g, fail, {6}, w, ScrRuntime{}));
    CHECK_FALSE(scr_cdef_eval(g, "KNIGHT_CONTROL:control-def[9999]", {6}, w, ScrRuntime{}));
}

TEST_CASE("every control-def of KNIGHT (67) and KIRIN (66) evaluates; their opcodes are all known") {
    const std::pair<const char*, size_t> counts[] = {{"KNIGHT", 67}, {"KIRIN", 66}};
    for (const auto& [boss, n] : counts) {
        const ScrGraph& g = graph(boss);
        CHECK(g.controlDefs.size() == n);
        for (const auto& [key, recs] : g.controlDefs) {
            for (const ScrCdefRecord& r : recs) {
                bool known = false;
                for (const ScrCdefOp& op : kScrCdefOps) known = known || op.op == r.op;
                CHECK_MESSAGE(known, key << " op " << r.op);
            }
            (void)scr_cdef_eval(g, key, {}, ScrWorld{}, ScrRuntime{});
        }
    }
}

TEST_CASE("an unknown control-def opcode is a load error naming it") {
    std::string json = export_json("KNIGHT");
    const std::string from = "\"op\": 118,";  // BOSS_MORTALITY_CONDITIONS' 0x76
    const size_t at = json.find(from);
    REQUIRE(at != std::string::npos);
    json.replace(at, from.size(), "\"op\": 170,");  // 0xaa: in FUN_0062bfd0's switch, in no export
    const std::string error = load_error(json);
    CHECK(error.find("0xaa") != std::string::npos);
    CHECK(error.find("control-def") != std::string::npos);
}

// S12's control-def table (scr_predicates.cpp cdefs()/cdef_holds before S13; 30 keys), as
// literal rows: the evaluator over KNIGHT's exported records must give the same answer on
// every grid point (hp ±, on/off ground, control-modes 0–12, no request or one bit 0–63).
// Proved against the deleted table itself before it went (task-4-report.md).
namespace {
enum class Old { False, Mortality, Landing, NoGround, Fail7, Fail8, Fail6 };
const std::pair<const char*, Old> kOldTable[] = {
    {"BOSS_CONTROL:control-def[44]", Old::Mortality},   {"PLAYER_CONTROL:control-def[2999]", Old::Landing},
    {"PLAYER_CONTROL:control-def[3151]", Old::NoGround}, {"KNIGHT_CONTROL:control-def[20]", Old::Fail7},
    {"KNIGHT_CONTROL:control-def[23]", Old::Fail8},      {"KNIGHT_CONTROL:control-def[26]", Old::Fail6},
    {"BOSS_CONTROL:control-def[6]", Old::False},         {"BOSS_CONTROL:control-def[10]", Old::False},
    {"BOSS_CONTROL:control-def[14]", Old::False},        {"BOSS_CONTROL:control-def[18]", Old::False},
    {"COMMON_CONTROL:control-def[18]", Old::False},      {"COMMON_CONTROL:control-def[42]", Old::False},
    {"COMMON_CONTROL:control-def[60]", Old::False},      {"COMMON_CONTROL:control-def[64]", Old::False},
    {"COMMON_CONTROL:control-def[70]", Old::False},      {"COMMON_CONTROL:control-def[123]", Old::False},
    {"COMMON_CONTROL:control-def[127]", Old::False},     {"COMMON_CONTROL:control-def[131]", Old::False},
    {"COMMON_CONTROL:control-def[145]", Old::False},     {"KNIGHT_CONTROL:control-def[0]", Old::False},
    {"KNIGHT_CONTROL:control-def[2]", Old::False},       {"KNIGHT_CONTROL:control-def[5]", Old::False},
    {"KNIGHT_CONTROL:control-def[8]", Old::False},       {"KNIGHT_CONTROL:control-def[11]", Old::False},
    {"KNIGHT_CONTROL:control-def[14]", Old::False},      {"KNIGHT_CONTROL:control-def[17]", Old::False},
    {"KNIGHT_CONTROL:control-def[29]", Old::False},      {"KNIGHT_CONTROL:control-def[32]", Old::False},
    {"KNIGHT_CONTROL:control-def[35]", Old::False},      {"KNIGHT_CONTROL:control-def[38]", Old::False},
};

bool old_holds(Old d, int bit, const ScrWorld& w) {
    switch (d) {
        case Old::Mortality: return !(w.hp > 0.0f);
        case Old::Landing: return w.bossOnGround;
        case Old::NoGround: return !w.bossOnGround;
        case Old::Fail7: return bit == 6 && w.bossControlMode == 7;
        case Old::Fail8: return bit == 6 && w.bossControlMode == 8;
        case Old::Fail6: return bit == 6 && w.bossControlMode == 6;
        case Old::False: return false;
    }
    return false;
}
}  // namespace

TEST_CASE("scr_cdef_eval equals S12's control-def table on KNIGHT over the grid") {
    const ScrGraph& g = graph("KNIGHT");
    CHECK(std::size(kOldTable) == 30);
    for (const auto& [prefix, old] : kOldTable) {
        std::string key;
        for (const auto& [k, recs] : g.controlDefs) {
            if (k.rfind(std::string(prefix) + " ", 0) == 0 || k == prefix) key = k;
        }
        const std::string name = prefix;
        CAPTURE(name);
        REQUIRE(!key.empty());
        int mismatches = 0;
        for (const float hp : {1.0f, 0.0f, -1.0f}) {
            for (const bool ground : {true, false}) {
                for (int cm = 0; cm <= 12; ++cm) {
                    for (int bit = -1; bit < 64; ++bit) {
                        ScrWorld w;
                        w.hp = hp;
                        w.bossOnGround = ground;
                        w.bossControlMode = cm;
                        const std::vector<int> q = bit < 0 ? std::vector<int>{} : std::vector<int>{bit};
                        if (scr_cdef_eval(g, key, q, w, ScrRuntime{}) != old_holds(old, bit, w)) ++mismatches;
                    }
                }
            }
        }
        CHECK(mismatches == 0);
    }
}
