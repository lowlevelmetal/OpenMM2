#pragma once

#include "city/CityData.h"
#include "city/PathSet.h"
#include "game/RaceConfig.h"
#include "game/bangers/BangerData.h"
#include "vfs/Vfs.h"

#include <string>
#include <string_view>
#include <vector>

namespace mm2::game::bangers {

// A prop to place in the world. `transform` puts the model's ground origin
// (the point its CG offset is measured from) with the prop's rotation.
struct PlacedProp {
    enum class Source { Instance, PathSet, StreetRule, Race };
    std::string model;
    Mat34 transform;
    int room = 0; // PSDL room when known, else 0 (found from the ground point)
    Source source = Source::Instance;
    // dgUnhitMtxBangerInstance keeps the whole matrix; dgUnhitYBangerInstance
    // keeps only a rotation about Y (see BangerSet::add).
    bool fullMatrix = false;
    // dgBangerInstance::SetVariant: the paint job (the .inst record's
    // variant byte; 0 for the other sources).
    int variant = 0;
};

// city/<map>/propdefs.csv: one street prop type (columns by name).
struct PropDef {
    std::string name;
    float start = 0.0f;    // metres from the start of the road to the first prop
    float distance = 1.0f; // metres between props
    int maxUse = 1;        // most props of this type per rule side and road
    float minLerp = 0.1f, maxLerp = 0.1f; // across the sidewalk: 0 curb .. 1 outer edge
    // file1..file4 as far as the row has cells (an empty cell stays as an
    // empty name: picking it places nothing, as in MM2).
    std::vector<std::string> files;
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
// strip; dgPath::Enumerate places nothing for any other) and a spacing byte
// in quarter metres (0 = 5 m). OpenMM2's PTH1 parser reads that trailer as
// the last point's spare word.
struct PathPlacement {
    int type = 0;
    float spacing = 5.0f;
};
PathPlacement decodePathPlacement(const city::PathSetPath& path);

// Props of a path set (city/<map>/props.pathset, race/<city>/<race>.pathset),
// dgPath::Enumerate + cityLevel::LoadPath:
//   type 0: a prop at every point, unrotated;
//   type 1: point pairs, a prop at the first with +X towards the second
//           (in the ground plane);
//   type 2: each segment separately, n = floor(length / spacing) props
//           length / n apart from its start while at least the spacing of
//           the segment is left (none on segments shorter than the spacing,
//           none at the last point), +X along the segment (tilted with it)
//           with Z = X x Y and Y = Z x X left unnormalised, kept as a full
//           matrix.
// Only paths whose model has banger data are returned when `bangerOnly`
// (dgUnhitBangerInstance::RequestBanger makes nothing without it).
std::vector<PlacedProp> placePathSet(const city::PathSet& set, PlacedProp::Source source,
                                     const BangerDataLibrary* bangerOnly = nullptr);

// Street props (cityPropulator, lvlSDL::Propulate) on the roads of the PSDL
// (lvlAiMap): for every road whose first room has a prop rule N other than
// 0, rules "nNNleft" then "nNNright". The random generator restarts (seed 1)
// per road. For each prop of a rule the whole road is walked along both
// sidewalks (the left one first), once per road strip of the first room:
// from `start`, every `distance` metres, at a random fraction
// minLerp..maxLerp from curb to outer edge, 0.15 m up, +X from curb to outer
// edge; props stand only on the rule's side, at most maxUse per rule side
// and road, picking a random variant. The sidewalk polylines are
// lvlAiMap::GetSidewalkVertex's: each room's sections after the first room's
// continue the road without their first section, interior corners are cut
// by up to 0.1 m.
std::vector<PlacedProp> placeStreetProps(const city::Psdl& psdl, const std::vector<PropDef>& defs,
                                         const std::vector<PropRule>& rules);

// The name of a race's prop path set in race/<city>/ (cityLevel::Load:
// dgGameModeNames[mode] formatted with the race index): "roam" for cruise,
// "race<N>", "circuit<N>", "blitz<N>", "crash<N>", "multicop" for Cops and
// Robbers. Empty when a race mode has no race index.
std::string racePropsName(GameMode mode, int raceIndex);

// Everything for a city, in cityLevel::Load's order: the street rules,
// banger instances of city/<map>.inst and <map>_ai.inst (e.g. stop signs),
// city/<map>/props.pathset, then race/<map>/<raceProps>.pathset when
// `raceProps` names one (racePropsName()).
std::vector<PlacedProp> placeCityProps(const city::CityData& city, const vfs::Vfs& vfs,
                                       const BangerDataLibrary& data, std::string_view raceProps = {});

} // namespace mm2::game::bangers
