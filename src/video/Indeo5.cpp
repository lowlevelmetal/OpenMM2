// Intel Indeo Video Interactive 5 decoder.
//
// C++ translation of FFmpeg's Indeo 5 decoder (n7.1: libavcodec/indeo5.c,
// ivi.c, ivi.h, ivi_dsp.c), keeping its structure, names (in comments) and
// arithmetic so the output is bit-identical. Only the paths Indeo 5 uses are
// kept (no Indeo 4 frame types, Haar transforms or B-frame averaging).
//
// Original copyright notices:
//
//   Indeo Video Interactive v5 compatible decoder
//   Copyright (c) 2009 Maxim Poliakovski
//
//   common functions / DSP functions for Indeo Video Interactive codecs
//   Copyright (c) 2009 Maxim Poliakovski
//
//   This file is part of FFmpeg.
//
//   FFmpeg is free software; you can redistribute it and/or
//   modify it under the terms of the GNU Lesser General Public
//   License as published by the Free Software Foundation; either
//   version 2.1 of the License, or (at your option) any later version.
//
//   FFmpeg is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//   Lesser General Public License for more details.
//
// As permitted by LGPL-2.1 section 3, OpenMM2 distributes this translation
// under the terms of the GNU General Public License version 3 or later.

#include "video/Indeo5.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>

