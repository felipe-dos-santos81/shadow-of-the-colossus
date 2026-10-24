#include "objanim_events.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "collision.h"
#include "json_reader.h"

namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

// --- The sidecar -> EventsFile ----------------------------------------------------

const Json& need(const Json& obj, const char* key, const std::string& where) {
    const Json* v = obj.kind == Json::Object ? obj.get(key) : nullptr;
    if (v == nullptr) fail(where + ": missing \"" + key + "\"");
    return *v;
}

const Json* opt(const Json& obj, const char* key) {
    const Json* v = obj.kind == Json::Object ? obj.get(key) : nullptr;
    return v == nullptr || v->kind == Json::Null ? nullptr : v;
}

float num(const Json& v, const std::string& where) {
    if (v.kind != Json::Number) fail(where + ": expected a number");
    return static_cast<float>(v.number);
}

std::string text(const Json& v, const std::string& where) {
    if (v.kind != Json::String) fail(where + ": expected a string");
    return v.string;
}

const Json& array(const Json& v, size_t size, const std::string& where) {
    if (v.kind != Json::Array || (size != 0 && v.array.size() != size)) {
        fail(where + ": expected an array" + (size != 0 ? " of " + std::to_string(size) : ""));
    }
    return v;
}

Vec3 vec3(const Json& v, const std::string& where) {
    const Json& a = array(v, 3, where);
    return Vec3{num(a.array[0], where), num(a.array[1], where), num(a.array[2], where)};
}

std::vector<std::pair<int, int>> pairs(const Json& v, const std::string& where) {
    std::vector<std::pair<int, int>> out;
    for (const Json& p : array(v, 0, where).array) {
        const Json& a = array(p, 2, where);
        out.emplace_back(static_cast<int>(num(a.array[0], where)), static_cast<int>(num(a.array[1], where)));
    }
    return out;
}

EventsParent parent_of(const Json& v, const std::string& where) {
    EventsParent p;
    if (v.kind == Json::String) {
        if (v.string == "entity") p.kind = EventsParent::Entity;
        else if (v.string == "entityRoot") p.kind = EventsParent::EntityRoot;
        else if (v.string == "skelton") p.kind = EventsParent::Skelton;
        else if (v.string == "spawn") p.kind = EventsParent::Spawn;
        else fail(where + ": unknown parent \"" + v.string + "\"");
    } else if (const Json* bone = opt(v, "bone")) {
        p.kind = EventsParent::Bone;
        p.bone = text(*bone, where);
    } else if (const Json* k = opt(v, "optBone")) {
        p.kind = EventsParent::OptBone;
        p.optBone = static_cast<int>(num(*k, where));
    } else {
        fail(where + ": unknown parent");
    }
    return p;
}

EventsKey key_of(const Json& v, int frame, const char* vecName, const std::string& where) {
    EventsKey k;
    k.f = frame;
    k.pos = vec3(need(v, "pos", where), where);
    const Json& q = array(need(v, "quat", where), 4, where);
    k.quat = Quat{num(q.array[0], where), num(q.array[1], where), num(q.array[2], where), num(q.array[3], where)};
    k.size = vec3(need(v, vecName, where), where);
    return k;
}

// `static` -> one key (f -1); `keys` -> one per frame, ascending.
std::vector<EventsKey> keys_of(const Json& row, const char* vecName, const std::string& where) {
    std::vector<EventsKey> out;
    if (const Json* s = opt(row, "static")) {
        out.push_back(key_of(*s, -1, vecName, where));
    } else if (const Json* keys = opt(row, "keys")) {
        for (const Json& k : array(*keys, 0, where).array) {
            out.push_back(key_of(k, static_cast<int>(num(need(k, "f", where), where)), vecName, where));
        }
        std::sort(out.begin(), out.end(), [](const EventsKey& a, const EventsKey& b) { return a.f < b.f; });
    }
    if (out.empty()) fail(where + ": no pose (static or keys)");
    return out;
}

