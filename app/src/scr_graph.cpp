#include "scr_graph.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

// --- A minimal JSON reader (the app has none outside cgltf's private one) ------

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;  // document order

    const Json* get(std::string_view key) const {
        for (const auto& [k, v] : object) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

struct JsonReader {
    std::string_view text;
    size_t at = 0;

    void skip() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at;
    }
    [[noreturn]] void error(const char* what) const {
        fail(std::string("JSON: ") + what + " at byte " + std::to_string(at));
    }
    char peek() {
        skip();
        if (at >= text.size()) error("unexpected end");
        return text[at];
    }
    void expect(char c) {
        if (peek() != c) error((std::string("expected '") + c + "'").c_str());
        ++at;
    }
    bool literal(std::string_view word) {
        if (text.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }

    static void put_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    std::string str() {
        expect('"');
        std::string out;
        while (true) {
            if (at >= text.size()) error("unterminated string");
            const char c = text[at++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= text.size()) error("unterminated escape");
            const char e = text[at++];
            switch (e) {
                case '"': case '\\': case '/': out += e; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (at + 4 > text.size()) error("short \\u escape");
                    // ponytail: a surrogate pair is encoded as two 3-byte units (labels only).
                    const std::string hex4(text.substr(at, 4));
                    put_utf8(out, static_cast<unsigned>(std::strtoul(hex4.c_str(), nullptr, 16)));
                    at += 4;
                    break;
                }
                default: error("bad escape");
            }
        }
    }

    Json value() {
        Json v;
        const char c = peek();
        if (c == '{') {
            v.kind = Json::Object;
            ++at;
            if (peek() == '}') return ++at, v;
            while (true) {
                std::string key = str();
                expect(':');
                v.object.emplace_back(std::move(key), value());
                if (peek() == '}') return ++at, v;
                expect(',');
            }
        }
        if (c == '[') {
            v.kind = Json::Array;
            ++at;
            if (peek() == ']') return ++at, v;
            while (true) {
                v.array.push_back(value());
                if (peek() == ']') return ++at, v;
                expect(',');
            }
        }
        if (c == '"') {
            v.kind = Json::String;
            v.string = str();
            return v;
        }
        if (literal("null")) return v;
        if (literal("true") || literal("false")) {
            v.kind = Json::Bool;
            v.boolean = text[at - 2] == 'u';  // tr[u]e / fal[s]e
            return v;
        }
        const size_t start = at;
        while (at < text.size() && std::string_view("+-0123456789.eE").find(text[at]) != std::string_view::npos) ++at;
        if (at == start) error("unexpected character");
        const std::string token(text.substr(start, at - start));
        char* end = nullptr;
        v.kind = Json::Number;
        v.number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) error("bad number");
        return v;
    }
};

// --- The export -> ScrGraph -----------------------------------------------------

struct Loader {
    const std::vector<std::string>& clipNames;
    const ScrRegistry& registry;
    ScrGraph& g;
    std::vector<std::string> condKeys, actKeys;

    static const Json& need(const Json& obj, const char* key, const std::string& where) {
        const Json* v = obj.kind == Json::Object ? obj.get(key) : nullptr;
        if (v == nullptr) fail(where + ": missing \"" + key + "\"");
        return *v;
    }
    static const Json* opt(const Json& obj, const char* key) {
        const Json* v = obj.kind == Json::Object ? obj.get(key) : nullptr;
        return v == nullptr || v->kind == Json::Null ? nullptr : v;
    }
    static double num(const Json& v, const std::string& where) {
        if (v.kind != Json::Number) fail(where + ": expected a number");
        return v.number;
    }
    static std::string text(const Json* v) { return v != nullptr && v->kind == Json::String ? v->string : ""; }
    static std::vector<float> floats(const Json* v, const std::string& where) {
        std::vector<float> out;
        if (v == nullptr) return out;
        if (v->kind != Json::Array) fail(where + ": expected an array of numbers");
        for (const Json& f : v->array) out.push_back(static_cast<float>(num(f, where)));
        return out;
    }

