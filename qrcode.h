// qrcode.h - a real QR encoder: byte mode, versions 1-40, all four ECC levels.
//
// Grown from ra9fael.bosskey's qrcode.h, whose Version 3 / level L path was
// verified end to end (OpenCV decode + a phone camera, see bosskey's
// tools/verify/qr_check.py). That file's primitives carry over unchanged in
// structure: GF(256) arithmetic, the Reed-Solomon generator polynomial, the
// 15-bit format word with its row/column order, the eight masks with the
// penalty rules, the two-column zigzag. On top of them this adds what any
// bigger code needs: the version/ECC block-structure tables, interleaving of
// multi-block codes, alignment patterns, the 18-bit version word, and the
// 16-bit byte-mode character count of versions 10 and up.
//
// Scope: byte mode only. A 2-63 byte text stays the compact Version 1-9 shape
// (8-bit count); bigger ones use the 16-bit count. Reference: ISO/IEC 18004;
// the tables and formulas follow Nayuki's reference implementation.
#ifndef RA9FAEL_QRCODE_H
#define RA9FAEL_QRCODE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace ra9fael::qr {

enum class Ecc { L, M, Q, H };

constexpr int kMinVersion = 1;
constexpr int kMaxVersion = 40;

struct Code {
    bool ok = false;
    int version = 0; // 1-40 when ok
    int size = 0;    // 17 + 4 * version
    Ecc level = Ecc::M;
    // size rows of size characters, '1' dark and '0' light, joined by '\n'.
    std::string rows;
};

namespace detail {

// ---- sizes and the standard block structure ---------------------------------
//
// Total codewords from the raw-module count (ISO/IEC 18004, Nayuki's
// getNumRawDataModules): always a multiple of 8 for versions 1-40.
inline int rawModules(int version)
{
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int numAlign = version / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (version >= 7)
            result -= 36; // the two 18-bit version words
    }
    return result;
}

inline int totalCodewords(int version) { return rawModules(version) / 8; }

// RS check codewords per block, indexed [level][version-1]; level 0=L to 3=H.
inline constexpr int kEccPerBlock[4][kMaxVersion] = {
    {7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28,
     28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30},
    {10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26,
     26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
     28, 28},
    {13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26,
     30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30},
    {17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26,
     28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30},
};