EventsVolume volume_of(const Json& row, bool damage, const std::string& at) {
    EventsVolume v;
    v.name = text(need(row, "name", at), at);
    const std::string where = at + " " + v.name;
    const std::string shape = text(need(row, "shape", where), where);
    if (shape == "box") v.shape = EventsVolume::Box;
    else if (shape == "cylinder") v.shape = EventsVolume::Cylinder;
    else if (damage && shape == "sphere") v.shape = EventsVolume::Sphere;
    else if (!damage && shape == "ellipsoid") v.shape = EventsVolume::Ellipsoid;
    else fail(where + ": unknown shape \"" + shape + "\"");
    v.parent = parent_of(need(row, "parent", where), where);
    v.keys = keys_of(row, "size", where);
    if (damage) {
        v.windows = pairs(need(row, "windows", where), where);
        if (const Json* def = opt(row, "def")) {
            v.hasDef = true;
            v.amount = num(need(*def, "amount", where), where);
            v.reaction = num(need(*def, "reaction", where), where);
        }
    } else {
        if (const Json* inner = opt(row, "inner")) v.inner = num(*inner, where);
        if (const Json* angle = opt(row, "angle")) v.angle = num(*angle, where);
    }
    return v;
}

std::vector<EventsVolume> volumes_of(const Json* list, bool damage, const std::string& where) {
    std::vector<EventsVolume> out;
    if (list == nullptr) return out;
    for (const Json& row : array(*list, 0, where).array) out.push_back(volume_of(row, damage, where));
    // EventsClipState::hit / EventsSpawnRunner::Instance::hit hold one bit per row.
    if (damage && out.size() > 64) fail(where + ": more than 64 damage rows");
    return out;
}

void load(const Json& doc, EventsFile& file) {
    const std::string format = text(need(doc, "format", "sidecar"), "sidecar");
    if (format != "sotc-objanim-events/1") fail("sidecar: format \"" + format + "\" is not sotc-objanim-events/1");
    const std::string frame = text(need(doc, "frame", "sidecar"), "sidecar");
    if (frame != "glb") fail("sidecar: frame \"" + frame + "\" is not glb");
    file.skeleton = text(need(doc, "skeleton", "sidecar"), "sidecar");
    file.judgeDefaults = volumes_of(opt(doc, "judgeDefaults"), false, "judgeDefaults");

    if (const Json* spawned = opt(doc, "spawned")) {
        if (spawned->kind != Json::Object) fail("sidecar: spawned is not an object");
        for (const auto& [name, s] : spawned->object) {
            const std::string where = "spawned " + name;
            EventsSpawned out;
            out.name = name;
            out.fps = num(need(s, "fps", where), where);
            out.frames = static_cast<int>(num(need(s, "frames", where), where));
            out.damage = volumes_of(opt(s, "damage"), true, where);
            file.spawned.push_back(std::move(out));
        }
    }

    const Json& clips = need(doc, "clips", "sidecar");
    if (clips.kind != Json::Object) fail("sidecar: clips is not an object");
    for (const auto& [name, c] : clips.object) {
        const std::string where = "clip " + name;
        EventsClip clip;
        clip.name = name;
        clip.fps = num(need(c, "fps", where), where);
        clip.frames = static_cast<int>(num(need(c, "frames", where), where));
        clip.damage = volumes_of(opt(c, "damage"), true, where);
        clip.judge = volumes_of(opt(c, "judge"), false, where);
        if (const Json* effects = opt(c, "effects")) {
            for (const Json& row : array(*effects, 0, where).array) {
                EventsEffect e;
                e.name = text(need(row, "name", where), where);
                const std::string at = where + " " + e.name;
                e.parent = parent_of(need(row, "parent", at), at);
                e.events = pairs(need(row, "events", at), at);
                e.keys = keys_of(row, "vec", at);
                if (const Json* spawns = opt(row, "spawns")) {
                    for (const Json& s : array(*spawns, 0, at).array) {
                        const int index = events_spawned(file, text(s, at));
                        if (index < 0) fail(at + ": spawns \"" + s.string + "\", which the sidecar does not carry");
                        e.spawns.push_back(index);
                    }
                }
                clip.effects.push_back(std::move(e));
            }
        }
        file.clips.push_back(std::move(clip));
    }
}

// --- Posing -----------------------------------------------------------------------