namespace mm2::video {
namespace {

// ---------------------------------------------------------------------------
// Tables

struct RVMapDesc {
    std::uint8_t eob_sym; // end of block symbol
    std::uint8_t esc_sym; // escape symbol
    std::uint8_t runtab[256];
    std::int8_t valtab[256];
};

#include "video/IndeoTables.inc"

struct HuffDesc {
    int numRows = 0;
    std::uint8_t xbits[16]{};
    bool operator==(const HuffDesc& o) const {
        return numRows == o.numRows && std::memcmp(xbits, o.xbits, static_cast<std::size_t>(numRows)) == 0;
    }
};

// ivi_mb_huff_desc / ivi_blk_huff_desc (ivi.c)
const HuffDesc kMbHuffDesc[8] = {
    {8, {0, 4, 5, 4, 4, 4, 6, 6}},
    {12, {0, 2, 2, 3, 3, 3, 3, 5, 3, 2, 2, 2}},
    {12, {0, 2, 3, 4, 3, 3, 3, 3, 4, 3, 2, 2}},
    {12, {0, 3, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2}},
    {13, {0, 4, 4, 3, 3, 3, 3, 2, 3, 3, 2, 1, 1}},
    {9, {0, 4, 4, 4, 4, 3, 3, 3, 2}},
    {10, {0, 4, 4, 4, 4, 3, 3, 2, 2, 2}},
    {12, {0, 4, 4, 4, 3, 3, 2, 3, 2, 2, 2, 2}},
};
const HuffDesc kBlkHuffDesc[8] = {
    {10, {1, 2, 3, 4, 4, 7, 5, 5, 4, 1}},
    {11, {2, 3, 4, 4, 4, 7, 5, 4, 3, 3, 2}},
    {12, {2, 4, 5, 5, 5, 5, 6, 4, 4, 3, 1, 1}},
    {13, {3, 3, 4, 4, 5, 6, 6, 4, 4, 3, 2, 1, 1}},
    {11, {3, 4, 4, 5, 5, 5, 6, 5, 4, 2, 2}},
    {13, {3, 4, 5, 5, 5, 5, 6, 4, 3, 3, 2, 1, 1}},
    {13, {3, 4, 5, 5, 5, 6, 5, 4, 3, 3, 2, 1, 1}},
    {9, {3, 4, 4, 5, 5, 5, 6, 5, 5}},
};

constexpr int kVlcBits = 13;        // IVI_VLC_BITS
constexpr int kIsProtected = 0x20;  // IVI5_IS_PROTECTED
constexpr int kPicSizeEsc = 15;     // IVI5_PIC_SIZE_ESC

enum FrameType { kIntra = 0, kInter = 1, kInterScal = 2, kInterNoRef = 3, kNull = 4 };

// ---------------------------------------------------------------------------
// Bit reader (FFmpeg get_bits.h with BITSTREAM_READER_LE, checked variant)

class BitReader {
public:
    void init(const std::uint8_t* buf, std::size_t bytes) {
        m_buf = buf;
        m_bytes = bytes;
        m_sizeBits = static_cast<long long>(bytes) * 8;
        m_index = 0;
    }
    unsigned peek(int n) const {
        if (n == 0)
            return 0;
        const std::size_t byte = static_cast<std::size_t>(m_index >> 3);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            const std::size_t b = byte + static_cast<std::size_t>(i);
            if (b < m_bytes)
                v |= static_cast<std::uint64_t>(m_buf[b]) << (8 * i);
        }
        v >>= (m_index & 7);
        return static_cast<unsigned>(v & ((n >= 32) ? 0xFFFFFFFFull : ((1ull << n) - 1)));
    }
    void skip(int n) { m_index = std::min(m_index + n, m_sizeBits + 8); }
    unsigned get(int n) {
        const unsigned v = peek(n);
        skip(n);
        return v;
    }
    unsigned get1() { return get(1); }
    void align() {
        const long long n = (-m_index) & 7;
        if (n)
            skip(static_cast<int>(n));
    }
    long long count() const { return m_index; }
    long long left() const { return m_sizeBits - m_index; }

private:
    const std::uint8_t* m_buf = nullptr;
    std::size_t m_bytes = 0;
    long long m_sizeBits = 0;
    long long m_index = 0;
};

// ---------------------------------------------------------------------------
// Huffman (VLC) tables: one-level lookup on the next kVlcBits bits

struct VlcEntry {
    std::int16_t sym = -1;
    std::uint8_t len = 0;
};

struct Vlc {
    std::vector<VlcEntry> table;
    bool valid() const { return !table.empty(); }
};

// ivi_create_huff_from_desc + vlc_init(..., VLC_INIT_OUTPUT_LE): codes are
// stored first-bit-first in a little-endian bit stream, so the lookup index
// is the bit-reversed code.
bool createHuffFromDesc(const HuffDesc& cb, Vlc& vlc) {
    vlc.table.assign(1u << kVlcBits, VlcEntry{});
    int pos = 0;
    for (int i = 0; i < cb.numRows; ++i) {
        const int codesPerRow = 1 << cb.xbits[i];
        const int notLastRow = i != cb.numRows - 1;
        const int prefix = ((1 << i) - 1) << (cb.xbits[i] + notLastRow);
        for (int j = 0; j < codesPerRow; ++j) {
            if (pos >= 256) // Some Indeo5 codebooks can have more than 256 elements, but only 256 codes are allowed
                break;
            int bits = i + cb.xbits[i] + notLastRow;
            if (bits > kVlcBits) {
                vlc.table.clear();
                return false;
            }
            const unsigned code = static_cast<unsigned>(prefix | j);
            if (!bits)
                bits = 1;
            unsigned rev = 0;
            for (int b = 0; b < bits; ++b)
                if (code & (1u << b))
                    rev |= 1u << (bits - 1 - b);
            for (unsigned k = rev; k < (1u << kVlcBits); k += 1u << bits)
                vlc.table[k] = {static_cast<std::int16_t>(pos), static_cast<std::uint8_t>(bits)};
            ++pos;
        }
    }
    return true;
}

struct StaticVlcs {
    Vlc mb[8], blk[8];
    StaticVlcs() {
        for (int i = 0; i < 8; ++i) {
            createHuffFromDesc(kMbHuffDesc[i], mb[i]);
            createHuffFromDesc(kBlkHuffDesc[i], blk[i]);
        }
    }
};

const StaticVlcs& staticVlcs() {
    static const StaticVlcs tables;
    return tables;
}

int getVlc(BitReader& gb, const Vlc& vlc) {
    const VlcEntry e = vlc.table[gb.peek(kVlcBits)];
    gb.skip(e.len);
    return e.sym;
}

struct HuffTab {
    int tabSel = 0;
    const Vlc* tab = nullptr;
    HuffDesc custDesc; // used only when tabSel == 7
    Vlc custTab;
};

constexpr int toSigned(int val) { return -((val >> 1) ^ -(val & 1)); } // IVI_TOSIGNED
constexpr int scaleMv(int mv, int mvScale) { return (mv + (mv > 0) + (mvScale - 1)) >> mvScale; }

// ---------------------------------------------------------------------------
// Picture structures (ivi.h)

struct MbInfo {
    std::int16_t xpos = 0, ypos = 0;
    std::uint32_t bufOffs = 0;
    std::uint8_t type = 0; // 0 intra, 1 inter
    std::uint8_t cbp = 0;
    std::int8_t qDelta = 0;
    std::int8_t mvX = 0, mvY = 0;
};

struct Tile {
    int xpos = 0, ypos = 0, width = 0, height = 0, mbSize = 0;
    int isEmpty = 0, dataSize = 0, numMBs = 0;
    std::vector<MbInfo> mbs;
    MbInfo* refMbs = nullptr;
};

enum class InvTransform { Slant8x8, RowSlant8, ColSlant8, PutPixels8x8, Slant4x4 };
enum class DcTransform { Slant2d, RowSlant, ColSlant, PutDcPixel8x8 };

struct Band {
    int plane = 0, bandNum = 0;
    int width = 0, height = 0, aheight = 0;
    int dataSize = 0;
    std::int16_t* buf = nullptr;
    std::int16_t* refBuf = nullptr;
    std::array<std::vector<std::int16_t>, 4> bufs;
    int pitch = 0;
    int isEmpty = 0;
    int mbSize = 0, blkSize = 0;
    int isHalfpel = 0;
    int inheritMv = 0, inheritQdelta = 0, qdeltaPresent = 0;
    int globQuant = 0;
    const std::uint8_t* scan = nullptr;
    HuffTab blkVlc;
    int numCorr = 0;
    std::uint8_t corr[61 * 2]{};
    int rvmapSel = 0;
    RVMapDesc* rvMap = nullptr;
    std::vector<Tile> tiles;
    InvTransform invTransform = InvTransform::Slant8x8;
    DcTransform dcTransform = DcTransform::Slant2d;
    int transformSize = 0;
    int is2dTrans = 0;
    int checksumPresent = 0;
    int checksum = 0;
    int bufsize = 0; // elements
    const std::uint16_t* intraBase = nullptr;
    const std::uint16_t* interBase = nullptr;
    const std::uint8_t* intraScale = nullptr;
    const std::uint8_t* interScale = nullptr;
};

struct Plane {
    int width = 0, height = 0;
    std::vector<Band> bands;
};

struct PicConfig {
    int picWidth = 0, picHeight = 0, chromaWidth = 0, chromaHeight = 0;
    int tileWidth = 0, tileHeight = 0, lumaBands = 0, chromaBands = 0;
    bool operator==(const PicConfig&) const = default;
};

// ---------------------------------------------------------------------------
// DSP (ivi_dsp.c): inverse slant transforms

// IVI_SLANT_BFLY, IVI_IREFLECT, IVI_SLANT_PART4
#define IVI_SLANT_BFLY(s1, s2, o1, o2, t)                                                                        \
    t = (s1) - (s2);                                                                                             \
    o1 = (s1) + (s2);                                                                                            \
    o2 = (t);
#define IVI_IREFLECT(s1, s2, o1, o2, t)                                                                          \
    t = (((s1) + (s2) * 2 + 2) >> 2) + (s1);                                                                     \
    o2 = (((s1) * 2 - (s2) + 2) >> 2) - (s2);                                                                    \
    o1 = (t);
#define IVI_SLANT_PART4(s1, s2, o1, o2, t)                                                                       \
    t = (s2) + (((s1) * 4 - (s2) + 4) >> 3);                                                                     \
    o2 = (s1) + ((-(s1) - (s2) * 4 + 4) >> 3);                                                                   \
    o1 = (t);

// Inverse slant8: inputs in call order (s1, s4, s8, s5, s2, s6, s3, s7) as in
// FFmpeg's IVI_INV_SLANT8 macro; `half` selects COMPENSATE(x) = (x + 1) >> 1.
inline void invSlant8(int s1, int s4, int s8, int s5, int s2, int s6, int s3, int s7, int* d[8], bool half) {
    int t0, t1, t2, t3, t4, t5, t6, t7, t8;
    IVI_SLANT_PART4(s4, s5, t4, t5, t0);
    IVI_SLANT_BFLY(s1, t5, t1, t5, t0);
    IVI_SLANT_BFLY(s2, s6, t2, t6, t0);
    IVI_SLANT_BFLY(s7, s3, t7, t3, t0);
    IVI_SLANT_BFLY(t4, s8, t4, t8, t0);
    IVI_SLANT_BFLY(t1, t2, t1, t2, t0);
    IVI_IREFLECT(t4, t3, t4, t3, t0);
    IVI_SLANT_BFLY(t5, t6, t5, t6, t0);
    IVI_IREFLECT(t8, t7, t8, t7, t0);
    IVI_SLANT_BFLY(t1, t4, t1, t4, t0);
    IVI_SLANT_BFLY(t2, t3, t2, t3, t0);
    IVI_SLANT_BFLY(t5, t8, t5, t8, t0);
    IVI_SLANT_BFLY(t6, t7, t6, t7, t0);
    const int r[8] = {t1, t2, t3, t4, t5, t6, t7, t8};
    for (int i = 0; i < 8; ++i)
        *d[i] = half ? ((r[i] + 1) >> 1) : r[i];
}

inline void invSlant4(int s1, int s4, int s2, int s3, int* d[4], bool half) {
    int t0, t1, t2, t3, t4;
    IVI_SLANT_BFLY(s1, s2, t1, t2, t0);
    IVI_IREFLECT(s4, s3, t4, t3, t0);
    IVI_SLANT_BFLY(t1, t4, t1, t4, t0);
    IVI_SLANT_BFLY(t2, t3, t2, t3, t0);
    const int r[4] = {t1, t2, t3, t4};
    for (int i = 0; i < 4; ++i)
        *d[i] = half ? ((r[i] + 1) >> 1) : r[i];
}

#undef IVI_SLANT_BFLY
#undef IVI_IREFLECT
#undef IVI_SLANT_PART4

// Writes int results into int16 outputs (the macros assign int to int16_t).
struct Out16 {
    int v[8];
    int* p[8];
    Out16() {
        for (int i = 0; i < 8; ++i)
            p[i] = &v[i];
    }
};

void inverseSlant8x8(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, const std::uint8_t* flags) {
    int tmp[64];
    const std::int32_t* src = in;
    int* dst = tmp;
    for (int i = 0; i < 8; ++i) {
        if (flags[i]) {
            int* d[8] = {&dst[0], &dst[8], &dst[16], &dst[24], &dst[32], &dst[40], &dst[48], &dst[56]};
            invSlant8(src[0], src[8], src[16], src[24], src[32], src[40], src[48], src[56], d, false);
        } else {
            dst[0] = dst[8] = dst[16] = dst[24] = dst[32] = dst[40] = dst[48] = dst[56] = 0;
        }
        ++src;
        ++dst;
    }
    const int* s = tmp;
    for (int i = 0; i < 8; ++i) {
        if (!s[0] && !s[1] && !s[2] && !s[3] && !s[4] && !s[5] && !s[6] && !s[7]) {
            std::memset(out, 0, 8 * sizeof(out[0]));
        } else {
            Out16 o;
            invSlant8(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], o.p, true);
            for (int k = 0; k < 8; ++k)
                out[k] = static_cast<std::int16_t>(o.v[k]);
        }
        s += 8;
        out += pitch;
    }
}

void inverseSlant4x4(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, const std::uint8_t* flags) {
    int tmp[16];
    const std::int32_t* src = in;
    int* dst = tmp;
    for (int i = 0; i < 4; ++i) {
        if (flags[i]) {
            int* d[4] = {&dst[0], &dst[4], &dst[8], &dst[12]};
            invSlant4(src[0], src[4], src[8], src[12], d, false);
        } else {
            dst[0] = dst[4] = dst[8] = dst[12] = 0;
        }
        ++src;
        ++dst;
    }
    const int* s = tmp;
    for (int i = 0; i < 4; ++i) {
        if (!s[0] && !s[1] && !s[2] && !s[3]) {
            out[0] = out[1] = out[2] = out[3] = 0;
        } else {
            Out16 o;
            invSlant4(s[0], s[1], s[2], s[3], o.p, true);
            for (int k = 0; k < 4; ++k)
                out[k] = static_cast<std::int16_t>(o.v[k]);
        }
        s += 4;
        out += pitch;
    }
}

