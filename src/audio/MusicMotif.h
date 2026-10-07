/* DirectMusic motif support for OpenMM2's soundtrack, implemented on top of
 * dmusic internals (MusicMotif.c is compiled into the dmusic library). dmusic
 * has no secondary segments, so a motif (a style pattern flagged MOTIF, e.g.
 * GrooverStyle's "BigAir") is played once on its own performance, following
 * the main performance's tempo and chord. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DmLoader DmLoader;
typedef struct DmPerformance DmPerformance;
typedef struct DmStyle DmStyle;

/* Loads a style file through the loader and downloads its bands' instruments.
 * Returns a new reference or NULL. */
DmStyle* OpenMM2_DmStyle_load(DmLoader* loader, char const* file);
void OpenMM2_DmStyle_release(DmStyle* style);

/* Plays pattern `pattern` of `style` once on `perf`, using the style's band
 * named `band` (NULL keeps the current band). Tempo and chord are taken from
 * `follow` (the main soundtrack performance) when given. Returns the
 * pattern's length in seconds, or a negative value when the pattern or band
 * does not exist. */
double OpenMM2_DmPerformance_playPattern(DmPerformance* perf, DmPerformance const* follow, DmStyle* style,
                                         char const* pattern, char const* band);

/* Sets the performance's current chord to DirectMusic's implicit default
 * (C2 major: root 12, chord pattern 0x91, major scale 0xab5ab5). dmusic starts
 * from an all-zero chord instead, which puts chord-relative notes of segments
 * without a chord track (all of MM2's soundtrack) an octave too low; with the
 * default chord the drum notes land exactly on the key ranges of the MM2 DLS
 * drum instruments (see docs/music.md). */
void OpenMM2_DmPerformance_setDefaultChord(DmPerformance* perf);

/* Number of mono samples until the next beat of `perf`'s current segment. */
uint32_t OpenMM2_DmPerformance_samplesToBeat(DmPerformance* perf);

#ifdef __cplusplus
}
#endif