// Blocks per code, indexed the same way.
inline constexpr int kBlocks[4][kMaxVersion] = {
    {1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10,
     12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17,
     18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23,
     23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25,
     34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

inline int levelIndex(Ecc level)
{
    switch (level) {
    case Ecc::L: return 0;
    case Ecc::M: return 1;
    case Ecc::Q: return 2;
    case Ecc::H: return 3;
    }
    return 1;
}

// The format word's two ECC-indicator bits — not levelIndex(): the standard's
// order is L = 01, M = 00, Q = 11, H = 10, and a reader de-interleaves and
// error-corrects by what these say.
inline int formatLevelBits(Ecc level)
{
    switch (level) {
    case Ecc::L: return 1;
    case Ecc::M: return 0;
    case Ecc::Q: return 3;
    case Ecc::H: return 2;
    }
    return 0;
}

inline int dataCodewords(int version, Ecc level)
{
    const int li = levelIndex(level);
    return totalCodewords(version) - kEccPerBlock[li][version - 1] * kBlocks[li][version - 1];
}

// ---- GF(256), primitive polynomial 0x11D ------------------------------------

struct Galois {
    std::array<int, 512> exp{};
    std::array<int, 256> log{};

    Galois()
    {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = x;
            log[x] = i;
            x <<= 1;
            if (x & 0x100)
                x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i)
            exp[i] = exp[i - 255];
    }

    int mul(int a, int b) const { return (a != 0 && b != 0) ? exp[log[a] + log[b]] : 0; }
};

inline const Galois& galois()
{
    static const Galois table;
    return table;
}

// (x - a^0)(x - a^1)...(x - a^(degree-1)), highest coefficient first.
inline std::vector<int> generatorPolynomial(int degree)
{
    const Galois& g = galois();
    std::vector<int> poly{1};
    for (int i = 0; i < degree; ++i) {
        std::vector<int> next(poly.size() + 1, 0);
        for (std::size_t j = 0; j < poly.size(); ++j) {
            next[j] ^= poly[j];
            next[j + 1] ^= g.mul(poly[j], g.exp[i]);
        }
        poly = std::move(next);
    }
    return poly;
}

// The Reed-Solomon check codewords: the remainder of data * x^degree divided
// by the generator polynomial (polynomial long division over GF(256)).
inline std::vector<int> ecCodewords(const std::vector<int>& data, const std::vector<int>& gen)
{
    const Galois& g = galois();
    std::vector<int> message(data.begin(), data.end());
    message.resize(data.size() + gen.size() - 1, 0);
    for (std::size_t i = 0; i < data.size(); ++i) {
        const int factor = message[i];
        if (factor == 0)
            continue;
        for (std::size_t j = 0; j < gen.size(); ++j)
            message[i + j] ^= g.mul(factor, gen[j]);
    }
    return std::vector<int>(message.end() - (gen.size() - 1), message.end());
}

// ---- the bit stream ---------------------------------------------------------

struct Bits {
    std::vector<std::uint8_t> bytes;
    int size = 0; // bits written

    void append(int value, int count)
    {
        for (int i = count - 1; i >= 0; --i) {
            if ((size & 7) == 0)
                bytes.push_back(0);
            if ((value >> i) & 1)
                bytes.back() |= static_cast<std::uint8_t>(0x80 >> (size & 7));
            ++size;
        }
    }
};

// Byte mode: mode indicator 0100, an 8-bit (versions 1-9) or 16-bit
// (versions 10-40) character count, the bytes, the terminator, padding to a
// codeword boundary, then the 0xEC/0x11 pad words.
inline std::vector<int> dataCodewordsOf(const std::string& text, int version, Ecc level, bool& ok)
{
    ok = false;
    const int dataCw = dataCodewords(version, level);
    const int countBits = version < 10 ? 8 : 16;
    if (text.size() > static_cast<std::size_t>(dataCw))
        return {};
    if ((4 + countBits + static_cast<int>(text.size()) * 8) > dataCw * 8)
        return {};

    Bits bits;
    bits.append(0x4, 4);
    bits.append(static_cast<int>(text.size()), countBits);
    for (unsigned char c : text)
        bits.append(static_cast<int>(c), 8);

    const int capacity = dataCw * 8;
    if (bits.size > capacity)
        return {};
    bits.append(0, std::min(4, capacity - bits.size));
    while ((bits.size & 7) != 0)
        bits.append(0, 1);

    int pad = 0xEC;
    while (static_cast<int>(bits.bytes.size()) < dataCw) {
        bits.bytes.push_back(static_cast<std::uint8_t>(pad));
        pad = (pad == 0xEC) ? 0x11 : 0xEC;
    }
    ok = true;
    return std::vector<int>(bits.bytes.begin(), bits.bytes.end());
}

// ---- interleaving ------------------------------------------------------------

// Splits the data codewords into the version's blocks (short ones first),
// computes each block's Reed-Solomon check codewords, and interleaves every
// block's data column-wise, then every block's check column-wise.
inline std::vector<int> interleave(const std::vector<int>& data, int version, Ecc level)
{
    const int li = levelIndex(level);
    const int numBlocks = kBlocks[li][version - 1];
    const int blockEccLen = kEccPerBlock[li][version - 1];
    const int rawCw = totalCodewords(version);
    const int numShortBlocks = numBlocks - rawCw % numBlocks;
    const int shortBlockLen = rawCw / numBlocks; // a long block's full length

    const auto gen = generatorPolynomial(blockEccLen);
    std::vector<std::vector<int>> blocks;
    blocks.reserve(numBlocks);
    int at = 0;
    for (int i = 0; i < numBlocks; ++i) {
        const int len = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        std::vector<int> block(data.begin() + at, data.begin() + at + len);
        at += len;
        auto ecc = ecCodewords(block, gen);
        if (i < numShortBlocks)
            block.push_back(0); // placeholder: skipped by the interleave below
        block.insert(block.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(block));
    }

    std::vector<int> out;
    out.reserve(rawCw);
    for (int i = 0; i < shortBlockLen + 1; ++i)
        for (int b = 0; b < numBlocks; ++b) {
            if (i == shortBlockLen - blockEccLen && b < numShortBlocks)
                continue; // the short blocks' placeholder column
            out.push_back(blocks[b][i]);
        }
    return out;
}

// ---- the module grid --------------------------------------------------------

struct Grid {
    int size = 0;
    // 0 light, 1 dark. Everything starts light: the modules the data stream
    // leaves over (the remainder bits) must be light before the mask flips
    // them, as the specification says.
    std::vector<int8_t> value;
    std::vector<uint8_t> fixed; // function modules: masks never touch them

    explicit Grid(int size_)
        : size(size_), value(static_cast<std::size_t>(size_) * size_, 0),
          fixed(static_cast<std::size_t>(size_) * size_, 0)
    {
    }

    int8_t at(int row, int col) const { return value[static_cast<std::size_t>(row) * size + col]; }
    bool isFixed(int row, int col) const
    {
        return fixed[static_cast<std::size_t>(row) * size + col] != 0;
    }
    void set(int row, int col, int bit)
    {
        value[static_cast<std::size_t>(row) * size + col] = static_cast<int8_t>(bit);
        fixed[static_cast<std::size_t>(row) * size + col] = 1;
    }
};

inline void drawFinder(Grid& g, int row, int col)
{
    for (int dr = -1; dr <= 7; ++dr) {
        for (int dc = -1; dc <= 7; ++dc) {
            const int r = row + dr;
            const int c = col + dc;
            if (r < 0 || r >= g.size || c < 0 || c >= g.size)
                continue;
            const bool inside = dr >= 0 && dr <= 6 && dc >= 0 && dc <= 6;
            const bool dark = inside && (dr == 0 || dr == 6 || dc == 0 || dc == 6
                                         || (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4));
            g.set(r, c, dark ? 1 : 0);
        }
    }
}

inline void drawAlignment(Grid& g, int row, int col)
{
    for (int dr = -2; dr <= 2; ++dr)
        for (int dc = -2; dc <= 2; ++dc) {
            const bool dark = std::max(std::abs(dr), std::abs(dc)) != 1;
            g.set(row + dr, col + dc, dark ? 1 : 0);
        }
}

// Alignment pattern centers for versions 2-40: each version's centers except
// the fixed 6, which the encoder prepends. Each version's last entry is its
// size - 7. The rows cover 1, 1, 1, 1, 1, 2,
// 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5,
// 5, 5, 6, 6, 6, 6, 6, 6 centers.
inline constexpr int kAlignFlat[] = {
    18, 22, 26, 30, 34,
    22, 38, 24, 42, 26, 46, 28, 50, 30, 54, 32, 58, 34, 62,
    26, 46, 66, 26, 48, 70, 26, 50, 74, 30, 54, 78, 30, 56, 82, 30, 58, 86, 34, 62, 90,
    28, 50, 72, 94, 26, 50, 74, 98, 30, 54, 78, 102, 28, 54, 80, 106, 32, 58, 84, 110,
    30, 58, 86, 114, 34, 62, 90, 118,
    26, 50, 74, 98, 122, 30, 54, 78, 102, 126, 26, 52, 78, 104, 130, 30, 56, 82, 108,
    134, 34, 60, 86, 112, 138, 30, 58, 86, 114, 142, 34, 62, 90, 118, 146,
    30, 54, 78, 102, 126, 150, 24, 50, 76, 102, 128, 154, 28, 54, 80, 106, 132, 158,
    32, 58, 84, 110, 136, 162, 26, 54, 82, 110, 138, 166, 30, 58, 86, 114, 142, 170,
};

// The count of interior centers for a version (0 for 1).
inline int alignCount(int version)
{
    if (version < 2)
        return 0;
    if (version < 7)
        return 1;
    if (version < 14)
        return 2;
    if (version < 21)
        return 3;
    if (version < 28)
        return 4;
    if (version < 35)
        return 5;
    return 6;
}

// Where the interior centers start in kAlignFlat for this version.
inline int alignStart(int version)
{
    int start = 0;
    for (int v = 2; v < version; ++v)
        start += alignCount(v);
    return start;
}

// All 15-bit format words: 2 ECC-level bits (L = 01, M = 00, Q = 11, H = 10),
// 3 mask bits, BCH(15,5), XOR'd with the specified mask 0x5412. Both copies,
// plus the dark module. Note the row/column order — the reference writes
// these as (x, y); swapping them silently transposes the whole format block
// and no reader can recover the mask.
inline void drawFormat(Grid& g, int levelBits, int mask)
{
    const int data = (levelBits << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i)
        rem = (rem << 1) ^ (((rem >> 9) & 1) ? 0x537 : 0);
    const int bits = ((data << 10) | rem) ^ 0x5412;
    auto bit = [bits](int i) { return (bits >> i) & 1; };

    // Copy 1: the vertical strip up beside the top-left finder, then the
    // horizontal one.
    for (int i = 0; i <= 5; ++i)
        g.set(i, 8, bit(i));
    g.set(7, 8, bit(6));
    g.set(8, 8, bit(7));
    g.set(8, 7, bit(8));
    for (int i = 9; i <= 14; ++i)
        g.set(8, 14 - i, bit(i));

    // Copy 2: split between the other two finders — eight bits along row 8
    // from the right edge, then seven down column 8.
    for (int i = 0; i <= 7; ++i)
        g.set(8, g.size - 1 - i, bit(i));
    for (int i = 8; i <= 14; ++i)
        g.set(g.size - 15 + i, 8, bit(i));

    g.set(g.size - 8, 8, 1); // the dark module, at (4 * version + 9, 8)
}

// The 18-bit version word of versions 7 and up, at the top right and the
// bottom left.
inline void drawVersion(Grid& g, int version)
{
    if (version < 7)
        return;
    int rem = version;
    for (int i = 0; i < 12; ++i)
        rem = (rem << 1) ^ (((rem >> 11) & 1) ? 0x1F25 : 0);
    const int bits = (version << 12) | rem;
    for (int i = 0; i < 18; ++i) {
        const int bit = (bits >> i) & 1;
        g.set(g.size - 11 + i % 3, i / 3, bit);
        g.set(i / 3, g.size - 11 + i % 3, bit);
    }
}

// Two-column zigzag from the bottom right, skipping the timing column.
inline void drawData(Grid& g, const std::vector<int>& codewords)
{
    const int total = static_cast<int>(codewords.size()) * 8;
    auto bitAt = [&](int i) { return (codewords[i >> 3] >> (7 - (i & 7))) & 1; };

    int i = 0;
    for (int right = g.size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < g.size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                const int col = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int row = upward ? g.size - 1 - vert : vert;
                if (!g.isFixed(row, col) && i < total) {
                    g.value[static_cast<std::size_t>(row) * g.size + col] =
                        static_cast<int8_t>(bitAt(i));
                    ++i;
                }
            }
        }
    }
}