void rowSlant8(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, const std::uint8_t*) {
    for (int i = 0; i < 8; ++i) {
        if (!in[0] && !in[1] && !in[2] && !in[3] && !in[4] && !in[5] && !in[6] && !in[7]) {
            std::memset(out, 0, 8 * sizeof(out[0]));
        } else {
            Out16 o;
            invSlant8(in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], o.p, true);
            for (int k = 0; k < 8; ++k)
                out[k] = static_cast<std::int16_t>(o.v[k]);
        }
        in += 8;
        out += pitch;
    }
}

void colSlant8(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, const std::uint8_t* flags) {
    const std::ptrdiff_t row2 = pitch << 1, row4 = pitch << 2, row8 = pitch << 3;
    for (int i = 0; i < 8; ++i) {
        std::int16_t* o8[8] = {&out[0],    &out[pitch],         &out[row2],        &out[row2 + pitch],
                               &out[row4], &out[row4 + pitch], &out[row4 + row2], &out[row8 - pitch]};
        if (flags[i]) {
            Out16 o;
            invSlant8(in[0], in[8], in[16], in[24], in[32], in[40], in[48], in[56], o.p, true);
            for (int k = 0; k < 8; ++k)
                *o8[k] = static_cast<std::int16_t>(o.v[k]);
        } else {
            for (int k = 0; k < 8; ++k)
                *o8[k] = 0;
        }
        ++in;
        ++out;
    }
}

void putPixels8x8(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, const std::uint8_t*) {
    for (int y = 0; y < 8; out += pitch, in += 8, ++y)
        for (int x = 0; x < 8; ++x)
            out[x] = static_cast<std::int16_t>(in[x]);
}

void dcSlant2d(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, int blkSize) {
    const auto dc = static_cast<std::int16_t>((*in + 1) >> 1);
    for (int y = 0; y < blkSize; out += pitch, ++y)
        for (int x = 0; x < blkSize; ++x)
            out[x] = dc;
}

void dcRowSlant(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, int blkSize) {
    const auto dc = static_cast<std::int16_t>((*in + 1) >> 1);
    for (int x = 0; x < blkSize; ++x)
        out[x] = dc;
    out += pitch;
    for (int y = 1; y < blkSize; out += pitch, ++y)
        for (int x = 0; x < blkSize; ++x)
            out[x] = 0;
}

void dcColSlant(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, int blkSize) {
    const auto dc = static_cast<std::int16_t>((*in + 1) >> 1);
    for (int y = 0; y < blkSize; out += pitch, ++y) {
        out[0] = dc;
        for (int x = 1; x < blkSize; ++x)
            out[x] = 0;
    }
}

void putDcPixel8x8(const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, int) {
    out[0] = static_cast<std::int16_t>(in[0]);
    std::memset(out + 1, 0, 7 * sizeof(out[0]));
    out += pitch;
    for (int y = 1; y < 8; out += pitch, ++y)
        std::memset(out, 0, 8 * sizeof(out[0]));
}

void invTransform(InvTransform t, const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch,
                  const std::uint8_t* flags) {
    switch (t) {
    case InvTransform::Slant8x8: inverseSlant8x8(in, out, pitch, flags); break;
    case InvTransform::RowSlant8: rowSlant8(in, out, pitch, flags); break;
    case InvTransform::ColSlant8: colSlant8(in, out, pitch, flags); break;
    case InvTransform::PutPixels8x8: putPixels8x8(in, out, pitch, flags); break;
    case InvTransform::Slant4x4: inverseSlant4x4(in, out, pitch, flags); break;
    }
}

void dcTransform(DcTransform t, const std::int32_t* in, std::int16_t* out, std::ptrdiff_t pitch, int blkSize) {
    switch (t) {
    case DcTransform::Slant2d: dcSlant2d(in, out, pitch, blkSize); break;
    case DcTransform::RowSlant: dcRowSlant(in, out, pitch, blkSize); break;
    case DcTransform::ColSlant: dcColSlant(in, out, pitch, blkSize); break;
    case DcTransform::PutDcPixel8x8: putDcPixel8x8(in, out, pitch, blkSize); break;
    }
}

// Motion compensation (IVI_MC_TEMPLATE), with OP = put (no delta) or add (delta).
template <int Size, bool Add>
void motionComp(std::int16_t* buf, const std::int16_t* ref, std::ptrdiff_t pitch, int mcType) {
    auto op = [](std::int16_t& a, int b) { a = static_cast<std::int16_t>(Add ? a + b : b); };
    switch (mcType) {
    case 0:
        for (int i = 0; i < Size; ++i, buf += pitch, ref += pitch)
            for (int j = 0; j < Size; ++j)
                op(buf[j], ref[j]);
        break;
    case 1:
        for (int i = 0; i < Size; ++i, buf += pitch, ref += pitch)
            for (int j = 0; j < Size; ++j)
                op(buf[j], (ref[j] + ref[j + 1]) >> 1);
        break;
    case 2: {
        const std::int16_t* w = ref + pitch;
        for (int i = 0; i < Size; ++i, buf += pitch, w += pitch, ref += pitch)
            for (int j = 0; j < Size; ++j)
                op(buf[j], (ref[j] + w[j]) >> 1);
        break;
    }
    case 3: {
        const std::int16_t* w = ref + pitch;
        for (int i = 0; i < Size; ++i, buf += pitch, w += pitch, ref += pitch)
            for (int j = 0; j < Size; ++j)
                op(buf[j], (ref[j] + ref[j + 1] + w[j] + w[j + 1]) >> 2);
        break;
    }
    default: break;
    }
}

std::uint8_t clipU8(int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); }

// ff_ivi_recompose53: 5/3 wavelet recomposition of the four luma bands.
void recompose53(const Plane& plane, std::uint8_t* dst, std::ptrdiff_t dstPitch) {
    int x, y, indx;
    int p0, p1, p2, p3, tmp0, tmp1, tmp2;
    int b0_1 = 0, b0_2 = 0, b1_1 = 0, b1_2 = 0, b1_3 = 0, b2_1 = 0, b2_2 = 0, b2_3 = 0, b2_4 = 0, b2_5 = 0, b2_6 = 0;
    int b3_1 = 0, b3_2 = 0, b3_3 = 0, b3_4 = 0, b3_5 = 0, b3_6 = 0, b3_7 = 0, b3_8 = 0, b3_9 = 0;
    std::ptrdiff_t pitch = plane.bands[0].pitch, backPitch = 0;
    const std::int16_t* b0_ptr = plane.bands[0].buf;
    const std::int16_t* b1_ptr = plane.bands[1].buf;
    const std::int16_t* b2_ptr = plane.bands[2].buf;
    const std::int16_t* b3_ptr = plane.bands[3].buf;

    for (y = 0; y < plane.height; y += 2) {
        if (y + 2 >= plane.height)
            pitch = 0;
        b0_1 = b0_ptr[0];
        b0_2 = b0_ptr[pitch];

        b1_1 = b1_ptr[backPitch];
        b1_2 = b1_ptr[0];
        b1_3 = b1_1 - b1_2 * 6 + b1_ptr[pitch];

        b2_2 = b2_ptr[0];
        b2_3 = b2_2;
        b2_5 = b2_ptr[pitch];
        b2_6 = b2_5;

        b3_2 = b3_ptr[backPitch];
        b3_3 = b3_2;
        b3_5 = b3_ptr[0];
        b3_6 = b3_5;
        b3_8 = b3_2 - b3_5 * 6 + b3_ptr[pitch];
        b3_9 = b3_8;

        for (x = 0, indx = 0; x < plane.width; x += 2, ++indx) {
            if (x + 2 >= plane.width) {
                --b0_ptr;
                --b1_ptr;
                --b2_ptr;
                --b3_ptr;
            }
            b2_1 = b2_2;
            b2_2 = b2_3;
            b2_4 = b2_5;
            b2_5 = b2_6;
            b3_1 = b3_2;
            b3_2 = b3_3;
            b3_4 = b3_5;
            b3_5 = b3_6;
            b3_7 = b3_8;
            b3_8 = b3_9;

            // LL band
            tmp0 = b0_1;
            tmp2 = b0_2;
            b0_1 = b0_ptr[indx + 1];
            b0_2 = b0_ptr[pitch + indx + 1];
            tmp1 = tmp0 + b0_1;
            p0 = tmp0 * 16;
            p1 = tmp1 * 8;
            p2 = (tmp0 + tmp2) * 8;
            p3 = (tmp1 + tmp2 + b0_2) * 4;

            // HL band
            tmp0 = b1_2;
            tmp1 = b1_1;
            b1_2 = b1_ptr[indx + 1];
            b1_1 = b1_ptr[backPitch + indx + 1];
            tmp2 = tmp1 - tmp0 * 6 + b1_3;
            b1_3 = b1_1 - b1_2 * 6 + b1_ptr[pitch + indx + 1];
            p0 += (tmp0 + tmp1) * 8;
            p1 += (tmp0 + tmp1 + b1_1 + b1_2) * 4;
            p2 += tmp2 * 4;
            p3 += (tmp2 + b1_3) * 2;

            // LH band
            b2_3 = b2_ptr[indx + 1];
            b2_6 = b2_ptr[pitch + indx + 1];
            tmp0 = b2_1 + b2_2;
            tmp1 = b2_1 - b2_2 * 6 + b2_3;
            p0 += tmp0 * 8;
            p1 += tmp1 * 4;
            p2 += (tmp0 + b2_4 + b2_5) * 4;
            p3 += (tmp1 + b2_4 - b2_5 * 6 + b2_6) * 2;

            // HH band
            b3_6 = b3_ptr[indx + 1];
            b3_3 = b3_ptr[backPitch + indx + 1];
            tmp0 = b3_1 + b3_4;
            tmp1 = b3_2 + b3_5;
            tmp2 = b3_3 + b3_6;
            b3_9 = b3_3 - b3_6 * 6 + b3_ptr[pitch + indx + 1];
            p0 += (tmp0 + tmp1) * 4;
            p1 += (tmp0 - tmp1 * 6 + tmp2) * 2;
            p2 += (b3_7 + b3_8) * 2;
            p3 += b3_7 - b3_8 * 6 + b3_9;

            dst[x] = clipU8((p0 >> 6) + 128);
            dst[x + 1] = clipU8((p1 >> 6) + 128);
            dst[dstPitch + x] = clipU8((p2 >> 6) + 128);
            dst[dstPitch + x + 1] = clipU8((p3 >> 6) + 128);
        }
        dst += dstPitch << 1;
        backPitch = -pitch;
        b0_ptr += pitch + 1;
        b1_ptr += pitch + 1;
        b2_ptr += pitch + 1;
        b3_ptr += pitch + 1;
    }
}

