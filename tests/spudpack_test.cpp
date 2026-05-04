#include "spudplate/spudpack.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "spudplate/crc32.h"
#include "test_helpers.h"

using spudplate::Spudpack;
using spudplate::SpudpackAsset;
using spudplate::SpudpackDep;
using spudplate::SpudpackError;
using spudplate::is_normalized_asset_path;
using spudplate::normalize_asset_path;
using spudplate::spudpack_decode;
using spudplate::spudpack_encode;
using spudplate::spudpack_read_file;
using spudplate::spudpack_write_file;
using spudplate::test::TmpDir;

namespace {

Spudpack make_simple() {
    Spudpack p;
    p.source = "ask name \"Name?\" string\n";
    p.program_bytes = {1, 2, 3, 4, 5};
    p.assets.push_back({"templates/main.cpp", 0644,
                        {'#', 'i', 'n', 'c', 'l', 'u', 'd', 'e'}});
    p.assets.push_back({"assets/logo.png", 0444, {0x89, 'P', 'N', 'G'}});
    return p;
}

void rewrite_crc(std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc =
        spudplate::crc32(bytes.data(), bytes.size() - 4);
    bytes[bytes.size() - 4] = static_cast<std::uint8_t>(crc & 0xFF);
    bytes[bytes.size() - 3] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    bytes[bytes.size() - 2] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
    bytes[bytes.size() - 1] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
}

}  // namespace

// --- normalize_asset_path / is_normalized_asset_path -----------------------

TEST(SpudpackPath, NormalizeStripsLeadingDotSlash) {
    EXPECT_EQ(normalize_asset_path("./a/b"), "a/b");
}

TEST(SpudpackPath, NormalizeCollapsesDoubleSlash) {
    EXPECT_EQ(normalize_asset_path("a//b"), "a/b");
}

TEST(SpudpackPath, NormalizeRejectsAbsolute) {
    EXPECT_THROW(normalize_asset_path("/a"), SpudpackError);
}

TEST(SpudpackPath, NormalizeRejectsParentSegment) {
    EXPECT_THROW(normalize_asset_path("a/../b"), SpudpackError);
}

TEST(SpudpackPath, NormalizeRejectsEmbeddedNul) {
    std::string raw("a\0b", 3);
    EXPECT_THROW(normalize_asset_path(raw), SpudpackError);
}

TEST(SpudpackPath, NormalizePreservesTrailingSlash) {
    EXPECT_EQ(normalize_asset_path("foo/bar/"), "foo/bar/");
}

TEST(SpudpackPath, NormalizeDropsCurrentDirSegments) {
    EXPECT_EQ(normalize_asset_path("a/./b"), "a/b");
}

TEST(SpudpackPath, IsNormalizedRejectsDotSegments) {
    EXPECT_FALSE(is_normalized_asset_path("a/./b"));
    EXPECT_FALSE(is_normalized_asset_path("a/../b"));
    EXPECT_FALSE(is_normalized_asset_path("/a"));
    EXPECT_FALSE(is_normalized_asset_path("a//b"));
    EXPECT_TRUE(is_normalized_asset_path("a/b"));
    EXPECT_TRUE(is_normalized_asset_path("a/b/"));
}

// --- round-trip ------------------------------------------------------------

TEST(SpudpackCodec, RoundTripEmpty) {
    Spudpack in;
    auto bytes = spudpack_encode(in);
    Spudpack out = spudpack_decode(bytes.data(), bytes.size());
    EXPECT_EQ(out.source, "");
    EXPECT_TRUE(out.program_bytes.empty());
    EXPECT_TRUE(out.assets.empty());
}

TEST(SpudpackCodec, RoundTripSimple) {
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    Spudpack out = spudpack_decode(bytes.data(), bytes.size());
    EXPECT_EQ(out.source, in.source);
    EXPECT_EQ(out.program_bytes, in.program_bytes);
    ASSERT_EQ(out.assets.size(), 2u);
    EXPECT_EQ(out.assets[0].path, in.assets[0].path);
    EXPECT_EQ(out.assets[0].mode, in.assets[0].mode);
    EXPECT_EQ(out.assets[0].data, in.assets[0].data);
    EXPECT_EQ(out.assets[1].path, in.assets[1].path);
    EXPECT_EQ(out.assets[1].mode, in.assets[1].mode);
    EXPECT_EQ(out.assets[1].data, in.assets[1].data);
}

