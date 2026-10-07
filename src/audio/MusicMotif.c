/* See MusicMotif.h. Compiled as part of the dmusic library (it needs the
 * library's internal headers and the non-static DmPattern_generateMessages
 * from OpenMM2's patched Performance.c). */
#include "_Internal.h"

#include "MusicMotif.h"

#include <ctype.h>
#include <string.h>

DmResult DmPattern_generateMessages(DmPattern* slf,
                                    DmStyle* sty,
                                    DmMessage_Chord* chord,
                                    uint32_t time,
                                    uint32_t seq,
                                    DmMessageQueue* out);

static int openmm2_iequals(char const* a, char const* b) {
	if (a == NULL || b == NULL) {
		return 0;
	}
	while (*a && *b) {
		if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
			return 0;
		}
		++a;
		++b;
	}
	return *a == *b;
}

DmStyle* OpenMM2_DmStyle_load(DmLoader* loader, char const* file) {
	DmReference ref;
	memset(&ref, 0, sizeof ref);
	ref.name = file;
	ref.file = file;

	DmStyle* sty = NULL;
	if (DmLoader_getStyle(loader, &ref, &sty) != DmResult_SUCCESS || sty == NULL) {
		return NULL;
	}
	if (DmStyle_download(sty, loader) != DmResult_SUCCESS) {
		DmStyle_release(sty);
		return NULL;
	}
	return sty;
}

void OpenMM2_DmStyle_release(DmStyle* style) {
	DmStyle_release(style);
}

double OpenMM2_DmPerformance_playPattern(DmPerformance* perf, DmPerformance const* follow, DmStyle* style,
                                         char const* pattern, char const* band) {
	if (perf == NULL || style == NULL || pattern == NULL) {
		return -1.0;
	}

	DmPattern* pttn = NULL;
	for (size_t i = 0; i < style->patterns.length; ++i) {
		if (openmm2_iequals(style->patterns.data[i].info.unam, pattern)) {
			pttn = &style->patterns.data[i];
			break;
		}
	}
	if (pttn == NULL) {
		return -1.0;
	}

	DmBand* bnd = NULL;
	if (band != NULL) {
		for (size_t i = 0; i < style->bands.length; ++i) {
			if (openmm2_iequals(style->bands.data[i]->info.unam, band)) {
				bnd = style->bands.data[i];
				break;
			}
		}
		if (bnd == NULL) {
			return -1.0;
		}
	}

	if (mtx_lock(&perf->lock) != thrd_success) {
		return -1.0;
	}

	DmMessageQueue_clear(&perf->control_queue);
	DmMessageQueue_clear(&perf->music_queue);
	DmSynth_sendNoteOffEverything(&perf->synth);

	if (perf->style != style) {
		DmStyle_release(perf->style);
		perf->style = DmStyle_retain(style);
	}
	perf->time_signature = style->time_signature;
	perf->tempo = follow != NULL ? follow->tempo : style->tempo;
	if (follow != NULL) {
		perf->chord = follow->chord;
	}

	if (bnd != NULL && perf->band != bnd) {
		DmBand_release(perf->band);
		perf->band = DmBand_retain(bnd);
		DmSynth_sendBandUpdate(&perf->synth, bnd);
	}

	perf->segment_start = perf->time;
	DmResult rv = DmPattern_generateMessages(pttn, style, &perf->chord, perf->time, perf->variation, &perf->music_queue);
	perf->variation += 1;

	double seconds = -1.0;
	if (rv == DmResult_SUCCESS) {
		uint32_t ticks = Dm_getMeasureLength(perf->time_signature) * pttn->length_measures;
		seconds = (double) ticks / Dm_getTicksPerSecond(perf->time_signature, perf->tempo);
	}

	(void) mtx_unlock(&perf->lock);
	return seconds;
}

uint32_t OpenMM2_DmPerformance_samplesToBeat(DmPerformance* perf) {
	if (perf == NULL || perf->segment == NULL) {
		return 0;
	}
	if (mtx_lock(&perf->lock) != thrd_success) {
		return 0;
	}
	uint32_t beat = Dm_getBeatLength(perf->time_signature);
	uint32_t offset = perf->time - perf->segment_start;
	uint32_t delay = (beat != 0 && offset % beat != 0) ? beat - offset % beat : 0;
	uint32_t samples =
	    Dm_getSampleCountForDuration(delay, perf->time_signature, perf->tempo, perf->sample_rate, 1);
	(void) mtx_unlock(&perf->lock);
	return samples;
}

void OpenMM2_DmPerformance_setDefaultChord(DmPerformance* perf) {
	if (perf == NULL || mtx_lock(&perf->lock) != thrd_success) {
		return;
	}
	DmMessage_Chord* c = &perf->chord;
	memset(c, 0, sizeof *c);
	c->type = DmMessage_CHORD;
	strcpy(c->name, "M");
	c->subchord_count = 1;
	c->subchords[0].chord_pattern = 0x91;
	c->subchords[0].scale_pattern = 0xab5ab5;
	c->subchords[0].inversion_points = 0xffffff;
	c->subchords[0].levels = 0xffffffff;
	c->subchords[0].chord_root = 12;
	c->subchords[0].scale_root = 0;
	(void) mtx_unlock(&perf->lock);
}