// ivi_output_plane: add the 128 bias back and clip.
void outputPlane(const Plane& plane, std::uint8_t* dst, std::ptrdiff_t dstPitch) {
    const std::int16_t* src = plane.bands[0].buf;
    const std::ptrdiff_t pitch = plane.bands[0].pitch;
    if (!src)
        return;
    for (int y = 0; y < plane.height; ++y) {
        for (int x = 0; x < plane.width; ++x)
            dst[x] = clipU8(src[x] + 128);
        src += pitch;
        dst += dstPitch;
    }
}

constexpr int alignUp(int v, int a) { return (v + a - 1) & ~(a - 1); }
constexpr int numTiles(int stride, int tileSize) { return (stride + tileSize - 1) / tileSize; }
constexpr int mbsPerTile(int w, int h, int mb) { return ((w + mb - 1) / mb) * ((h + mb - 1) / mb); }

} // namespace

// ---------------------------------------------------------------------------
// Decoder context (IVI45DecContext)

struct Indeo5Decoder::Impl {
    BitReader gb;
    RVMapDesc rvmapTabs[9];
    int frameNum = 0;
    int frameType = kIntra;
    int prevFrameType = kIntra;
    int isScalable = 0;
    const std::uint8_t* frameData = nullptr;
    int interScal = 0;
    int frameFlags = 0;
    int picHdrSize = 0;
    int checksum = 0;
    PicConfig picConf;
    Plane planes[3];
    int bufSwitch = 0, dstBuf = 0, refBuf = 0, ref2Buf = 0;
    HuffTab mbVlc;
    int gopFlags = 0, gopHdrSize = 0;
    std::uint32_t lockWord = 0;
    int gopInvalid = 1;
    int bufInvalid[4]{};
    std::string lastError;

    Impl() { std::memcpy(rvmapTabs, ff_ivi_rvmap_tabs, sizeof(rvmapTabs)); }

    bool fail(std::string msg) {
        lastError = std::move(msg);
        return false;
    }
    Result error(std::string msg) {
        fail(std::move(msg));
        return Result::Error;
    }

    // ff_ivi_dec_huff_desc
    bool decHuffDesc(int descCoded, bool blockTab, HuffTab& huff) {
        const StaticVlcs& s = staticVlcs();
        if (!descCoded) {
            huff.tab = blockTab ? &s.blk[7] : &s.mb[7];
            return true;
        }
        huff.tabSel = static_cast<int>(gb.get(3));
        if (huff.tabSel == 7) {
            HuffDesc desc;
            desc.numRows = static_cast<int>(gb.get(4));
            if (!desc.numRows)
                return fail("empty custom Huffman table");
            for (int i = 0; i < desc.numRows; ++i)
                desc.xbits[i] = static_cast<std::uint8_t>(gb.get(4));
            if (!(desc == huff.custDesc) || !huff.custTab.valid()) {
                huff.custDesc = desc;
                if (!createHuffFromDesc(huff.custDesc, huff.custTab)) {
                    huff.custDesc.numRows = 0;
                    return fail("invalid custom Huffman table");
                }
            }
            huff.tab = &huff.custTab;
        } else {
            huff.tab = blockTab ? &s.blk[huff.tabSel] : &s.mb[huff.tabSel];
        }
        return true;
    }

    // ff_ivi_init_planes
    bool initPlanes(const PicConfig& cfg) {
        if (cfg.picWidth <= 0 || cfg.picHeight <= 0 || cfg.picWidth > 8192 || cfg.picHeight > 8192 ||
            cfg.lumaBands < 1 || cfg.chromaBands < 1)
            return fail("invalid picture size");
        planes[0].width = cfg.picWidth;
        planes[0].height = cfg.picHeight;
        planes[0].bands.assign(static_cast<std::size_t>(cfg.lumaBands), Band{});
        planes[1].width = planes[2].width = (cfg.picWidth + 3) >> 2;
        planes[1].height = planes[2].height = (cfg.picHeight + 3) >> 2;
        planes[1].bands.assign(static_cast<std::size_t>(cfg.chromaBands), Band{});
        planes[2].bands.assign(static_cast<std::size_t>(cfg.chromaBands), Band{});
        for (int p = 0; p < 3; ++p) {
            Plane& pl = planes[p];
            const int nb = static_cast<int>(pl.bands.size());
            const int bw = nb == 1 ? pl.width : (pl.width + 1) >> 1;
            const int bh = nb == 1 ? pl.height : (pl.height + 1) >> 1;
            const int alignFac = p ? 8 : 16;
            const int wa = alignUp(bw, alignFac), ha = alignUp(bh, alignFac);
            for (int b = 0; b < nb; ++b) {
                Band& band = pl.bands[static_cast<std::size_t>(b)];
                band.plane = p;
                band.bandNum = b;
                band.width = bw;
                band.height = bh;
                band.pitch = wa;
                band.aheight = ha;
                band.bufsize = wa * ha;
            }
        }
        return true;
    }

    // ivi_init_tiles / ff_ivi_init_tiles
    bool initTiles(int tileWidth, int tileHeight) {
        for (int p = 0; p < 3; ++p) {
            int tw = !p ? tileWidth : (tileWidth + 3) >> 2;
            int th = !p ? tileHeight : (tileHeight + 3) >> 2;
            if (!p && planes[0].bands.size() == 4) {
                if (tw % 2 || th % 2)
                    return fail("odd tiles");
                tw >>= 1;
                th >>= 1;
            }
            if (tw <= 0 || th <= 0)
                return fail("invalid tile size");
            for (auto& band : planes[p].bands) {
                const int nt = numTiles(band.width, tw) * numTiles(band.height, th);
                band.tiles.assign(static_cast<std::size_t>(nt), Tile{});
                Tile* refTile = planes[0].bands[0].tiles.data();
                std::size_t t = 0;
                for (int y = 0; y < band.height; y += th) {
                    for (int x = 0; x < band.width; x += tw) {
                        Tile& tile = band.tiles[t++];
                        tile.xpos = x;
                        tile.ypos = y;
                        tile.mbSize = band.mbSize;
                        tile.width = std::min(band.width - x, tw);
                        tile.height = std::min(band.height - y, th);
                        tile.isEmpty = tile.dataSize = 0;
                        tile.numMBs = mbsPerTile(tile.width, tile.height, band.mbSize);
                        tile.mbs.assign(static_cast<std::size_t>(tile.numMBs), MbInfo{});
                        tile.refMbs = nullptr;
                        if (p || band.bandNum) {
                            if (tile.numMBs != refTile->numMBs)
                                return fail("reference tile mismatch");
                            tile.refMbs = refTile->mbs.data();
                            ++refTile;
                        }
                    }
                }
            }
        }
        return true;
    }