    static uint32_t hex(const Json& v, const std::string& where) {
        if (v.kind != Json::String || v.string.rfind("0x", 0) != 0) fail(where + ": expected a \"0x...\" fn");
        char* end = nullptr;
        const unsigned long n = std::strtoul(v.string.c_str() + 2, &end, 16);
        if (*end != '\0' || v.string.size() < 3) fail(where + ": bad fn \"" + v.string + "\"");
        return static_cast<uint32_t>(n);
    }

    static int index_of(const std::vector<std::string>& keys, const std::string& key) {
        const auto it = std::find(keys.begin(), keys.end(), key);
        return it == keys.end() ? -1 : static_cast<int>(it - keys.begin());
    }

    // "TABLE:C12" / "TABLE:F3" -> index; null -> -1; a dangling name is an error.
    int ref(const Json* v, const std::vector<std::string>& keys, const char* what, const std::string& where) const {
        if (v == nullptr || v->kind == Json::Null) return -1;
        if (v->kind != Json::String) fail(where + ": expected a " + what + " name");
        const int i = index_of(keys, v->string);
        if (i < 0) fail(where + ": unknown " + what + " " + v->string);
        return i;
    }

    int clip(const Json& motion, const std::string& where) const {
        const Json* name = opt(motion, "clip");
        if (name == nullptr) fail(where + ": motion " + text(motion.get("motion")) + " has no clip");
        const int i = index_of(clipNames, name->string);
        if (i < 0) fail(where + ": clip " + name->string + " is not in the clip names");
        return i;
    }
    int clip_opt(const Json* motion, const std::string& where) const {
        return motion == nullptr ? -1 : clip(*motion, where);
    }

    int mode_ref(const Json* v, const std::string& where) const {
        if (v == nullptr) return -1;
        const int m = static_cast<int>(num(need(*v, "mode", where), where));
        if (m < 0 || m >= static_cast<int>(g.modes.size())) {
            fail(where + ": mode " + std::to_string(m) + " out of range");
        }
        return m;
    }

    static int ctrl(const Json* v, const std::string& where) {
        if (v == nullptr) return -1;
        const int bit = static_cast<int>(num(need(*v, "bit", where), where));
        if (bit < 0 || bit > 63) fail(where + ": control bit " + std::to_string(bit) + " out of range");
        return bit;
    }

    ScrArg arg(const std::string& slot, const Json& v, const std::string& where) const {
        ScrArg a;
        a.slot = slot;
        const std::string at = where + " " + slot;
        switch (v.kind) {
            case Json::Null: a.null = true; break;
            case Json::Bool: a.num = v.boolean ? 1.0 : 0.0; break;
            case Json::Number: a.num = v.number; break;
            case Json::String: {
                const std::string& s = v.string;
                if (s.rfind("0x", 0) == 0) {
                    a.num = static_cast<double>(hex(v, at));
                } else if ((a.cond = index_of(condKeys, s)) >= 0 || (a.actions = index_of(actKeys, s)) >= 0) {
                    a.text = s;
                } else {
                    // A "TABLE:C<n>" / "TABLE:F<n>" the export does not carry is dangling.
                    const size_t colon = s.find(':');
                    if (colon != std::string::npos && colon + 2 < s.size() &&
                        (s[colon + 1] == 'C' || s[colon + 1] == 'F') &&
                        s.find_first_not_of("0123456789", colon + 2) == std::string::npos) {
                        fail(at + ": unknown reference " + s);
                    }
                    a.text = s;  // a handle the interpreter does not resolve
                }
                break;
            }
            case Json::Object:
                a.text = text(v.get("name"));
                if (v.get("clip") != nullptr) {
                    a.clip = clip(v, at);
                } else if (v.get("mode") != nullptr) {
                    a.mode = mode_ref(&v, at);
                } else if (v.get("bit") != nullptr) {
                    a.ctrl = ctrl(&v, at);
                    a.text = text(v.get("ctrl"));
                } else {
                    fail(at + ": unrecognised argument object");
                }
                break;
            case Json::Array: fail(at + ": unrecognised argument array");
        }
        return a;
    }