Vec3 add(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(Vec3 a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
Vec3 lerp(Vec3 a, Vec3 b, float t) { return add(a, scale(sub(b, a), t)); }
float length(Vec3 a) { return std::sqrt(vec3_dot(a, a)); }

// The key at `frame`: lerped between the keys of the two whole frames around it,
// else the last key at or before it (else the first).
EventsKey sample(const std::vector<EventsKey>& keys, float frame) {
    const int f = static_cast<int>(std::floor(frame));
    const auto next = std::upper_bound(keys.begin(), keys.end(), f,
                                       [](int value, const EventsKey& k) { return value < k.f; });
    if (next == keys.begin()) return keys.front();
    const EventsKey& k0 = *(next - 1);
    if (next == keys.end() || k0.f != f || next->f != f + 1) return k0;
    const float t = frame - static_cast<float>(f);
    return EventsKey{f, lerp(k0.pos, next->pos, t), quat_slerp(k0.quat, next->quat, t), lerp(k0.size, next->size, t)};
}

Mat4 joint_world(const PoseFrame& pose, std::string_view name) {
    Mat4 out = mat4_identity();
    if (!pose.joint || !pose.joint(name, out)) fail("events: no joint " + std::string(name) + " in the pose");
    return out;
}

// The R4 pose space (see the header): the row's world matrix.
Mat4 pose_matrix(const EventsParent& parent, const EventsKey& key, const PoseFrame& pose,
                 const std::vector<std::string>& optBones) {
    Vec3 pos = key.pos;
    Mat4 base = pose.entity;
    switch (parent.kind) {
        case EventsParent::Bone:
            base = joint_world(pose, parent.bone);
            break;
        case EventsParent::OptBone:
            if (parent.optBone < 0 || parent.optBone >= static_cast<int>(optBones.size())) {
                fail("events: no OptBone " + std::to_string(parent.optBone) + " in the pose");
            }
            base = joint_world(pose, optBones[parent.optBone]);
            break;
        case EventsParent::Skelton:
            // R4: (x, hipY + y, -z) of the raw value = R(180° about y) of the mirrored one;
            // the rotation keeps the sidecar quat (D6: sectors open forward, a guess).
            pos = Vec3{-pos.x, pose.hipY + pos.y, -pos.z};
            break;
        case EventsParent::Entity:
        case EventsParent::EntityRoot:  // not in R4 (Gaius has none): read as the entity
        case EventsParent::Spawn:       // the spawn transform rides in pose.entity
            break;
    }
    return mat4_mul(base, mat4_from_trs(pos, key.quat, Vec3{1.0f, 1.0f, 1.0f}));
}

bool row_active(const EventsVolume& row, float frame) {
    const int f = static_cast<int>(std::floor(frame));
    return std::any_of(row.windows.begin(), row.windows.end(),
                       [f](const std::pair<int, int>& w) { return w.first <= f && f <= w.second; });
}

Vec3 local(const PosedVolume& v, Vec3 p) {
    const Vec3 d = sub(p, v.centre);
    return Vec3{vec3_dot(d, v.axis[0]), vec3_dot(d, v.axis[1]), vec3_dot(d, v.axis[2])};
}

// Distance from `p` to a damage volume (0 inside).
float distance(EventsVolume::Shape shape, const PosedVolume& v, Vec3 p) {
    const Vec3 l = local(v, p);
    const Vec3 s = v.size;
    switch (shape) {
        case EventsVolume::Sphere:
            return std::max(length(l) - s.x, 0.0f);
        case EventsVolume::Box:
            return length(Vec3{std::max(std::fabs(l.x) - s.x, 0.0f), std::max(std::fabs(l.y) - s.y, 0.0f),
                               std::max(std::fabs(l.z) - s.z, 0.0f)});
        case EventsVolume::Cylinder:
        case EventsVolume::Ellipsoid: {
            // ponytail: an elliptic section's radial distance is scaled by its smaller
            // radius (exact for the circular sections the data uses).
            const float rho = std::sqrt((l.x / s.x) * (l.x / s.x) + (l.z / s.z) * (l.z / s.z));
            const float dr = std::max(rho - 1.0f, 0.0f) * std::min(s.x, s.z);
            const float dy = std::max(std::fabs(l.y) - s.y, 0.0f);
            return std::sqrt(dr * dr + dy * dy);
        }
    }
    return 0.0f;
}

// The player's capsule (collision.h) against a damage volume: the distance to the
// axis segment's closest point (convex in t: a ternary search) within the radius.
bool hits_capsule(EventsVolume::Shape shape, const PosedVolume& v, Vec3 feet) {
    const auto at = [&](float t) {
        return distance(shape, v, Vec3{feet.x, feet.y + t * kPlayerCapsuleHeight, feet.z});
    };
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i) {
        const float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
        if (at(m1) <= at(m2)) {
            hi = m2;
        } else {
            lo = m1;
        }
    }
    return at(0.5f * (lo + hi)) <= kPlayerCapsuleRadius;
}

// The row swept from `from` to `to` in sub-steps of ≤ kEventsSweepStep of travel
// (the farthest-moving of its centre and box corners). `from` was tested last frame.
bool swept_hit(EventsVolume::Shape shape, const PosedVolume& from, const PosedVolume& to, Vec3 feet) {
    float travel = length(sub(to.centre, from.centre));
    for (int c = 0; c < 8; ++c) {
        const auto corner = [c](const PosedVolume& v) {
            Vec3 p = v.centre;
            const float sizes[3] = {v.size.x, v.size.y, v.size.z};
            for (int a = 0; a < 3; ++a) p = add(p, scale(v.axis[a], (c >> a & 1) ? sizes[a] : -sizes[a]));
            return p;
        };
        travel = std::max(travel, length(sub(corner(to), corner(from))));
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(travel / kEventsSweepStep)));
    for (int s = 1; s <= steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        PosedVolume v;
        v.centre = lerp(from.centre, to.centre, t);
        v.size = lerp(from.size, to.size, t);
        // Lerped axes, re-orthonormalised (a frame's turn is small: no slerp needed).
        v.axis[0] = vec3_normalize(lerp(from.axis[0], to.axis[0], t));
        const Vec3 y = lerp(from.axis[1], to.axis[1], t);
        v.axis[1] = vec3_normalize(sub(y, scale(v.axis[0], vec3_dot(y, v.axis[0]))));
        v.axis[2] = vec3_cross(v.axis[0], v.axis[1]);
        if (hits_capsule(shape, v, feet)) return true;
    }
    return false;
}