    // decode_gop_header (indeo5.c)
    bool decodeGopHeader() {
        int blkSizeChanged = 0;
        PicConfig pc;

        gopFlags = static_cast<int>(gb.get(8));
        gopHdrSize = (gopFlags & 1) ? static_cast<int>(gb.get(16)) : 0;
        if (gopFlags & kIsProtected)
            lockWord = gb.get(32);
        const int tileSize = (gopFlags & 0x40) ? 64 << gb.get(2) : 0;
        if (tileSize > 256)
            return fail(std::format("invalid tile size {}", tileSize));

        pc.lumaBands = static_cast<int>(gb.get(2)) * 3 + 1;
        pc.chromaBands = static_cast<int>(gb.get1()) * 3 + 1;
        const int scalable = pc.lumaBands != 1 || pc.chromaBands != 1;
        if (scalable && (pc.lumaBands != 4 || pc.chromaBands != 1))
            return fail("unsupported scalability subdivision");

        const int picSizeIndx = static_cast<int>(gb.get(4));
        if (picSizeIndx == kPicSizeEsc) {
            pc.picHeight = static_cast<int>(gb.get(13));
            pc.picWidth = static_cast<int>(gb.get(13));
        } else {
            pc.picHeight = ivi5_common_pic_sizes[picSizeIndx * 2 + 1] << 2;
            pc.picWidth = ivi5_common_pic_sizes[picSizeIndx * 2] << 2;
        }
        if (gopFlags & 2)
            return fail("YV12 picture format not supported");

        pc.chromaHeight = (pc.picHeight + 3) >> 2;
        pc.chromaWidth = (pc.picWidth + 3) >> 2;
        if (!tileSize) {
            pc.tileHeight = pc.picHeight;
            pc.tileWidth = pc.picWidth;
        } else {
            pc.tileHeight = pc.tileWidth = tileSize;
        }

        if (!(pc == picConf) || gopInvalid) {
            if (!initPlanes(pc))
                return false;
            picConf = pc;
            isScalable = scalable;
            blkSizeChanged = 1;
        }

        for (int p = 0; p <= 1; ++p) {
            for (int i = 0; i < (!p ? pc.lumaBands : pc.chromaBands); ++i) {
                Band& band = planes[p].bands[static_cast<std::size_t>(i)];
                band.isHalfpel = static_cast<int>(gb.get1());
                int mbSize = static_cast<int>(gb.get1());
                const int blkSize = 8 >> gb.get1();
                mbSize = blkSize << !mbSize;
                if (p == 0 && blkSize == 4)
                    return fail("4x4 luma blocks are unsupported");
                blkSizeChanged = mbSize != band.mbSize || blkSize != band.blkSize;
                if (blkSizeChanged) {
                    band.mbSize = mbSize;
                    band.blkSize = blkSize;
                }
                if (gb.get1())
                    return fail("extended transform info not supported");

                switch ((p << 2) + i) {
                case 0:
                    band.invTransform = InvTransform::Slant8x8;
                    band.dcTransform = DcTransform::Slant2d;
                    band.scan = ff_zigzag_direct;
                    band.transformSize = 8;
                    break;
                case 1:
                    band.invTransform = InvTransform::RowSlant8;
                    band.dcTransform = DcTransform::RowSlant;
                    band.scan = ff_ivi_vertical_scan_8x8;
                    band.transformSize = 8;
                    break;
                case 2:
                    band.invTransform = InvTransform::ColSlant8;
                    band.dcTransform = DcTransform::ColSlant;
                    band.scan = ff_ivi_horizontal_scan_8x8;
                    band.transformSize = 8;
                    break;
                case 3:
                    band.invTransform = InvTransform::PutPixels8x8;
                    band.dcTransform = DcTransform::PutDcPixel8x8;
                    band.scan = ff_ivi_horizontal_scan_8x8;
                    band.transformSize = 8;
                    break;
                case 4:
                    band.invTransform = InvTransform::Slant4x4;
                    band.dcTransform = DcTransform::Slant2d;
                    band.scan = ff_ivi_direct_scan_4x4;
                    band.transformSize = 4;
                    break;
                }
                band.is2dTrans = band.invTransform == InvTransform::Slant8x8 ||
                                 band.invTransform == InvTransform::Slant4x4;
                if (band.transformSize != band.blkSize)
                    return fail("transform and block size mismatch");

                const int quantMat = !p ? (pc.lumaBands > 1 ? i + 1 : 0) : 5;
                if (band.blkSize == 8) {
                    if (quantMat >= 5)
                        return fail("quant matrix index too large");
                    band.intraBase = &ivi5_base_quant_8x8_intra[quantMat][0];
                    band.interBase = &ivi5_base_quant_8x8_inter[quantMat][0];
                    band.intraScale = &ivi5_scale_quant_8x8_intra[quantMat][0];
                    band.interScale = &ivi5_scale_quant_8x8_inter[quantMat][0];
                } else {
                    band.intraBase = ivi5_base_quant_4x4_intra;
                    band.interBase = ivi5_base_quant_4x4_inter;
                    band.intraScale = ivi5_scale_quant_4x4_intra;
                    band.interScale = ivi5_scale_quant_4x4_inter;
                }
                if (gb.get(2))
                    return fail("end marker missing");
            }
        }

        // copy chroma parameters into the 2nd chroma plane
        for (int i = 0; i < pc.chromaBands; ++i) {
            const Band& b1 = planes[1].bands[static_cast<std::size_t>(i)];
            Band& b2 = planes[2].bands[static_cast<std::size_t>(i)];
            b2.width = b1.width;
            b2.height = b1.height;
            b2.mbSize = b1.mbSize;
            b2.blkSize = b1.blkSize;
            b2.isHalfpel = b1.isHalfpel;
            b2.intraBase = b1.intraBase;
            b2.interBase = b1.interBase;
            b2.intraScale = b1.intraScale;
            b2.interScale = b1.interScale;
            b2.scan = b1.scan;
            b2.invTransform = b1.invTransform;
            b2.dcTransform = b1.dcTransform;
            b2.is2dTrans = b1.is2dTrans;
            b2.transformSize = b1.transformSize;
        }

        if (blkSizeChanged && !initTiles(pc.tileWidth, pc.tileHeight))
            return false;

        if (gopFlags & 8) {
            if (gb.get(3))
                return fail("alignment bits are not zero");
            if (gb.get1())
                gb.skip(24); // transparency fill colour
        }
        gb.align();
        gb.skip(23); // unknown meaning
        if (gb.get1()) {
            int i;
            do {
                i = static_cast<int>(gb.get(16));
            } while (i & 0x8000);
        }
        gb.align();
        return true;
    }

    bool skipHdrExtension() {
        int len;
        do {
            len = static_cast<int>(gb.get(8));
            if (8 * len > gb.left())
                return fail("truncated header extension");
            for (int i = 0; i < len; ++i)
                gb.skip(8);
        } while (len);
        return true;
    }

    // decode_pic_hdr (indeo5.c)
    bool decodePicHdr() {
        if (gb.get(5) != 0x1F)
            return fail("invalid picture start code");
        prevFrameType = frameType;
        frameType = static_cast<int>(gb.get(3));
        if (frameType >= 5) {
            frameType = kIntra;
            return fail("invalid frame type");
        }
        frameNum = static_cast<int>(gb.get(8));
        if (frameType == kIntra) {
            if (!decodeGopHeader()) {
                gopInvalid = 1;
                return false;
            }
            gopInvalid = 0;
        }
        if (frameType == kInterScal && !isScalable) {
            frameType = kInter;
            return fail("scalable inter frame in a non-scalable stream");
        }
        if (frameType != kNull) {
            frameFlags = static_cast<int>(gb.get(8));
            picHdrSize = (frameFlags & 1) ? static_cast<int>(gb.get(24)) : 0;
            checksum = (frameFlags & 0x10) ? static_cast<int>(gb.get(16)) : 0;
            if (frameFlags & 0x20)
                skipHdrExtension();
            if (!decHuffDesc(frameFlags & 0x40, false, mbVlc))
                return false;
            gb.skip(3); // unknown meaning
        }
        gb.align();
        return true;
    }