    std::vector<ScrArg> args(const Json& rec, const std::string& where) const {
        std::vector<ScrArg> out;
        const Json& a = need(rec, "args", where);
        if (a.kind != Json::Object) fail(where + ": args is not an object");
        for (const auto& [slot, v] : a.object) out.push_back(arg(slot, v, where));
        return out;
    }

    void load(const Json& doc) {
        if (text(doc.get("format")) != "sotc-scr-graph/0") fail("not a sotc-scr-graph/0 document");
        g.boss = text(doc.get("boss"));

        const Json& conds = need(doc, "conditions", "graph");
        const Json& acts = need(doc, "actions", "graph");
        const Json& modes = need(doc, "modes", "graph");
        if (conds.kind != Json::Object || acts.kind != Json::Object || modes.kind != Json::Array) {
            fail("graph: conditions/actions must be objects, modes an array");
        }
        for (const auto& [k, v] : conds.object) condKeys.push_back(k);
        for (const auto& [k, v] : acts.object) actKeys.push_back(k);
        g.modes.resize(modes.array.size());  // mode refs range-check against this

        for (const auto& [key, c] : conds.object) {
            ScrCondition cond;
            cond.key = key;
            cond.label = text(c.get("label"));
            const std::string op = text(&need(c, "op", key));
            if (op == "and") cond.op = ScrOp::And;
            else if (op == "or") cond.op = ScrOp::Or;
            else if (op == "false") cond.op = ScrOp::False;
            else fail(key + ": unknown combiner \"" + op + "\"");
            const Json& clauses = need(c, "clauses", key);
            if (clauses.kind != Json::Array) fail(key + ": clauses is not an array");
            for (const Json& cl : clauses.array) {
                ScrClause clause;
                clause.pred = text(cl.get("pred"));
                clause.fn = hex(need(cl, "fn", key), key);
                const Json& e = need(cl, "expect", key);
                clause.expect = e.kind == Json::Bool ? (e.boolean ? 1 : 0) : static_cast<int>(num(e, key));
                clause.args = args(cl, key);
                if (clause.fn == kScrSubFn) {
                    const ScrArg* sub = scr_arg(clause.args, "h50");
                    if (sub == nullptr || (!sub->null && sub->cond < 0)) fail(key + ": sub without a condition in h50");
                } else if (registry.predicates.find(clause.fn) == registry.predicates.end()) {
                    char fn[16];
                    std::snprintf(fn, sizeof fn, "0x%x", clause.fn);
                    fail(key + ": unknown predicate fn " + fn + " (" + clause.pred + ")");
                }
                cond.clauses.push_back(std::move(clause));
            }
            g.conditions.push_back(std::move(cond));
        }

        for (const auto& [key, list] : acts.object) {
            ScrActionList out;
            out.key = key;
            if (list.kind != Json::Array) fail(key + ": not an action list");
            for (const Json& r : list.array) {
                ScrAction a;
                a.act = text(r.get("act"));
                a.fn = hex(need(r, "fn", key), key);
                if (registry.actions.find(a.fn) == registry.actions.end()) {
                    char fn[16];
                    std::snprintf(fn, sizeof fn, "0x%x", a.fn);
                    fail(key + ": unknown action kind " + fn + " (" + a.act + ")");
                }
                a.args = args(r, key);
                out.records.push_back(std::move(a));
            }
            g.actions.push_back(std::move(out));
        }

        for (size_t i = 0; i < modes.array.size(); ++i) {
            const Json& m = modes.array[i];
            const std::string where = "mode " + std::to_string(i);
            ScrMode& mode = g.modes[i];
            mode.id = static_cast<int>(num(need(m, "id", where), where));
            if (mode.id != static_cast<int>(i)) fail(where + ": id " + std::to_string(mode.id) + " out of order");
            mode.name = text(m.get("name"));
            if (const Json* loco = opt(m, "locomotion")) {
                ScrLocomotion& l = mode.locomotion;
                l.row = text(loco->get("row"));
                if (const Json* k = opt(*loco, "kind")) l.kind = static_cast<int>(num(*k, where));
                if (const Json* p = opt(*loco, "p0c")) l.p0c = static_cast<int>(num(*p, where));
                l.attack = text(loco->get("attack"));
                l.attackTrigger = text(loco->get("attack_trigger"));
                l.attain = floats(opt(*loco, "attain"), where);
                l.perception = floats(opt(*loco, "perception"), where);
                l.gaze = text(loco->get("gaze"));
            }
            if (const Json* e = opt(m, "enter")) {
                mode.enterActions = ref(e->get("actions"), actKeys, "action list", where + " enter");
                mode.enterCtrl = ctrl(opt(*e, "ctrl"), where + " enter");
                mode.enterWaitClip = clip_opt(opt(*e, "wait_motion"), where + " enter");
            }
            if (const Json* u = opt(m, "update")) {
                mode.updateActions = ref(u->get("actions"), actKeys, "action list", where + " update");
                mode.updateCtrl = ctrl(opt(*u, "ctrl"), where + " update");
            }
            if (const Json* x = opt(m, "exit")) {
                mode.exitActions = ref(x->get("actions"), actKeys, "action list", where + " exit");
                if (const Json* c = opt(*x, "ctrl_or_demo")) {
                    if (c->kind == Json::String) mode.exitDemo = c->string;  // a scr_demo_def
                    else mode.exitCtrl = ctrl(c, where + " exit");
                }
                mode.exitWaitClip = clip_opt(opt(*x, "wait_motion"), where + " exit");
            }
            const Json& rows = need(m, "transitions", where);
            if (rows.kind != Json::Array) fail(where + ": transitions is not an array");
            for (const Json& r : rows.array) {
                ScrTransition t;
                t.cond = ref(r.get("when"), condKeys, "condition", where + " transition");
                t.to = static_cast<int>(num(need(r, "to", where), where));
                if (t.to < 0 || t.to >= static_cast<int>(modes.array.size())) {
                    fail(where + ": transition to " + std::to_string(t.to) + " out of range");
                }
                if (const Json* s = opt(r, "min_since_transition")) {
                    t.minSinceTransition = static_cast<float>(num(*s, where));
                }
                mode.transitions.push_back(t);
            }
        }

        const Json& root = need(doc, "root", "graph");
        g.root.startMode = mode_ref(opt(root, "start_mode"), "root start_mode");
        g.root.retryStartMode = mode_ref(opt(root, "retry_start_mode"), "root retry_start_mode");
        g.root.retryCtrl = ctrl(opt(root, "retry_ctrl"), "root retry_ctrl");
        g.root.deathMode = mode_ref(opt(root, "death_mode"), "root death_mode");
        if (const Json* d = opt(root, "death_control_mode")) {
            // "KNIGHT:control-mode[10]" -> 10
            const size_t open = d->string.rfind('[');
            if (d->kind != Json::String || open == std::string::npos) {
                fail("root death_control_mode: expected \"T:control-mode[n]\"");
            }
            g.root.deathControlMode = std::atoi(d->string.c_str() + open + 1);
        }
        g.root.callbackActions = ref(root.get("callback_actions"), actKeys, "action list", "root callbacks");
        g.root.everyFrameActions = ref(root.get("every_frame_actions"), actKeys, "action list", "root every frame");
        g.root.anger = floats(opt(root, "anger"), "root anger");

        const Json& cms = need(doc, "control_modes", "graph");
        if (cms.kind != Json::Object) fail("graph: control_modes is not an object keyed by index");
        g.controlModes.resize(cms.object.size());
        std::vector<bool> seen(cms.object.size(), false);
        for (const auto& [key, cm] : cms.object) {
            char* end = nullptr;
            const long i = std::strtol(key.c_str(), &end, 10);
            if (*end != '\0' || key.empty() || i < 0 || i >= static_cast<long>(seen.size()) || seen[i]) {
                fail("control_modes: key \"" + key + "\" is not a dense index");
            }
            seen[i] = true;
            const std::string where = "control_mode " + key;
            ScrControlMode& out = g.controlModes[i];
            out.name = text(cm.get("name"));
            const Json& assign = need(cm, "assign", where);
            if (assign.kind != Json::Array) fail(where + ": assign is not an array");
            for (const Json& a : assign.array) {
                ScrAssign row;
                row.layer = text(a.get("layer"));
                row.when = text(a.get("when"));
                const Json* c = opt(a, "ctrl");
                row.ctrl = ctrl(c, where);
                row.ctrlName = c == nullptr ? "" : text(c->get("ctrl"));
                row.orient = text(a.get("orient"));
                if (const Json* n = opt(a, "next_control_mode")) row.nextControlMode = static_cast<int>(num(*n, where));
                out.assign.push_back(std::move(row));
            }
        }

        const Json& edges = need(doc, "motion_graph", "graph");
        if (edges.kind != Json::Array) fail("graph: motion_graph is not an array");
        for (const Json& e : edges.array) {
            ScrMotionEdge edge;
            edge.table = text(e.get("table"));
            edge.orient = text(e.get("orient"));
            for (auto [key, slot] : {std::pair{"from", &edge.from}, std::pair{"to", &edge.to}}) {
                const Json* name = opt(e, key);
                if (name == nullptr) continue;
                *slot = index_of(clipNames, name->string);
                if (*slot < 0) fail("motion_graph: clip " + name->string + " is not in the clip names");
            }
            if (const Json* p = opt(e, "p30")) edge.p30 = static_cast<float>(num(*p, "motion_graph"));
            if (const Json* p = opt(e, "p34")) edge.p34 = static_cast<float>(num(*p, "motion_graph"));
            g.motionGraph.push_back(std::move(edge));
        }
    }
};

}  // namespace

