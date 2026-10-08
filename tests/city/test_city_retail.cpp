// Tests against the retail game files ($OPENMM2_GAME_DATA).
#include "TestData.h"
#include "city/CityData.h"
#include "city/CityMesh.h"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>

using namespace mm2;
using namespace mm2::city;

namespace {

// Takes the name by value so the returned reference never looks tied to a
// temporary argument (GetParam() is a const char*).
const CityData& loadedCity(std::string_view name) {
    static std::map<std::string, CityData, std::less<>> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        std::string err;
        auto c = loadCity(*test::gameData(), name, &err);
        if (!c)
            ADD_FAILURE() << name << ": " << err;
        it = cache.emplace(name, c ? std::move(*c) : CityData{}).first;
    }
    return it->second;
}

std::string_view textOf(const std::optional<std::vector<std::byte>>& b) {
    return b ? std::string_view(reinterpret_cast<const char*>(b->data()), b->size()) : std::string_view();
}

bool insidePolygon(const std::vector<Vec2>& poly, Vec2 p) {
    bool c = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            c = !c;
    }
    return c;
}

float distanceToPolygon(const std::vector<Vec2>& poly, Vec2 p) {
    float best = 1e30f;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        const Vec2 d = b - a;
        const float len2 = d.mag2();
        const float t = len2 > 0 ? std::clamp((p - a).dot(d) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, (p - (a + d * t)).mag());
    }
    return best;
}

std::vector<Vec2> roomPolygon(const Psdl& psdl, std::size_t room) {
    std::vector<Vec2> poly;
    for (const auto& pt : psdl.rooms[room].perimeter)
        poly.push_back({psdl.vertices[pt.vertex].x, psdl.vertices[pt.vertex].z});
    return poly;
}

class RetailCity : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override { MM2_REQUIRE_GAME_DATA(); }
};

} // namespace

TEST(RetailCityList, FindsBothCities) {
    MM2_REQUIRE_GAME_DATA();
    const auto cities = listCities(*test::gameData());
    ASSERT_EQ(cities.size(), 2u);
    EXPECT_EQ(cities[0].mapName, "london");
    EXPECT_EQ(cities[1].mapName, "sf");
    EXPECT_EQ(cities[1].localizedName, "San Francisco");
    for (const auto& c : cities) {
        EXPECT_EQ(c.blitzNames.size(), static_cast<std::size_t>(c.blitzCount));
        EXPECT_EQ(c.circuitNames.size(), static_cast<std::size_t>(c.circuitCount));
        EXPECT_EQ(c.checkpointNames.size(), static_cast<std::size_t>(c.checkpointCount));
    }
}

TEST_P(RetailCity, PsdlIsConsistent) {
    const auto& c = loadedCity(GetParam());
    const auto& p = c.psdl;
    EXPECT_TRUE(validatePsdl(p).empty());
    EXPECT_GT(p.roomCount(), 1000u);
    EXPECT_GT(p.firstRoadRoom, 1u);
    // Rooms before firstRoadRoom are building blocks; none of them are roads.
    for (std::size_t r = 1; r < p.firstRoadRoom; ++r)
        EXPECT_EQ(p.rooms[r].flags & (RoomFlag::Road | RoomFlag::Intersection), 0) << "room " << r;
    // Every vertex lies inside the stored bounds.
    for (const auto& v : p.vertices) {
        EXPECT_GE(v.x, p.bounds.min.x - 0.01f);
        EXPECT_LE(v.x, p.bounds.max.x + 0.01f);
        EXPECT_GE(v.z, p.bounds.min.z - 0.01f);
        EXPECT_LE(v.z, p.bounds.max.z + 0.01f);
    }
    // Neighbours are symmetric: if A lists B across an edge, B lists A.
    std::size_t asymmetric = 0, links = 0;
    for (std::size_t r = 1; r < p.roomCount(); ++r)
        for (const auto& pt : p.rooms[r].perimeter) {
            if (!pt.neighbor)
                continue;
            ++links;
            bool back = false;
            for (const auto& q : p.rooms[pt.neighbor].perimeter)
                back |= q.neighbor == r;
            asymmetric += !back;
        }
    EXPECT_GT(links, 1000u);
    // Measured: 53 of ~5,800 links in London and 138 in SF are one-way
    // (mostly around tunnels and bridges).
    EXPECT_LT(asymmetric, links / 20);
}

