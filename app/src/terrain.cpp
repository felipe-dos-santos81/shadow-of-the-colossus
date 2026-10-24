#include "terrain.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "json_reader.h"

namespace {

using Tri = TerrainMap::Tri;
using Grid = TerrainMap::Grid;

constexpr float kEdgeSlack = 1e-5f;  // authored: barycentric slack so a point on a shared edge hits

void build_grid(const std::vector<Tri>& tris, Grid& g) {
    g = Grid{};
    if (tris.empty()) return;
    float maxX = -1e30f, maxZ = -1e30f;
    g.minX = g.minZ = 1e30f;
    for (const Tri& t : tris) {
        for (const auto& v : t.v) {
            g.minX = std::min(g.minX, v[0]);
            maxX = std::max(maxX, v[0]);
            g.minZ = std::min(g.minZ, v[2]);
            maxZ = std::max(maxZ, v[2]);
        }
    }
    g.nx = static_cast<int>((maxX - g.minX) / TerrainMap::kCell) + 1;
    g.nz = static_cast<int>((maxZ - g.minZ) / TerrainMap::kCell) + 1;
    g.cells.assign(static_cast<size_t>(g.nx) * g.nz, {});
    for (uint32_t i = 0; i < tris.size(); ++i) {
        float lx = 1e30f, hx = -1e30f, lz = 1e30f, hz = -1e30f;
        for (const auto& v : tris[i].v) {
            lx = std::min(lx, v[0]);
            hx = std::max(hx, v[0]);
            lz = std::min(lz, v[2]);
            hz = std::max(hz, v[2]);
        }
        const int x0 = static_cast<int>((lx - g.minX) / TerrainMap::kCell);
        const int x1 = static_cast<int>((hx - g.minX) / TerrainMap::kCell);
        const int z0 = static_cast<int>((lz - g.minZ) / TerrainMap::kCell);
        const int z1 = static_cast<int>((hz - g.minZ) / TerrainMap::kCell);
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) g.cells[static_cast<size_t>(z) * g.nx + x].push_back(i);
        }
    }
}

// The triangle's surface height under (x, z); false when (x, z) is outside it or the
// triangle is vertical (no area in plan).
bool surface_y(const Tri& t, float x, float z, float& y) {
    const float ax = t.v[0][0], az = t.v[0][2], bx = t.v[1][0], bz = t.v[1][2];
    const float cx = t.v[2][0], cz = t.v[2][2];
    const float det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    if (std::fabs(det) < 1e-9f) return false;
    const float l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / det;
    const float l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / det;
    const float l3 = 1.0f - l1 - l2;
    if (l1 < -kEdgeSlack || l2 < -kEdgeSlack || l3 < -kEdgeSlack) return false;
    y = l1 * t.v[0][1] + l2 * t.v[1][1] + l3 * t.v[2][1];
    return true;
}

// The highest triangle surface under p inside [lo, hi]; nullptr when none.
const Tri* highest(const std::vector<Tri>& tris, const Grid& g, Vec3 p, float lo, float hi,
                   float& outY) {
    if (g.cells.empty()) return nullptr;
    const int cx = static_cast<int>(std::floor((p.x - g.minX) / TerrainMap::kCell));
    const int cz = static_cast<int>(std::floor((p.z - g.minZ) / TerrainMap::kCell));
    if (cx < 0 || cz < 0 || cx >= g.nx || cz >= g.nz) return nullptr;
    const Tri* best = nullptr;
    for (uint32_t i : g.cells[static_cast<size_t>(cz) * g.nx + cx]) {
        float y;
        if (!surface_y(tris[i], p.x, p.z, y) || y < lo || y > hi) continue;
        if (!best || y > outY) {
            best = &tris[i];
            outY = y;
        }
    }
    return best;
}

float num(const Json& j) {
    if (j.kind != Json::Number) throw std::runtime_error("terrain: expected a number");
    return static_cast<float>(j.number);
}

// A JSON integer in [0, max], kept as a double until the cast (masks must not pass through
// float); anything else is a load error naming the triangle.
uint32_t whole(const Json* j, const char* key, size_t tri, double max) {
    if (!j || j->kind != Json::Number || j->number != std::floor(j->number) || j->number < 0.0 ||
        j->number > max)
        throw std::runtime_error(std::string("terrain: ") + key + " of triangle " + std::to_string(tri) +
                                 " is not an integer in range");
    return static_cast<uint32_t>(j->number);
}

void read_tris(const Json& doc, const char* key, bool hasClass, std::vector<Tri>& out) {
    const Json* arr = doc.get(key);
    if (!arr || arr->kind != Json::Array) throw std::runtime_error(std::string("terrain: missing array ") + key);
    for (size_t n = 0; n < arr->array.size(); ++n) {
        const Json& e = arr->array[n];
        const Json* v = e.get("v");
        const Json* owner = e.get("owner");
        const Json* cls = e.get("class");
        if (!v || v->kind != Json::Array || v->array.size() != 3 || !owner || (hasClass && !cls))
            throw std::runtime_error(std::string("terrain: bad triangle in ") + key);
        Tri t{};
        for (int k = 0; k < 3; ++k) {
            const Json& p = v->array[k];
            if (p.kind != Json::Array || p.array.size() != 3)
                throw std::runtime_error(std::string("terrain: bad vertex in ") + key);
            for (int c = 0; c < 3; ++c) t.v[k][c] = num(p.array[c]);
        }
        t.owner = whole(owner, "owner", n, 4294967295.0);
        t.cls = hasClass ? static_cast<int>(whole(cls, "class", n, 127.0)) : 0;  // data: 7-bit class mask
        out.push_back(t);
    }
}

}  // namespace

bool terrain_load(const std::string& json, TerrainMap& out, std::string& error) {
    try {
        const Json doc = json_parse(json);
        const Json* fmt = doc.get("format");
        if (!fmt || fmt->kind != Json::String || fmt->string != "sotc-terrain/0")
            throw std::runtime_error("terrain: format is not sotc-terrain/0");
        TerrainMap map;
        read_tris(doc, "stage_tris", true, map.stage);
        read_tris(doc, "boss_ground", false, map.boss);
        build_grid(map.stage, map.stageGrid);
        build_grid(map.boss, map.bossGrid);
        out = std::move(map);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

TerrainHit terrain_at(const TerrainMap& map, Vec3 p) {
    float y = 0.0f;
    const Tri* t = highest(map.stage, map.stageGrid, p, p.y - 1.0f, p.y + 0.5f, y);  // authored: brief's window
    return t ? TerrainHit{true, t->cls, t->owner, y} : TerrainHit{false, 0, 0, 0.0f};
}

TerrainHit terrain_boss_ground(const TerrainMap& map, Vec3 p) {
    float y = 0.0f;
    const Tri* t = highest(map.boss, map.bossGrid, p, -1e30f, p.y + 2.0f, y);  // authored: S12's step (+2.0 m)
    return t ? TerrainHit{true, 0, t->owner, y} : TerrainHit{false, 0, 0, 0.0f};
}
