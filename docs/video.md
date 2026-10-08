# Intro movie

The original game plays `GAME/LOGOS.AVI` (the Microsoft and Angel Studios
logos) before the title screen. It lives in the disc's `GAME` folder (and next
to the game in an installation), outside the `.AR` archives, so it is opened
with `vfs::openSourceFile(source, "LOGOS.AVI")`.

| Property | Value |
|---|---|
| Container | AVI (RIFF), `avih` + two `strl`, `movi`, `idx1` |
| Video | Intel Indeo Video Interactive 5 (`IV50`), 320×240, 15 fps, 332 frames, no null frames |
| Audio | PCM, 8-bit unsigned, mono, 22050 Hz, 488,032 samples (22.13 s), interleaved ahead of the video (`dwInitialFrames` 11) |

## Code

* `src/video/Avi.*`: RIFF/AVI demuxer. Walks `hdrl` (`avih`, `strh`,
  `strf`) and the `movi` list (descending into `rec ` lists), collecting
  `##dc`/`##db` video and `##wb` audio chunks in file order. `idx1` is not
  needed for sequential playback. 8/16-bit PCM is returned as 16-bit
  `audio::SoundBuffer`.
* `src/video/Indeo5.*`: the Indeo 5 decoder, translated to self-contained
  C++ from FFmpeg (see below). Output is planar YUV 4:1:0;
  `yuv410ToRgba()` converts with BT.601 limited-range coefficients (Indeo's
  YVU9 is Rec. 601 video range) and bilinear chroma upsampling with
  centred chroma siting.
* `src/video/Movie.*`: decodes frames in order; empty chunks, null frames and
  corrupt frames repeat the previous picture.
* `src/app/IntroScreen.*`: decodes on a worker thread (6 frames ahead), plays
  the soundtrack as a mixer stream on `Bus::Effects` and shows the frame due
  at the audio clock (wall clock when no audio device is open) at its own
  size (320×240) in the middle of the 640×480 UI space, as MM2's
  `ebolaPlayMovie` plays it in an MCI window centred on the 640×480 screen.
  Esc, Space or the left mouse button skip it, checked every 250 ms; it
  pauses while the window is inactive (after it has had focus once).
  Without `LOGOS.AVI` the screen continues straight to the frontend. MM2
  skips the movie with `-nomovie` or in a window; OpenMM2 plays it in a
  window too.
* `tools/introplay`: `--compare-yuv`, `--compare-rgba`, `--png` and window
  playback through `IntroScreen`.

## Verification

* `test_video`: AVI parsing on a synthetic file, decoder robustness against
  random data, and the retail movie: all 332 frames decode without error,
  frame count equals `avih.dwTotalFrames`, audio length matches the video
  (22.13 s).
* Against `ffmpeg -fps_mode passthrough -pix_fmt yuv410p` (FFmpeg's own Indeo 5
  decoder): **bit-identical** — mean absolute difference 0 in Y, U and V,
  0 of 332 frames differ.
* RGBA against ffmpeg with `-sws_flags bilinear+accurate_rnd+full_chroma_int`:
  mean absolute difference R 0.003, G 0.003, B 0.002, max 1 (rounding).
  Against swscale's default fast path: R 0.42, G 1.13, B 0.39, max 22, due to
  its different chroma sampling.
* AddressSanitizer + UBSan: the retail movie plus 20,000 randomly mutated or
  truncated frames, no reports.
* Window playback ends after 22.17 s for a 22.13 s movie; the audio clock
  tracks wall time within 10 ms throughout.

## Licence

`Indeo5.cpp` is a translation of FFmpeg n7.1's `libavcodec/indeo5.c`, `ivi.c`,
`ivi.h` and `ivi_dsp.c`, and `IndeoTables.inc` copies its data tables
(`indeo5data.h`, the scan and run/value tables of `ivi.c`, and
`ff_zigzag_direct`). FFmpeg's files are © 2009 Maxim Poliakovski and licensed
under the GNU LGPL 2.1 or later. LGPL 2.1 section 3 allows distributing them
under the GNU GPL instead, which OpenMM2 does (GPL-3.0-or-later). The original
copyright notices are kept in the file headers.

The decoder was translated rather than vendored with a libavcodec
compatibility shim because the Indeo 5 subset is small (about 1,500 lines
without the Indeo 4 paths) and needs nothing else from FFmpeg except a bit
reader and a one-level Huffman table, which are reimplemented here. The
translation keeps FFmpeg's arithmetic exactly, which the bit-identical
comparison confirms.
