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
};

// city/<map>/propdefs.csv: one street prop type.
struct PropDef {
    std::string name;
    float start = 0.0f;    // metres from the start of the sidewalk to the first prop
    float distance = 1.0f; // metres between props
    int maxUse = 1;        // most props of this type per sidewalk
    float minLerp = 0.1f, maxLerp = 0.1f; // across the sidewalk: 0 curb .. 1 outer edge
    std::vector<std::string> files;       // model variants
};
// city/<map>/proprules.csv: props of a rule ("n01left") by name.
struct PropRule {
    std::string name;
    std::vector<std::string> props;
};

std::vector<PropDef> parsePropDefs(std::string_view text);
std::vector<PropRule> parsePropRules(std::string_view text);

// A path of a .pathset decoded into placement type and spacing.
//
// MM2's dgPath has a Type (0 single points, 1 directed points, 2 line strip)
// and a Spacing that the PTH1 layout has no fields for. In every retail path
// the last point's spare word holds them: low byte = type, next byte =
// spacing in tenths of a metre (e.g. 0x1402: line strip every 2.0 m for
// wooden barricades, 0xC802: every 20 m for pier cleats, 0x1C02 every 2.8 m
// for bollards). Inferred from the data, consistent across both cities.
struct PathPlacement {
    int type = 0;        // 0 single points, 1 directed points (position, look-at pairs), 2 line strip
    float spacing = 0.0f;
};
PathPlacement decodePathPlacement(const city::PathSetPath& path);

// Props of a path set (city/<map>/props.pathset, race/<dir>/<race>.pathset).
// Only paths whose model has banger data are returned when `bangerOnly`.
std::vector<PlacedProp> placePathSet(const city::PathSet& set, PlacedProp::Source source,
                                     const BangerDataLibrary* bangerOnly = nullptr);

// Street props from city/<map>/propdefs.csv + proprules.csv: every road room
// with a PSDL prop rule N gets rule "nNNleft" along its left sidewalk and
// "nNNright" along its right one (RoadStrip sections: outer L, curb L, curb
// R, outer R). Props stand `start + k * distance` metres along the curb,
// at most maxUse per sidewalk, `minLerp` of the way from curb to outer edge,
// with their +X axis pointing away from the road (so lamp arms, modelled
// along -X, reach over the street). All of this is inferred from the data
// (rules n01-n16 in London and n01-n20 in SF match the room prop-rule range).
std::vector<PlacedProp> placeStreetProps(const city::Psdl& psdl, const std::vector<PropDef>& defs,
                                         const std::vector<PropRule>& rules);

// Everything for a city: banger instances from city/<map>.inst and
// <map>_ai.inst (e.g. stop signs), city/<map>/props.pathset and the street
// rules.
std::vector<PlacedProp> placeCityProps(const city::CityData& city, const vfs::Vfs& vfs, const BangerDataLibrary& data);

} // namespace mm2::game::bangers
