#pragma once

#include "city/CityData.h"
#include "city/PathSet.h"
#include "game/bangers/BangerData.h"
#include "vfs/Vfs.h"

#include <string>
#include <vector>

namespace mm2::game::bangers {

// A prop to place in the world. `transform` puts the model's ground origin
// (the point its CG offset is measured from) with the prop's rotation.
struct PlacedProp {
    enum class Source { Instance, PathSet, StreetRule, Race };
    std::string model;
    Mat34 transform;
    int room = 0; // PSDL room when known, else 0
    Source source = Source::Instance;
    // dgUnhitMtxBangerInstance keeps the whole matrix; dgUnhitYBangerInstance
    // keeps only a rotation about Y (see BangerSet::add).
    bool fullMatrix = false;
};

// city/<map>/propdefs.csv: one street prop type (columns by name).
struct PropDef {
    std::string name;
    float start = 0.0f;    // metres from the start of the road to the first prop
    float distance = 1.0f; // metres between props
    int maxUse = 1;        // most props of this type per walk
    float minLerp = 0.1f, maxLerp = 0.1f; // across the sidewalk: 0 curb .. 1 outer edge
    std::vector<std::string> files;       // model variants (file1..file4)
};
// city/<map>/proprules.csv: props of a rule ("n01left") by name, prop1...
struct PropRule {
    std::string name;
    std::vector<std::string> props;
};

std::vector<PropDef> parsePropDefs(std::string_view text);
std::vector<PropRule> parsePropRules(std::string_view text);

// A path set's placement (dgPath::Load): the trailer after the last point
// holds a type byte (0 single points, 1 position/direction pairs, 2 line
// strip) and a spacing byte in quarter metres (0 = 5 m). OpenMM2's PTH1
// parser reads that trailer as the last point's spare word.
struct PathPlacement {
    int type = 0;
    float spacing = 5.0f;
};
PathPlacement decodePathPlacement(const city::PathSetPath& path);

// Props of a path set (city/<map>/props.pathset, race/<dir>/<race>.pathset),
// dgPath::Enumerate + cityLevel::LoadPath:
//   type 0: a prop at every point, unrotated;
//   type 1: point pairs, a prop at the first with +X towards the second
//           (in the ground plane);
//   type 2: each segment separately: n = floor(length / spacing) props at
//           equal steps from its start (none on segments shorter than the
//           spacing, none at the last point), +X along the segment (tilted
//           with it), kept as a full matrix.
// Only paths whose model has banger data are returned when `bangerOnly`.
std::vector<PlacedProp> placePathSet(const city::PathSet& set, PlacedProp::Source source,
                                     const BangerDataLibrary* bangerOnly = nullptr);

// Street props (cityPropulator, lvlSDL::Propulate): for every PSDL road
// whose flags have 0x40, with the prop rule N of its first room, rules
// "nNNleft" then "nNNright". The random generator restarts (seed 1) per
// road. For each prop of a rule the whole road is walked along both
// sidewalks (curb and outer edge polylines through all its rooms), once per
// road strip in the first room: from `start`, every `distance` metres, at a
// random fraction minLerp..maxLerp from curb to outer edge, 0.15 m up, +X
// from curb to outer edge; props stand only on the rule's side, at most
// maxUse per walk, picking a random variant.
std::vector<PlacedProp> placeStreetProps(const city::Psdl& psdl, const std::vector<PropDef>& defs,
                                         const std::vector<PropRule>& rules);

// Everything for a city: banger instances from city/<map>.inst and
// <map>_ai.inst (e.g. stop signs), the street rules and
// city/<map>/props.pathset (cityLevel::Load's order).
std::vector<PlacedProp> placeCityProps(const city::CityData& city, const vfs::Vfs& vfs, const BangerDataLibrary& data);

} // namespace mm2::game::bangers