    // decode_band_hdr (indeo5.c)
    bool decodeBandHdr(Band& band) {
        const int bandFlags = static_cast<int>(gb.get(8));
        if (bandFlags & 1) {
            band.isEmpty = 1;
            return true;
        }
        band.isEmpty = 0;
        band.dataSize = (frameFlags & 0x80) ? static_cast<int>(gb.get(24)) : 0;
        band.inheritMv = bandFlags & 2;
        band.inheritQdelta = bandFlags & 8;
        band.qdeltaPresent = bandFlags & 4;
        if (!band.qdeltaPresent)
            band.inheritQdelta = 1;

        band.numCorr = 0;
        if (bandFlags & 0x10) {
            band.numCorr = static_cast<int>(gb.get(8));
            if (band.numCorr > 61)
                return fail("too many rvmap corrections");
            for (int i = 0; i < band.numCorr * 2; ++i)
                band.corr[i] = static_cast<std::uint8_t>(gb.get(8));
        }
        band.rvmapSel = (bandFlags & 0x40) ? static_cast<int>(gb.get(3)) : 8;
        if (!decHuffDesc(bandFlags & 0x80, true, band.blkVlc))
            return false;
        band.checksumPresent = static_cast<int>(gb.get1());
        if (band.checksumPresent)
            band.checksum = static_cast<int>(gb.get(16));
        band.globQuant = static_cast<int>(gb.get(5));
        if (bandFlags & 0x20) {
            gb.align();
            skipHdrExtension();
        }
        gb.align();
        return true;
    }

    // decode_mb_info (indeo5.c)
    bool decodeMbInfo(Band& band, Tile& tile) {
        const int rowOffset = band.mbSize * band.pitch;
        MbInfo* mb = tile.mbs.data();
        MbInfo* refMb = tile.refMbs;
        int offs = tile.ypos * band.pitch + tile.xpos;

        if (!refMb && ((band.qdeltaPresent && band.inheritQdelta) || band.inheritMv))
            return fail("missing reference macroblocks");
        if (tile.numMBs != mbsPerTile(tile.width, tile.height, band.mbSize))
            return fail("tile size mismatch");

        const int mvScale = (planes[0].bands[0].mbSize >> 3) - (band.mbSize >> 3);
        int mvX = 0, mvY = 0;

        for (int y = tile.ypos; y < tile.ypos + tile.height; y += band.mbSize) {
            int mbOffset = offs;
            for (int x = tile.xpos; x < tile.xpos + tile.width; x += band.mbSize) {
                mb->xpos = static_cast<std::int16_t>(x);
                mb->ypos = static_cast<std::int16_t>(y);
                mb->bufOffs = static_cast<std::uint32_t>(mbOffset);

                if (gb.get1()) {
                    if (frameType == kIntra)
                        return fail("empty macroblock in an intra picture");
                    mb->type = 1;
                    mb->cbp = 0;
                    mb->qDelta = 0;
                    if (!band.plane && !band.bandNum && (frameFlags & 8))
                        mb->qDelta = static_cast<std::int8_t>(toSigned(getVlc(gb, *mbVlc.tab)));
                    mb->mvX = mb->mvY = 0;
                    if (band.inheritMv && refMb) {
                        if (mvScale) {
                            mb->mvX = static_cast<std::int8_t>(scaleMv(refMb->mvX, mvScale));
                            mb->mvY = static_cast<std::int8_t>(scaleMv(refMb->mvY, mvScale));
                        } else {
                            mb->mvX = refMb->mvX;
                            mb->mvY = refMb->mvY;
                        }
                    }
                } else {
                    if (band.inheritMv && refMb)
                        mb->type = refMb->type;
                    else if (frameType == kIntra)
                        mb->type = 0;
                    else
                        mb->type = static_cast<std::uint8_t>(gb.get1());

                    const int blksPerMb = band.mbSize != band.blkSize ? 4 : 1;
                    mb->cbp = static_cast<std::uint8_t>(gb.get(blksPerMb));

                    mb->qDelta = 0;
                    if (band.qdeltaPresent) {
                        if (band.inheritQdelta) {
                            if (refMb)
                                mb->qDelta = refMb->qDelta;
                        } else if (mb->cbp || (!band.plane && !band.bandNum && (frameFlags & 8))) {
                            mb->qDelta = static_cast<std::int8_t>(toSigned(getVlc(gb, *mbVlc.tab)));
                        }
                    }

                    if (!mb->type) {
                        mb->mvX = mb->mvY = 0;
                    } else if (band.inheritMv && refMb) {
                        if (mvScale) {
                            mb->mvX = static_cast<std::int8_t>(scaleMv(refMb->mvX, mvScale));
                            mb->mvY = static_cast<std::int8_t>(scaleMv(refMb->mvY, mvScale));
                        } else {
                            mb->mvX = refMb->mvX;
                            mb->mvY = refMb->mvY;
                        }
                    } else {
                        mvY += toSigned(getVlc(gb, *mbVlc.tab));
                        mvX += toSigned(getVlc(gb, *mbVlc.tab));
                        mb->mvX = static_cast<std::int8_t>(mvX);
                        mb->mvY = static_cast<std::int8_t>(mvY);
                    }
                }

                const int s = band.isHalfpel;
                if (mb->type &&
                    (x + (mb->mvX >> s) + (y + (mb->mvY >> s)) * band.pitch < 0 ||
                     x + ((mb->mvX + s) >> s) + band.mbSize - 1 +
                             (y + band.mbSize - 1 + ((mb->mvY + s) >> s)) * band.pitch >
                         band.bufsize - 1))
                    return fail("motion vector outside reference");

                ++mb;
                if (refMb)
                    ++refMb;
                mbOffset += band.mbSize;
            }
            offs += rowOffset;
        }
        gb.align();
        return true;
    }

    // ivi_mc (Indeo 5 never uses the second, bidirectional vector)
    template <bool Add>
    bool mc(const Band& band, int offs, int mvX, int mvY, int mcType) {
        const int refOffs = offs + mvY * band.pitch + mvX;
        const int bufSize = band.pitch * band.aheight;
        const int minSize = band.pitch * (band.blkSize - 1) + band.blkSize;
        const int refSize = (mcType > 1) * band.pitch + (mcType & 1);
        if (offs < 0 || refOffs < 0 || !band.refBuf || bufSize - minSize < offs ||
            bufSize - minSize - refSize < refOffs)
            return fail("motion compensation out of bounds");
        if (band.blkSize == 8)
            motionComp<8, Add>(band.buf + offs, band.refBuf + refOffs, band.pitch, mcType);
        else
            motionComp<4, Add>(band.buf + offs, band.refBuf + refOffs, band.pitch, mcType);
        return true;
    }