bool on_entity(const PlayerView& player, int entity) {
    return entity >= 0 && (player.attachedTo == entity || player.standingOn == entity);
}

void sort_hits(std::vector<DamageHit>& hits) {
    std::stable_sort(hits.begin(), hits.end(),
                     [](const DamageHit& a, const DamageHit& b) { return a.amount > b.amount; });
}

// The engine's judge point tests (FUN_0065ed70; recon objanim/py/volumes.py, 1:1).
bool judge_contains(const EventsVolume& row, const PosedVolume& v, Vec3 p) {
    const Vec3 l = local(v, p);
    const Vec3 s = v.size;
    if (row.shape == EventsVolume::Box) {
        return std::fabs(l.x) <= s.x && std::fabs(l.y) <= s.y && std::fabs(l.z) <= s.z;  // half extents
    }
    const float nx = l.x / s.x, ny = l.y / s.y, nz = l.z / s.z;
    float r2 = nx * nx + nz * nz;
    if (row.shape == EventsVolume::Ellipsoid) {
        r2 += ny * ny;
    } else if (std::fabs(ny) >= 1.0f) {
        return false;
    }
    if (!(r2 <= 1.0f && row.inner * row.inner <= r2)) return false;
    // The sector about local +z; 1.19e-7 = FLT_EPSILON, the binary's guard.
    return std::cos(row.angle * 0.5f) < (r2 > 1.19e-7f ? nz / std::sqrt(r2) : 0.0f);
}

}  // namespace