const ScrArg* scr_arg(const std::vector<ScrArg>& args, const char* slot) {
    for (const ScrArg& a : args) {
        if (a.slot == slot) return &a;
    }
    return nullptr;
}

bool scr_graph_load(const std::string& json, const std::vector<std::string>& clipNames,
                    const ScrRegistry& registry, ScrGraph& out, std::string& error) {
    try {
        JsonReader reader{json};
        const Json doc = reader.value();
        reader.skip();
        if (reader.at != json.size()) reader.error("trailing data");
        ScrGraph graph;
        Loader{clipNames, registry, graph, {}, {}}.load(doc);
        out = std::move(graph);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

void scr_runtime_start(const ScrGraph& graph, ScrRuntime& rt, bool retry) {
    rt = ScrRuntime{};
    rt.pending = retry ? graph.root.retryStartMode : graph.root.startMode;
}

// FUN_0082d9f0 (§2): the combiner is the first record's; each clause holds when its
// (u8) result equals its expect; AND fails on the first mismatch, OR passes on the
// first match.
bool scr_eval(const ScrGraph& graph, const ScrRegistry& registry, int cond, const ScrRuntime& rt,
              const ScrWorld& world) {
    if (cond < 0) return true;  // a null handle
    const ScrCondition& c = graph.conditions[cond];
    if (c.op == ScrOp::False) return false;
    for (const ScrClause& clause : c.clauses) {
        bool result;
        if (clause.fn == kScrSubFn) {
            result = scr_eval(graph, registry, scr_arg(clause.args, "h50")->cond, rt, world);
        } else {
            result = registry.predicates.at(clause.fn)(clause, world, rt);
        }
        const bool holds = (result ? 1 : 0) == clause.expect;
        if (c.op == ScrOp::And && !holds) return false;
        if (c.op == ScrOp::Or && holds) return true;
    }
    return c.op == ScrOp::And;
}

void scr_run(ScrCtx& ctx, int actions) {
    if (actions < 0) return;
    for (const ScrAction& a : ctx.graph.actions[actions].records) ctx.registry.actions.at(a.fn)(a, ctx);
}

void scr_request(ScrStepOut& out, int bit) {
    if (bit < 0 || std::find(out.controls.begin(), out.controls.end(), bit) != out.controls.end()) return;
    out.controls.push_back(bit);
}

// FUN_0082c080 (§1). Steps 2-5 (perception, anger, locomotion, the move target, the
// queued control) and 12 (the attack arm) belong to the boss's own code (Task 5),
// which reads ScrStepOut::modeChanged for the new mode's slowstatus.
ScrStepOut scr_step(const ScrGraph& graph, const ScrRegistry& registry, ScrRuntime& rt, const ScrWorld& world,
                    float dt) {
    ScrStepOut out;
    ScrCtx ctx{graph, registry, rt, world, out};
    rt.transitioned = false;  // 1.

    // 6. Forced death: HP <= 0 in the root's death control-mode, not yet in the death mode.
    const bool forced = graph.root.deathMode >= 0 && graph.root.deathControlMode >= 0 &&
                        world.bossControlMode == graph.root.deathControlMode &&
                        rt.current != graph.root.deathMode && !(world.hp > 0.0f);
    if (forced) {
        rt.pending = graph.root.deathMode;
        rt.entered = true;
    }

    // 7. Entry gate: entered once the boss plays the mode's +0x60 motion; until then
    // re-request its +0x50 control.
    if (!rt.entered && rt.current >= 0) {
        const ScrMode& m = graph.modes[rt.current];
        if (world.bossMotion == m.enterWaitClip) rt.entered = true;
        else scr_request(out, m.enterCtrl);
    }

    // 8. The transition walk: first row whose minimum <= timer 2 and whose condition holds.
    bool fired = false;
    if (!forced && rt.entered && rt.current >= 0) {
        for (const ScrTransition& t : graph.modes[rt.current].transitions) {
            if (t.minSinceTransition <= rt.timer2 && scr_eval(graph, registry, t.cond, rt, world)) {
                rt.pending = t.to;
                fired = true;
                break;
            }
        }
    }

    // 9. The switch, behind the old mode's exit gate.
    rt.exitDeferred = false;
    if ((rt.current != rt.pending || fired) && rt.pending >= 0) {
        if (rt.current >= 0) {
            const ScrMode& old = graph.modes[rt.current];
            scr_request(out, old.exitCtrl);  // a demo exit (FUN_00836520) is not played
            rt.exitDeferred = !forced && old.exitWaitClip >= 0 && world.bossMotion != old.exitWaitClip;
            if (!rt.exitDeferred) scr_run(ctx, old.exitActions);
        }
        if (!rt.exitDeferred) {
            const int next = rt.pending;
            const ScrMode& m = graph.modes[next];
            scr_run(ctx, m.enterActions);
            if (m.enterCtrl >= 0) {
                scr_request(out, m.enterCtrl);
                if (m.enterWaitClip >= 0) rt.entered = false;
            }
            if (rt.current != next) rt.timer1 = 0.0f;
            rt.timer2 = 0.0f;
            rt.previous = rt.current;
            rt.current = next;
            rt.transitioned = true;
            out.modeChanged = true;
        }
    }

    // 10. Update: the current mode's actions, then the pending mode's control (the
    // same mode unless an exit is deferred).
    if (rt.entered && rt.current >= 0) {
        const int updateCtrl = graph.modes[rt.pending].updateCtrl;
        scr_run(ctx, graph.modes[rt.current].updateActions);
        scr_request(out, updateCtrl);
    }

    // 11. The root's every-frame actions.
    scr_run(ctx, graph.root.everyFrameActions);

    // 13. Timers.
    rt.timer1 += dt;
    rt.timer2 += dt;
    out.mode = rt.current;
    out.deathForced = forced;
    return out;
}