    // ivi_decode_coded_blocks
    bool decodeCodedBlock(const Band& band, int mvX, int mvY, int* prevDc, int isIntra, int mcType,
                          std::uint32_t quant, int offs) {
        const std::uint16_t* baseTab = isIntra ? band.intraBase : band.interBase;
        const RVMapDesc* rvmap = band.rvMap;
        std::uint8_t colFlags[8]{};
        std::int32_t trvec[64]{};
        std::uint32_t sym = 0;
        const int blkSize = band.blkSize;
        const int numCoeffs = blkSize * blkSize;
        const int colMask = blkSize - 1;
        int scanPos = -1;
        const int minSize = band.pitch * (band.transformSize - 1) + band.transformSize;
        const int bufSize = band.pitch * band.aheight - offs;
        if (minSize > bufSize)
            return fail("block outside the band");
        if (!band.scan)
            return fail("scan pattern not set");

        while (scanPos <= numCoeffs) {
            sym = static_cast<std::uint32_t>(getVlc(gb, *band.blkVlc.tab));
            if (sym == rvmap->eob_sym)
                break;
            int run, val;
            if (sym == rvmap->esc_sym) {
                run = getVlc(gb, *band.blkVlc.tab) + 1;
                const std::uint32_t lo = static_cast<std::uint32_t>(getVlc(gb, *band.blkVlc.tab));
                const std::uint32_t hi = static_cast<std::uint32_t>(getVlc(gb, *band.blkVlc.tab));
                val = toSigned(static_cast<int>((hi << 6) | lo));
            } else {
                if (sym >= 256u)
                    return fail("invalid block symbol");
                run = rvmap->runtab[sym];
                val = rvmap->valtab[sym];
            }
            scanPos += run;
            if (scanPos >= numCoeffs || scanPos < 0)
                break;
            const int pos = band.scan[scanPos];
            const std::uint32_t q = (baseTab[pos] * quant) >> 9;
            // FFSIGN(0) is -1 in FFmpeg; keep it for bit-exact output.
            if (q > 1)
                val = val * static_cast<int>(q) + (val > 0 ? 1 : -1) * static_cast<int>(((q ^ 1) - 1) >> 1);
            trvec[pos] = val;
            colFlags[pos & colMask] |= !!val;
        }
        if (scanPos < 0 || (scanPos >= numCoeffs && sym != rvmap->eob_sym))
            return fail("corrupt block data");

        if (isIntra && band.is2dTrans) {
            *prevDc += trvec[0];
            trvec[0] = *prevDc;
            colFlags[0] |= !!*prevDc;
        }
        if (band.transformSize > band.blkSize)
            return fail("transform too large");
        invTransform(band.invTransform, trvec, band.buf + offs, band.pitch, colFlags);
        if (!isIntra)
            return mc<true>(band, offs, mvX, mvY, mcType);
        return true;
    }

    // ivi_decode_blocks
    bool decodeBlocks(const Band& band, Tile& tile) {
        int prevDc = 0;
        const int blkSize = band.blkSize;
        const int numBlocks = band.mbSize != blkSize ? 4 : 1;
        int mcType = 0;
        int mvX = 0, mvY = 0;

        for (int mbn = 0; mbn < tile.numMBs; ++mbn) {
            const MbInfo& mb = tile.mbs[static_cast<std::size_t>(mbn)];
            const int isIntra = !mb.type;
            std::uint32_t cbp = mb.cbp;
            std::uint32_t bufOffs = mb.bufOffs;

            std::uint32_t quant = static_cast<std::uint32_t>(std::clamp(band.globQuant + mb.qDelta, 0, 23));
            const std::uint8_t* scaleTab = isIntra ? band.intraScale : band.interScale;
            if (scaleTab)
                quant = scaleTab[quant];

            if (!isIntra) {
                mvX = mb.mvX;
                mvY = mb.mvY;
                if (band.isHalfpel) {
                    mcType = ((mvY & 1) << 1) | (mvX & 1);
                    mvX >>= 1;
                    mvY >>= 1;
                }
                if (mb.type) {
                    const int dmvX = mb.mvX >> band.isHalfpel;
                    const int dmvY = mb.mvY >> band.isHalfpel;
                    const int cx = mb.mvX & band.isHalfpel;
                    const int cy = mb.mvY & band.isHalfpel;
                    if (mb.xpos + dmvX < 0 || mb.xpos + dmvX + band.mbSize + cx > band.pitch ||
                        mb.ypos + dmvY < 0 || mb.ypos + dmvY + band.mbSize + cy > band.aheight)
                        return fail("motion vector out of bounds");
                }
            }

            for (int blk = 0; blk < numBlocks; ++blk) {
                if (blk & 1) {
                    bufOffs += static_cast<std::uint32_t>(blkSize);
                } else if (blk == 2) {
                    bufOffs -= static_cast<std::uint32_t>(blkSize);
                    bufOffs += static_cast<std::uint32_t>(blkSize * band.pitch);
                }
                if (cbp & 1) {
                    if (!decodeCodedBlock(band, mvX, mvY, &prevDc, isIntra, mcType, quant, static_cast<int>(bufOffs)))
                        return false;
                } else {
                    const int bufSize = band.pitch * band.aheight - static_cast<int>(bufOffs);
                    const int minSize = (blkSize - 1) * band.pitch + blkSize;
                    if (minSize > bufSize)
                        return fail("block outside the band");
                    if (isIntra) {
                        dcTransform(band.dcTransform, &prevDc, band.buf + bufOffs, band.pitch, blkSize);
                    } else if (!mc<false>(band, static_cast<int>(bufOffs), mvX, mvY, mcType)) {
                        return false;
                    }
                }
                cbp >>= 1;
            }
        }
        gb.align();
        return true;
    }

    // ivi_process_empty_tile
    bool processEmptyTile(const Band& band, Tile& tile, int mvScale) {
        const int clearFirst = !band.qdeltaPresent && !band.plane && !band.bandNum;
        const int mbSize = band.mbSize;
        const int xend = tile.xpos + tile.width;
        const int isHalfpel = band.isHalfpel;
        const int pitch = band.pitch;
        if (tile.numMBs != mbsPerTile(tile.width, tile.height, mbSize))
            return fail("tile size mismatch");

        int offs = tile.ypos * pitch + tile.xpos;
        MbInfo* mb = tile.mbs.data();
        MbInfo* refMb = tile.refMbs;
        const int rowOffset = mbSize * pitch;
        int needMc = 0;

        for (int y = tile.ypos; y < tile.ypos + tile.height; y += mbSize) {
            int mbOffset = offs;
            for (int x = tile.xpos; x < xend; x += mbSize) {
                mb->xpos = static_cast<std::int16_t>(x);
                mb->ypos = static_cast<std::int16_t>(y);
                mb->bufOffs = static_cast<std::uint32_t>(mbOffset);
                mb->type = 1;
                mb->cbp = 0;
                if (clearFirst) {
                    mb->qDelta = static_cast<std::int8_t>(band.globQuant);
                    mb->mvX = 0;
                    mb->mvY = 0;
                }
                if (refMb) {
                    if (band.inheritQdelta)
                        mb->qDelta = refMb->qDelta;
                    if (band.inheritMv) {
                        if (mvScale) {
                            mb->mvX = static_cast<std::int8_t>(scaleMv(refMb->mvX, mvScale));
                            mb->mvY = static_cast<std::int8_t>(scaleMv(refMb->mvY, mvScale));
                        } else {
                            mb->mvX = refMb->mvX;
                            mb->mvY = refMb->mvY;
                        }
                        needMc |= mb->mvX || mb->mvY;
                        const int dmvX = mb->mvX >> isHalfpel, dmvY = mb->mvY >> isHalfpel;
                        const int cx = mb->mvX & isHalfpel, cy = mb->mvY & isHalfpel;
                        if (mb->xpos + dmvX < 0 || mb->xpos + dmvX + mbSize + cx > pitch || mb->ypos + dmvY < 0 ||
                            mb->ypos + dmvY + mbSize + cy > band.aheight)
                            return fail("motion vector out of bounds");
                    }
                    ++refMb;
                }
                ++mb;
                mbOffset += mbSize;
            }
            offs += rowOffset;
        }

        if (band.inheritMv && needMc) {
            const int numBlocks = mbSize != band.blkSize ? 4 : 1;
            for (int mbn = 0; mbn < tile.numMBs; ++mbn) {
                const MbInfo& info = tile.mbs[static_cast<std::size_t>(mbn)];
                int mvX = info.mvX, mvY = info.mvY, mcType;
                if (!band.isHalfpel) {
                    mcType = 0;
                } else {
                    mcType = ((mvY & 1) << 1) | (mvX & 1);
                    mvX >>= 1;
                    mvY >>= 1;
                }
                for (int blk = 0; blk < numBlocks; ++blk) {
                    const int o = static_cast<int>(info.bufOffs) + band.blkSize * ((blk & 1) + !!(blk & 2) * pitch);
                    if (!mc<false>(band, o, mvX, mvY, mcType))
                        return false;
                }
            }
        } else {
            const std::int16_t* src = band.refBuf + tile.ypos * pitch + tile.xpos;
            std::int16_t* dst = band.buf + tile.ypos * pitch + tile.xpos;
            for (int y = 0; y < tile.height; ++y) {
                std::memcpy(dst, src, static_cast<std::size_t>(tile.width) * sizeof(dst[0]));
                src += pitch;
                dst += pitch;
            }
        }
        return true;
    }