int events_clip(const EventsFile& file, std::string_view name) {
    for (size_t i = 0; i < file.clips.size(); ++i) {
        if (file.clips[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int events_spawned(const EventsFile& file, std::string_view name) {
    for (size_t i = 0; i < file.spawned.size(); ++i) {
        if (file.spawned[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

bool events_load(const std::string& path, EventsFile& out, std::string& error) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) fail(path + ": cannot open");
        std::stringstream buffer;
        buffer << in.rdbuf();
        EventsFile file;
        load(json_parse(buffer.str()), file);
        out = std::move(file);
        return true;
    } catch (const std::exception& e) {
        error = path + ": " + e.what();
        return false;
    }
}

PosedVolume events_pose(const EventsVolume& row, float frame, const PoseFrame& pose,
                        const std::vector<std::string>& optBones) {
    const EventsKey key = sample(row.keys, frame);
    const Mat4 m = pose_matrix(row.parent, key, pose, optBones);
    PosedVolume v;
    v.centre = Vec3{m.m[12], m.m[13], m.m[14]};
    for (int a = 0; a < 3; ++a) v.axis[a] = vec3_normalize(Vec3{m.m[a * 4], m.m[a * 4 + 1], m.m[a * 4 + 2]});
    v.size = key.size;
    return v;
}

std::vector<DamageHit> events_damage(const EventsFile& file, const EventsClipState& now,
                                     const EventsClipState& prev, const PoseView& pose,
                                     const PlayerView& player) {
    std::vector<DamageHit> hits;
    if (now.clip < 0 || on_entity(player, pose.entity)) return hits;  // §13.5
    const bool sameInstance = prev.clip == now.clip && prev.instance == now.instance;
    const std::vector<EventsVolume>& rows = file.clips[now.clip].damage;
    for (size_t i = 0; i < rows.size(); ++i) {
        const EventsVolume& row = rows[i];
        if (!row.hasDef || (now.hit >> i & 1) != 0 || !row_active(row, now.frame)) continue;
        const PosedVolume to = events_pose(row, now.frame, pose.now, pose.optBones);
        const PosedVolume from = sameInstance && row_active(row, prev.frame)
                                     ? events_pose(row, prev.frame, pose.prev, pose.optBones)
                                     : to;
        if (swept_hit(row.shape, from, to, player.feet)) {
            hits.push_back(DamageHit{row.name, static_cast<int>(i), row.amount, row.reaction});
        }
    }
    sort_hits(hits);
    return hits;
}

std::vector<EventFire> events_fired(const EventsFile& file, const EventsClipState& now,
                                    const EventsClipState& prev, const PoseView& pose) {
    std::vector<EventFire> fired;
    if (now.clip < 0) return fired;
    const bool same = prev.clip == now.clip && prev.instance == now.instance;
    const bool wrap = prev.clip == now.clip && prev.instance + 1 == now.instance;
    const std::vector<EventsEffect>& rows = file.clips[now.clip].effects;
    for (size_t i = 0; i < rows.size(); ++i) {
        for (const auto& [start, end] : rows[i].events) {
            const float s = static_cast<float>(start);
            const bool crossed = same   ? prev.frame < s && s <= now.frame
                                 : wrap ? prev.frame < s || s <= now.frame
                                        : s <= now.frame;
            if (!crossed) continue;
            // The key exported for this start (one per event start), else the static one.
            const auto key = std::find_if(rows[i].keys.begin(), rows[i].keys.end(),
                                          [start](const EventsKey& k) { return k.f == start; });
            const EventsKey& k = key != rows[i].keys.end() ? *key : rows[i].keys.front();
            fired.push_back(EventFire{rows[i].name, now.clip, static_cast<int>(i),
                                      pose_matrix(rows[i].parent, k, pose.now, pose.optBones)});
        }
    }
    return fired;
}

void EventsSpawnRunner::fire(const EventsFile& file, const std::vector<EventFire>& fired) {
    for (const EventFire& f : fired) {
        for (int spawned : file.clips[f.clip].effects[f.row].spawns) active.push_back(Instance{spawned, f.at});
    }
}

void EventsSpawnRunner::step(const EventsFile& file, float dt) {
    for (Instance& i : active) i.frame += dt * file.spawned[i.spawned].fps;
    std::erase_if(active, [&](const Instance& i) { return i.frame >= file.spawned[i.spawned].frames; });
}

std::vector<DamageHit> EventsSpawnRunner::damage(const EventsFile& file, const PlayerView& player, int entity) {
    std::vector<DamageHit> hits;
    if (on_entity(player, entity)) return hits;  // §13.5
    for (Instance& inst : active) {
        PoseFrame at;
        at.entity = inst.at;
        const std::vector<EventsVolume>& rows = file.spawned[inst.spawned].damage;
        for (size_t i = 0; i < rows.size(); ++i) {
            const EventsVolume& row = rows[i];
            if (!row.hasDef || (inst.hit >> i & 1) != 0 || !row_active(row, inst.frame)) continue;
            // The spawn does not move: no sweep.
            if (hits_capsule(row.shape, events_pose(row, inst.frame, at, {}), player.feet)) {
                inst.hit |= uint64_t{1} << i;
                hits.push_back(DamageHit{row.name, static_cast<int>(i), row.amount, row.reaction});
            }
        }
    }
    sort_hits(hits);
    return hits;
}

bool events_player_in_judge(const EventsFile& file, std::string_view name, const EventsClipState& now,
                            const PoseView& pose, const PlayerView& player) {
    const auto holds = [&](const EventsVolume& row, float frame) {
        return row.name == name &&
               judge_contains(row, events_pose(row, frame, pose.now, pose.optBones), player.feet);
    };
    if (now.clip >= 0) {
        for (const EventsVolume& row : file.clips[now.clip].judge) {
            if (holds(row, now.frame)) return true;
        }
    }
    return std::any_of(file.judgeDefaults.begin(), file.judgeDefaults.end(),
                       [&](const EventsVolume& row) { return holds(row, 0.0f); });
}