TEST(SpudpackCodec, RoundTripEmptyLeafDir) {
    Spudpack in;
    in.assets.push_back({"empty/", 0755, {}});
    auto bytes = spudpack_encode(in);
    Spudpack out = spudpack_decode(bytes.data(), bytes.size());
    ASSERT_EQ(out.assets.size(), 1u);
    EXPECT_EQ(out.assets[0].path, "empty/");
    EXPECT_EQ(out.assets[0].mode, 0755);
    EXPECT_TRUE(out.assets[0].data.empty());
}

TEST(SpudpackCodec, RoundTripSetuidModeWithinSevenSevenSeven) {
    Spudpack in;
    in.assets.push_back({"bin/run", 04755, {0xFE, 0xED}});
    auto bytes = spudpack_encode(in);
    Spudpack out = spudpack_decode(bytes.data(), bytes.size());
    ASSERT_EQ(out.assets.size(), 1u);
    EXPECT_EQ(out.assets[0].mode, 04755);
}

// --- decoder error paths ---------------------------------------------------

TEST(SpudpackCodec, BadMagic) {
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    bytes[0] = 'X';
    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("not a spudpack"), std::string::npos);
        ASSERT_TRUE(e.offset().has_value());
        EXPECT_EQ(*e.offset(), 0u);
    }
}

TEST(SpudpackCodec, UnsupportedVersionZero) {
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    bytes[4] = 0;
    rewrite_crc(bytes);
    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("unsupported spudpack version"),
                  std::string::npos);
        ASSERT_TRUE(e.offset().has_value());
        EXPECT_EQ(*e.offset(), 4u);
    }
}

TEST(SpudpackCodec, UnsupportedVersionFour) {
    // v1, v2, and v3 are accepted; v4 (and above) is not.
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    bytes[4] = 4;
    rewrite_crc(bytes);
    EXPECT_THROW(spudpack_decode(bytes.data(), bytes.size()), SpudpackError);
}

TEST(SpudpackCodec, V1FixtureDecodes) {
    // Encoder writes the highest supported version; flip the byte to 1 and
    // rewrite the CRC. Decoding succeeds and the resulting Spudpack carries
    // version=1 so downstream code (binary serialiser) can decode RunStmt
    // without the trailing timeout field.
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    bytes[4] = 1;
    rewrite_crc(bytes);
    auto out = spudpack_decode(bytes.data(), bytes.size());
    EXPECT_EQ(out.version, 1u);
    EXPECT_EQ(out.source, in.source);
}

TEST(SpudpackCodec, V2FixtureDecodes) {
    // v2 packs in the wild carry no deps - flip the byte to 2 and decode.
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    bytes[4] = 2;
    rewrite_crc(bytes);
    auto out = spudpack_decode(bytes.data(), bytes.size());
    EXPECT_EQ(out.version, 2u);
    EXPECT_TRUE(out.deps.empty());
}

TEST(SpudpackCodec, CrcMismatchDetected) {
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    // Flip a byte inside the source field, leaving the trailer alone.
    bytes[10] ^= 0xFF;
    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("CRC mismatch"), std::string::npos);
        ASSERT_TRUE(e.offset().has_value());
        EXPECT_EQ(*e.offset(), bytes.size() - 4);
    }
}

TEST(SpudpackCodec, DepCountNonZeroInV1Rejected) {
    // Hand-craft a minimal valid header with version=1 and dep_count=1.
    // v1 packs in the wild never carry deps, so the decoder must refuse.
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'S', 'P', 'U', 'D'});
    bytes.push_back(1);  // version
    bytes.push_back(0);  // flags
    bytes.push_back(0);  // source_len = 0
    bytes.push_back(0);  // program_len = 0
    bytes.push_back(0);  // asset_count = 0
    bytes.push_back(1);  // dep_count = 1 (illegal in v1)
    bytes.insert(bytes.end(), 4, 0);
    rewrite_crc(bytes);
    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("does not support deps"),
                  std::string::npos);
    }
}