inline bool maskBit(int mask, int row, int col)
{
    switch (mask) {
    case 0: return (row + col) % 2 == 0;
    case 1: return row % 2 == 0;
    case 2: return col % 3 == 0;
    case 3: return (row + col) % 3 == 0;
    case 4: return (row / 2 + col / 3) % 2 == 0;
    case 5: return (row * col) % 2 + (row * col) % 3 == 0;
    case 6: return ((row * col) % 2 + (row * col) % 3) % 2 == 0;
    case 7: return ((row + col) % 2 + (row * col) % 3) % 2 == 0;
    default: return false;
    }
}

// The four penalty rules (N1 runs, N2 blocks, N3 finder lookalikes, N4 the
// dark ratio), used only to pick the least noisy mask.
inline int penalty(const Grid& g)
{
    int score = 0;

    for (int dir = 0; dir < 2; ++dir) {
        for (int line = 0; line < g.size; ++line) {
            int run = 1;
            int prev = -1;
            for (int k = 0; k < g.size; ++k) {
                const int v = dir == 0 ? g.at(line, k) : g.at(k, line);
                if (v == prev) {
                    ++run;
                    if (run == 5)
                        score += 3;
                    else if (run > 5)
                        score += 1;
                } else {
                    prev = v;
                    run = 1;
                }
            }
        }
    }

    for (int r = 0; r + 1 < g.size; ++r)
        for (int c = 0; c + 1 < g.size; ++c) {
            const int v = g.at(r, c);
            if (v == g.at(r, c + 1) && v == g.at(r + 1, c) && v == g.at(r + 1, c + 1))
                score += 3;
        }

    static constexpr int kPatterns[2][11] = {
        {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0},
        {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1},
    };
    for (int dir = 0; dir < 2; ++dir)
        for (int line = 0; line < g.size; ++line)
            for (int start = 0; start + 11 <= g.size; ++start) {
                bool first = true;
                bool second = true;
                for (int k = 0; k < 11; ++k) {
                    const int v = dir == 0 ? g.at(line, start + k) : g.at(start + k, line);
                    first = first && v == kPatterns[0][k];
                    second = second && v == kPatterns[1][k];
                }
                if (first)
                    score += 40;
                if (second)
                    score += 40;
            }

    int dark = 0;
    for (std::size_t i = 0; i < g.value.size(); ++i)
        dark += g.value[i] == 1;
    const int total = g.size * g.size;
    score += std::abs(dark * 100 - total * 50) / (total * 5) * 10;

    return score;
}

} // namespace detail