TEST_P(RetailCity, MeshIsWellFormed) {
    const auto& c = loadedCity(GetParam());
    const auto mesh = buildCityMesh(c.psdl);
    EXPECT_GT(mesh.triangleCount(), 50000u);
    std::size_t roomsWithGeometry = 0;
    for (std::size_t r = 1; r < mesh.rooms.size(); ++r) {
        const auto& room = mesh.rooms[r];
        roomsWithGeometry += !room.batches.empty();
        for (const auto& b : room.batches) {
            ASSERT_EQ(b.indices.size() % 3, 0u);
            for (auto i : b.indices)
                ASSERT_LT(i, b.vertices.size());
            for (const auto& v : b.vertices) {
                ASSERT_TRUE(std::isfinite(v.position.x) && std::isfinite(v.position.y) &&
                            std::isfinite(v.position.z));
                ASSERT_NEAR(v.normal.mag(), 1.0f, 1e-3f);
                ASSERT_TRUE(std::isfinite(v.uv.x) && std::isfinite(v.uv.y));
            }
            if (b.kind == SurfaceKind::Road || b.kind == SurfaceKind::Roof || b.kind == SurfaceKind::Sidewalk) {
                for (const auto& v : b.vertices)
                    ASSERT_GE(v.normal.y, 0.0f) << surfaceKindName(b.kind) << " room " << r;
            }
        }
    }
    // Only geometry-less rooms (water, warps) may be empty.
    EXPECT_GT(roomsWithGeometry, mesh.rooms.size() * 95 / 100);
}

TEST_P(RetailCity, InstancesSitInTheirRooms) {
    const auto& c = loadedCity(GetParam());
    ASSERT_GT(c.instances.size(), 1000u);
    ASSERT_GT(c.aiInstances.size(), 10u);
    for (const auto* list : {&c.instances, &c.aiInstances}) {
        std::size_t near = 0;
        for (const auto& inst : *list) {
            ASSERT_GT(inst.room, 0);
            ASSERT_LT(inst.room, c.psdl.roomCount());
            const auto poly = roomPolygon(c.psdl, inst.room);
            const Vec2 p{inst.transform.m3.x, inst.transform.m3.z};
            near += insidePolygon(poly, p) || distanceToPolygon(poly, p) < 1.0f;
        }
        // Measured: 99.5% of london.inst; landmarks may straddle rooms.
        EXPECT_GT(near, list->size() * 97 / 100);
    }
}

TEST_P(RetailCity, PvsMatchesRooms) {
    const auto& c = loadedCity(GetParam());
    ASSERT_TRUE(c.pvs);
    EXPECT_EQ(c.pvs->roomCount(), c.psdl.roomCount());
    std::size_t selfVisible = 0, rows = 0;
    for (std::size_t r = 1; r < c.psdl.roomCount(); ++r) {
        rows += c.pvs->hasData(r);
        selfVisible += c.pvs->visible(r, r);
    }
    EXPECT_EQ(rows, c.psdl.roomCount() - 1);
    EXPECT_GE(selfVisible, c.psdl.roomCount() - 4);
}

TEST_P(RetailCity, AiMapMatchesPsdl) {
    const auto& c = loadedCity(GetParam());
    ASSERT_TRUE(c.aiMap);
    const auto& map = *c.aiMap;
    EXPECT_TRUE(validateAiMap(map, c.psdl.roomCount()).empty());
    // One AI path per PSDL road, sharing its rooms, then the shortcut roads
    // of <city>_sup.bai (70 London, 49 SF).
    EXPECT_EQ(map.numShortcuts, GetParam() == std::string("london") ? 70u : 49u);
    const std::size_t mainPaths = map.paths.size() - map.numShortcuts;
    ASSERT_EQ(mainPaths, c.psdl.roads.size());
    std::size_t sharesRooms = 0;
    for (std::size_t k = 0; k < mainPaths; ++k) {
        std::set<std::uint16_t> rooms(map.paths[k].rooms.begin(), map.paths[k].rooms.end());
        bool all = true;
        for (auto r : c.psdl.roads[k].rooms)
            all &= rooms.contains(PsdlRoad::roomId(r));
        sharesRooms += all;
    }
    EXPECT_GT(sharesRooms, mainPaths * 9 / 10);
    // Intersections are in intersection rooms and list paths that end there.
    std::size_t inIntersectionRoom = 0, consistentEnds = 0;
    for (const auto& in : map.intersections) {
        inIntersectionRoom += (c.psdl.rooms[in.room].flags & RoomFlag::Intersection) != 0;
        for (auto pid : in.paths) {
            const auto& p = map.paths[pid];
            consistentEnds += p.ends[0].intersection == in.id || p.ends[1].intersection == in.id;
        }
    }
    EXPECT_GT(inIntersectionRoom, map.intersections.size() * 9 / 10);
    std::size_t listed = 0;
    for (const auto& in : map.intersections)
        listed += in.paths.size();
    EXPECT_EQ(consistentEnds, listed);
    // Path ends: ends[0] is the intersection at the last section, ends[1] at
    // the first (measured 529/540 London, 374/379 SF; the rest are loops).
    // (The hand-made shortcut roads' frames are rougher; not checked.)
    std::size_t ordered = 0;
    for (std::size_t k = 0; k < mainPaths; ++k) {
        const auto& p = map.paths[k];
        const auto& a = map.intersections[p.ends[0].intersection].center;
        const auto& b = map.intersections[p.ends[1].intersection].center;
        ordered += p.center.back().dist(a) <= p.center.back().dist(b);
        // Section frames: y and z are unit vectors, z points back along the
        // road (Angel convention), x is unit except at a few sharp bends.
        for (std::size_t i = 0; i < p.sectionCount(); ++i) {
            ASSERT_NEAR(p.yAxis[i].mag(), 1.0f, 0.01f);
            ASSERT_NEAR(p.zAxis[i].mag(), 1.0f, 0.01f);
            ASSERT_NEAR(p.xAxis[i].mag(), 1.0f, 0.07f);
            ASSERT_NEAR(p.xAxis[i].dot(p.yAxis[i]), 0.0f, 0.02f);
            if (i + 1 < p.sectionCount()) {
                const Vec3 forward = (p.center[i + 1] - p.center[i]).normalized();
                ASSERT_LT(forward.dot(p.zAxis[i]), 0.0f);
                // x points left of the direction of increasing index.
                ASSERT_GT(p.xAxis[i].dot(Vec3::yAxis().cross(forward)), 0.0f);
            }
        }
        float acc = 0;
        for (std::size_t i = 1; i < p.sectionCount(); ++i) {
            acc += p.center[i].dist(p.center[i - 1]);
            ASSERT_NEAR(p.centerLengths[i - 1], acc, 0.05f);
        }
    }
    EXPECT_GT(ordered, mainPaths * 9 / 10);
}