TEST(SpudpackCodec, RoundTripWithDeps) {
    // Build a self-contained dep blob first, then bundle two of them as
    // deps of an outer pack. The dep bytes are stored opaquely so the
    // outer encode/decode does not care about their internals.
    Spudpack inner_a;
    inner_a.source = "ask q \"q?\" string\n";
    inner_a.program_bytes = {9, 9, 9};
    auto inner_a_bytes = spudpack_encode(inner_a);

    Spudpack inner_b;
    inner_b.source = "let x = 1\n";
    inner_b.program_bytes = {1, 1};
    auto inner_b_bytes = spudpack_encode(inner_b);

    Spudpack in = make_simple();
    in.deps.push_back({"inner_a", inner_a_bytes});
    in.deps.push_back({"inner_b", inner_b_bytes});
    auto bytes = spudpack_encode(in);
    Spudpack out = spudpack_decode(bytes.data(), bytes.size());
    EXPECT_EQ(out.version, 3u);
    ASSERT_EQ(out.deps.size(), 2u);
    EXPECT_EQ(out.deps[0].name, "inner_a");
    EXPECT_EQ(out.deps[0].bytes, inner_a_bytes);
    EXPECT_EQ(out.deps[1].name, "inner_b");
    EXPECT_EQ(out.deps[1].bytes, inner_b_bytes);

    // The dep bytes must round-trip back through the codec untouched.
    Spudpack a_decoded = spudpack_decode(out.deps[0].bytes.data(),
                                         out.deps[0].bytes.size());
    EXPECT_EQ(a_decoded.source, inner_a.source);
}