// The most bytes byte mode holds at this level (version 40):
// 2953 / 2331 / 1663 / 1273.
inline int capacityBytes(Ecc level)
{
    const int dataCw = detail::dataCodewords(kMaxVersion, level);
    const int ccBits = kMaxVersion < 10 ? 8 : 16;
    return (dataCw * 8 - 4 - ccBits) / 8;
}

// The smallest version carrying n bytes at this level; 0 when none does.
inline int versionForBytes(std::size_t n, Ecc level)
{
    for (int v = kMinVersion; v <= kMaxVersion; ++v) {
        const int ccBits = v < 10 ? 8 : 16;
        if (4 + ccBits + static_cast<int>(n) * 8 <= detail::dataCodewords(v, level) * 8)
            return v;
    }
    return 0;
}

// Encodes text (up to capacityBytes(level)) as the smallest version that
// fits, at the given level. ok is false when the text does not fit.
inline Code encode(const std::string& text, Ecc level)
{
    using namespace detail;

    const int version = versionForBytes(text.size(), level);
    if (version <= 0)
        return {};

    bool haveData = false;
    auto data = dataCodewordsOf(text, version, level, haveData);
    if (!haveData)
        return {};
    const auto codewords = interleave(data, version, level);

    const int size = version * 4 + 17;
    Grid base(size);
    drawFinder(base, 0, 0);
    drawFinder(base, 0, size - 7);
    drawFinder(base, size - 7, 0);
    for (int i = 8; i < size - 8; ++i) {
        base.set(6, i, i % 2 == 0 ? 1 : 0);
        base.set(i, 6, i % 2 == 0 ? 1 : 0);
    }
    // Alignment patterns at every position pair, skipping the three corners
    // that sit on finder patterns. pos is the fixed 6 followed by this
    // version's kAlignFlat centers, whose last entry is size - 7, so the
    // bottom-right pattern is drawn.
    const int count = alignCount(version);
    const int start = alignStart(version);
    std::vector<int> pos;
    pos.reserve(static_cast<std::size_t>(count) + 1);
    pos.push_back(6);
    for (int i = 0; i < count; ++i)
        pos.push_back(kAlignFlat[start + i]);
    const int n = static_cast<int>(pos.size());
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
                continue; // the three top finders
            drawAlignment(base, pos[j], pos[i]); // row, col
        }
    drawFormat(base, formatLevelBits(level), 0); // reserves the format area
    drawVersion(base, version);
    drawData(base, codewords);

    Grid best(size);
    int bestScore = -1;
    for (int mask = 0; mask < 8; ++mask) {
        Grid g(size);
        g.value = base.value;
        g.fixed = base.fixed;
        for (int r = 0; r < size; ++r)
            for (int c = 0; c < size; ++c)
                if (!g.isFixed(r, c) && maskBit(mask, r, c))
                    g.value[static_cast<std::size_t>(r) * size + c] ^= 1;
        drawFormat(g, formatLevelBits(level), mask);

        const int score = penalty(g);
        if (bestScore < 0 || score < bestScore) {
            bestScore = score;
            best = std::move(g);
        }
    }

    Code out;
    out.ok = true;
    out.version = version;
    out.size = size;
    out.level = level;
    out.rows.reserve(static_cast<std::size_t>(size) * (size + 1));
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c)
            out.rows.push_back(best.at(r, c) == 1 ? '1' : '0');
        if (r + 1 < size)
            out.rows.push_back('\n');
    }
    return out;
}

} // namespace ra9fael::qr

#endif // RA9FAEL_QRCODE_H