TEST_P(RetailCity, EnvironmentLoads) {
    const auto& c = loadedCity(GetParam());
    for (std::size_t i = 0; i < c.lighting.size(); ++i)
        ASSERT_TRUE(c.lighting[i]) << "lt" << i;
    EXPECT_EQ(c.lighting[0]->name, "clear-morning");
    EXPECT_EQ(c.lighting[lightingIndex(3, 3)]->name, "rainy-night");
    EXPECT_EQ(c.fog.size(), 16u);
    ASSERT_TRUE(c.sky);
    EXPECT_FALSE(c.sky->model.empty());
    ASSERT_TRUE(c.water);
    ASSERT_TRUE(c.extent);
    EXPECT_FALSE(c.resetPoints.empty());
    EXPECT_FALSE(c.materials.empty());
    EXPECT_GT(c.textureMaterials.size(), 1000u);
    EXPECT_TRUE(c.cruise);
    EXPECT_FALSE(c.cityPathSets.empty());
}

TEST_P(RetailCity, RacesReferenceExistingFiles) {
    const auto& c = loadedCity(GetParam());
    const auto& vfs = *test::gameData();
    std::map<RaceMode, int> perMode;
    for (const auto& r : c.races) {
        ++perMode[r.mode];
        ASSERT_TRUE(r.settings) << raceModeName(r.mode) << r.index;
        EXPECT_FALSE(r.aiMap.empty()) << raceModeName(r.mode) << r.index;
        if (r.mode == RaceMode::CrashCourse) {
            ASSERT_FALSE(r.crashEvents.empty());
            auto bytes = vfs.readAll(r.crashEvents);
            ASSERT_TRUE(bytes);
            auto events = parseCrashEvents(textOf(bytes));
            ASSERT_TRUE(events);
            ASSERT_FALSE(events->empty());
            for (const auto& e : *events)
                EXPECT_TRUE(vfs.exists("race/" + c.info.raceDir + "/" + e.file + ".csv")) << e.file;
        } else {
            ASSERT_FALSE(r.waypoints.empty()) << raceModeName(r.mode) << r.index;
            auto bytes = vfs.readAll(r.waypoints);
            auto wp = parseWaypoints(textOf(bytes));
            ASSERT_TRUE(wp);
            EXPECT_GE(wp->size(), 2u);
        }
        for (const auto* cfgPath : {&r.aiMap, &r.aiMapPro}) {
            if (cfgPath->empty())
                continue;
            auto bytes = vfs.readAll(*cfgPath);
            auto cfg = parseAiMapConfig(textOf(bytes));
            ASSERT_TRUE(cfg) << *cfgPath;
            for (const auto& o : cfg->opponents) {
                const std::string opp = "race/" + c.info.raceDir + "/" + o.pathFile;
                auto ob = vfs.readAll(opp);
                ASSERT_TRUE(ob) << opp;
                auto path = parseOpponentPath(textOf(ob));
                ASSERT_TRUE(path) << opp;
                EXPECT_GE(path->size(), 2u) << opp;
            }
        }
    }
    EXPECT_EQ(perMode[RaceMode::Blitz], c.info.blitzCount);
    EXPECT_EQ(perMode[RaceMode::Circuit], c.info.circuitCount);
    EXPECT_EQ(perMode[RaceMode::Checkpoint], c.info.checkpointCount);
    EXPECT_GT(perMode[RaceMode::CrashCourse], 0);
}

INSTANTIATE_TEST_SUITE_P(Cities, RetailCity, ::testing::Values("london", "sf"));