TEST(SpudpackCodec, EncodeRejectsDepNameWithSlash) {
    Spudpack in;
    in.deps.push_back({"a/b", {1, 2, 3}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, EncodeRejectsEmptyDepName) {
    Spudpack in;
    in.deps.push_back({"", {1, 2, 3}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, EncodeRejectsDepNameDotDot) {
    Spudpack in;
    in.deps.push_back({"..", {1, 2, 3}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, DecodeRejectsDepNameWithSlash) {
    // Build a v3 pack and patch the dep name in place to "a/b".
    Spudpack in;
    in.deps.push_back({"abc", {1, 2, 3}});
    auto bytes = spudpack_encode(in);
    // After magic(4)+version(1)+flags(1)+source_len(1)+program_len(1)+
    // asset_count(1)+dep_count(1)+name_len(1), the next 3 bytes are "abc".
    std::size_t name_off = 4 + 1 + 1 + 1 + 1 + 1 + 1 + 1;
    bytes[name_off] = 'a';
    bytes[name_off + 1] = '/';
    bytes[name_off + 2] = 'b';
    rewrite_crc(bytes);
    EXPECT_THROW(spudpack_decode(bytes.data(), bytes.size()), SpudpackError);
}

TEST(SpudpackCodec, DecodeRejectsHugeDepCount) {
    // Hand-craft a v3 pack with dep_count = (1 << 16), well above the cap.
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'S', 'P', 'U', 'D'});
    bytes.push_back(3);  // version
    bytes.push_back(0);  // flags
    bytes.push_back(0);  // source_len = 0
    bytes.push_back(0);  // program_len = 0
    bytes.push_back(0);  // asset_count = 0
    // varint 1<<16 -> 0x80 0x80 0x04
    bytes.push_back(0x80);
    bytes.push_back(0x80);
    bytes.push_back(0x04);
    bytes.insert(bytes.end(), 4, 0);
    rewrite_crc(bytes);
    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("dep_count exceeds maximum"),
                  std::string::npos);
    }
}

TEST(SpudpackCodec, EncodeRejectsNonNormalizedPath) {
    Spudpack in;
    in.assets.push_back({"a/../b", 0644, {1}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, EncodeRejectsEmbeddedNulInPath) {
    Spudpack in;
    in.assets.push_back({std::string("a\0b", 3), 0644, {1}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, EncodeRejectsLeadingSlash) {
    Spudpack in;
    in.assets.push_back({"/etc/passwd", 0644, {1}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, EncodeRejectsTrailingSlashWithData) {
    Spudpack in;
    in.assets.push_back({"foo/", 0755, {1, 2}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, EncodeRejectsModeAboveSevenSevenSeven) {
    Spudpack in;
    in.assets.push_back({"foo", static_cast<std::uint16_t>(0xF000), {1}});
    EXPECT_THROW(spudpack_encode(in), SpudpackError);
}

TEST(SpudpackCodec, DecodeRejectsHugeSourceLength) {
    // Hand-craft: claim source_len = SIZE_MAX-1 via 10-byte varint. The
    // length validation must throw before any allocation.
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'S', 'P', 'U', 'D'});
    bytes.push_back(1);
    bytes.push_back(0);
    // 10-byte varint with the largest representable u64 minus 1.
    for (int i = 0; i < 9; ++i) bytes.push_back(0xFF);
    bytes.push_back(0x7F);
    bytes.insert(bytes.end(), 4, 0);
    EXPECT_THROW(spudpack_decode(bytes.data(), bytes.size()), SpudpackError);
}

TEST(SpudpackCodec, DecodeRejectsHugeAssetCount) {
    // Hand-craft: source/program/asset_count where asset_count = (1 << 24).
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'S', 'P', 'U', 'D'});
    bytes.push_back(1);
    bytes.push_back(0);
    bytes.push_back(0);  // source_len = 0
    bytes.push_back(0);  // program_len = 0
    // varint 1<<24 = 16777216 -> 0x80 0x80 0x80 0x08
    bytes.push_back(0x80);
    bytes.push_back(0x80);
    bytes.push_back(0x80);
    bytes.push_back(0x08);
    bytes.insert(bytes.end(), 4, 0);
    rewrite_crc(bytes);
    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("asset_count exceeds maximum"),
                  std::string::npos);
    }
}

TEST(SpudpackCodec, DecodeRejectsModeWithReservedBits) {
    // Build a spudpack with one asset, then overwrite the encoded mode
    // field with 0xFFFF (which has bits above 0o7777). Re-CRC so the only
    // failure is the mode check.
    Spudpack in;
    in.assets.push_back({"foo", 0644, {1, 2, 3}});
    auto bytes = spudpack_encode(in);

    // Locate the mode field. After magic(4)+version(1)+flags(1)+source_len(1
    // varint byte = 0)+program_len(1 = 0)+asset_count(1 = 1)+path_len(1)+
    // path("foo" = 3 bytes), the next 2 bytes are the mode.
    std::size_t mode_off = 4 + 1 + 1 + 1 + 1 + 1 + 1 + 3;
    bytes[mode_off] = 0xFF;
    bytes[mode_off + 1] = 0xFF;
    rewrite_crc(bytes);

    try {
        spudpack_decode(bytes.data(), bytes.size());
        FAIL() << "expected throw";
    } catch (const SpudpackError& e) {
        EXPECT_NE(std::string(e.what()).find("reserved bits"), std::string::npos);
    }
}

TEST(SpudpackCodec, TruncatedFileBeforeTrailer) {
    Spudpack in = make_simple();
    auto bytes = spudpack_encode(in);
    bytes.resize(bytes.size() / 2);
    EXPECT_THROW(spudpack_decode(bytes.data(), bytes.size()), SpudpackError);
}

TEST(SpudpackCodec, TruncatedAtMagic) {
    std::vector<std::uint8_t> bytes(3, 0);
    EXPECT_THROW(spudpack_decode(bytes.data(), bytes.size()), SpudpackError);
}

// --- file IO ---------------------------------------------------------------

TEST(SpudpackFile, RoundTripWriteRead) {
    TmpDir tmp;
    Spudpack in = make_simple();
    auto file = tmp.path() / "demo.spp";
    spudpack_write_file(file, in);
    Spudpack out = spudpack_read_file(file);
    EXPECT_EQ(out.source, in.source);
    EXPECT_EQ(out.program_bytes, in.program_bytes);
    ASSERT_EQ(out.assets.size(), in.assets.size());
}

TEST(SpudpackFile, ReadMissingFileThrows) {
    TmpDir tmp;
    EXPECT_THROW(spudpack_read_file(tmp.path() / "does_not_exist.spp"),
                 SpudpackError);
}