    // ivi_dec_tile_data_size
    int decTileDataSize() {
        int len = 0;
        if (gb.get1()) {
            len = static_cast<int>(gb.get(8));
            if (len == 255)
                len = static_cast<int>(gb.get(24));
        }
        gb.align();
        return len;
    }

    std::int16_t* prepareBuf(Band& band, int i) {
        if (picConf.lumaBands <= 1 && i == 2)
            return nullptr;
        auto& b = band.bufs[static_cast<std::size_t>(i)];
        if (b.empty())
            b.assign(static_cast<std::size_t>(band.bufsize), 0);
        return b.data();
    }

    void applyCorrection(RVMapDesc& map, int idx1, int idx2) {
        std::swap(map.runtab[idx1], map.runtab[idx2]);
        std::swap(map.valtab[idx1], map.valtab[idx2]);
        if (idx1 == map.eob_sym || idx2 == map.eob_sym)
            map.eob_sym = static_cast<std::uint8_t>(map.eob_sym ^ idx1 ^ idx2);
        if (idx1 == map.esc_sym || idx2 == map.esc_sym)
            map.esc_sym = static_cast<std::uint8_t>(map.esc_sym ^ idx1 ^ idx2);
    }

    // decode_band (ivi.c)
    bool decodeBand(Band& band) {
        band.buf = prepareBuf(band, dstBuf);
        if (!band.buf)
            return fail("band buffer points to no data");
        band.refBuf = prepareBuf(band, refBuf);
        if (!band.refBuf)
            return fail("no reference buffer");

        if (!decodeBandHdr(band))
            return false;
        if (band.isEmpty)
            return fail("empty band encountered");

        band.rvMap = &rvmapTabs[band.rvmapSel];
        for (int i = 0; i < band.numCorr; ++i)
            applyCorrection(*band.rvMap, band.corr[i * 2], band.corr[i * 2 + 1]);

        long long pos = gb.count();
        bool ok = true;
        for (auto& tile : band.tiles) {
            if (tile.mbSize != band.mbSize) {
                ok = fail("macroblock size mismatch");
                break;
            }
            tile.isEmpty = static_cast<int>(gb.get1());
            if (tile.isEmpty) {
                if (!processEmptyTile(band, tile, (planes[0].bands[0].mbSize >> 3) - (band.mbSize >> 3))) {
                    ok = false;
                    break;
                }
            } else {
                tile.dataSize = decTileDataSize();
                if (!tile.dataSize) {
                    ok = fail("tile data size is zero");
                    break;
                }
                if (!decodeMbInfo(band, tile) || !decodeBlocks(band, tile)) {
                    ok = false;
                    break;
                }
                if (((gb.count() - pos) >> 3) != tile.dataSize) {
                    ok = fail("tile data size mismatch");
                    break;
                }
                pos += static_cast<long long>(tile.dataSize) << 3;
            }
        }

        for (int i = band.numCorr - 1; i >= 0; --i)
            applyCorrection(*band.rvMap, band.corr[i * 2], band.corr[i * 2 + 1]);
        gb.align();
        return ok;
    }

    // switch_buffers (indeo5.c)
    void switchBuffers() {
        switch (prevFrameType) {
        case kIntra:
        case kInter:
            bufSwitch ^= 1;
            dstBuf = bufSwitch;
            refBuf = bufSwitch ^ 1;
            break;
        case kInterScal:
            if (!interScal) {
                ref2Buf = 2;
                interScal = 1;
            }
            std::swap(dstBuf, ref2Buf);
            refBuf = ref2Buf;
            break;
        default: break;
        }
        switch (frameType) {
        case kIntra:
            bufSwitch = 0;
            [[fallthrough]];
        case kInter:
            interScal = 0;
            dstBuf = bufSwitch;
            refBuf = bufSwitch ^ 1;
            break;
        default: break;
        }
    }

    // ff_ivi_decode_frame
    Result decode(const std::uint8_t* data, std::size_t size, YuvFrame& out) {
        gb.init(data, size);
        frameData = data;
        if (!decodePicHdr())
            return Result::Error;
        if (gopInvalid)
            return error("invalid GOP");
        if (gopFlags & kIsProtected)
            return error("password-protected clip");
        if (planes[0].bands.empty())
            return error("color planes not initialized");

        switchBuffers();

        if (frameType != kNull) {
            bufInvalid[dstBuf] = 1;
            for (int p = 0; p < 3; ++p)
                for (auto& band : planes[p].bands)
                    if (!decodeBand(band))
                        return Result::Error;
            bufInvalid[dstBuf] = 0;
        } else {
            if (isScalable)
                return error("null frame in a scalable stream");
            for (int p = 0; p < 3; ++p)
                if (!planes[p].bands[0].buf)
                    return error("null frame before any picture");
        }
        if (bufInvalid[dstBuf])
            return error("invalid buffer");
        if (frameType == kNull)
            return Result::NoFrame;

        out.width = planes[0].width;
        out.height = planes[0].height;
        out.chromaWidth = planes[1].width;
        out.chromaHeight = planes[1].height;
        out.y.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
        out.u.resize(static_cast<std::size_t>(out.chromaWidth) * static_cast<std::size_t>(out.chromaHeight));
        out.v.resize(out.u.size());
        if (isScalable)
            recompose53(planes[0], out.y.data(), out.width);
        else
            outputPlane(planes[0], out.y.data(), out.width);
        // The bitstream's plane 1 is V (Cr) and plane 2 is U (Cb).
        outputPlane(planes[2], out.u.data(), out.chromaWidth);
        outputPlane(planes[1], out.v.data(), out.chromaWidth);
        return Result::Frame;
    }
};

Indeo5Decoder::Indeo5Decoder() : m(std::make_unique<Impl>()) {}
Indeo5Decoder::~Indeo5Decoder() = default;

Indeo5Decoder::Result Indeo5Decoder::decode(std::span<const std::byte> data, YuvFrame& out, std::string* error) {
    m->lastError.clear();
    const Result r = m->decode(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), out);
    if (r == Result::Error && error)
        *error = m->lastError;
    return r;
}

void yuv410ToRgba(const YuvFrame& f, std::vector<std::uint8_t>& rgba) {
    rgba.resize(static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height) * 4);
    if (f.chromaWidth <= 0 || f.chromaHeight <= 0)
        return;
    // Chroma sample (i, j) sits at the centre of its 4x4 luma block; luma
    // pixel x maps to chroma coordinate (x + 0.5) / 4 - 0.5.
    auto sampleChroma = [&](const std::vector<std::uint8_t>& plane, int x, int y) {
        const float cx = std::clamp((static_cast<float>(x) + 0.5f) * 0.25f - 0.5f, 0.0f,
                                    static_cast<float>(f.chromaWidth - 1));
        const float cy = std::clamp((static_cast<float>(y) + 0.5f) * 0.25f - 0.5f, 0.0f,
                                    static_cast<float>(f.chromaHeight - 1));
        const int x0 = static_cast<int>(cx), y0 = static_cast<int>(cy);
        const int x1 = std::min(x0 + 1, f.chromaWidth - 1), y1 = std::min(y0 + 1, f.chromaHeight - 1);
        const float fx = cx - static_cast<float>(x0), fy = cy - static_cast<float>(y0);
        auto at = [&](int xx, int yy) {
            return static_cast<float>(plane[static_cast<std::size_t>(yy) * static_cast<std::size_t>(f.chromaWidth) +
                                            static_cast<std::size_t>(xx)]);
        };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
        const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
        return top + (bottom - top) * fy;
    };
    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            const float Y = 1.164383f * (static_cast<float>(f.y[static_cast<std::size_t>(y) * f.width + x]) - 16.0f);
            const float U = sampleChroma(f.u, x, y) - 128.0f;
            const float V = sampleChroma(f.v, x, y) - 128.0f;
            const auto px = static_cast<std::size_t>(y * f.width + x) * 4;
            rgba[px + 0] = clipU8(static_cast<int>(std::lround(Y + 1.596027f * V)));
            rgba[px + 1] = clipU8(static_cast<int>(std::lround(Y - 0.391762f * U - 0.812968f * V)));
            rgba[px + 2] = clipU8(static_cast<int>(std::lround(Y + 2.017232f * U)));
            rgba[px + 3] = 255;
        }
    }
}

} // namespace mm2::video
