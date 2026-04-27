#include "spudplate/crc32.h"

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

const std::uint8_t* as_bytes(const char* s) {
    return reinterpret_cast<const std::uint8_t*>(s);
}

}  // namespace

TEST(Crc32, EmptyInputIsZero) {
    EXPECT_EQ(spudplate::crc32(nullptr, 0), 0u);
}

TEST(Crc32, IsoHdlcCheckVector) {
    // The canonical CRC-32/ISO-HDLC check value for the ASCII string
    // "123456789" - used by every reference implementation.
    const char* s = "123456789";
    EXPECT_EQ(spudplate::crc32(as_bytes(s), 9), 0xCBF43926u);
}

TEST(Crc32, SeedFoldingMatchesContiguousCall) {
    const char* full = "abcdefgh";
    const std::uint32_t whole = spudplate::crc32(as_bytes(full), 8);

    // Same buffer walked in two halves with the seed parameter folding the
    // running digest forward must produce the identical result.
    const std::uint32_t first = spudplate::crc32(as_bytes(full), 4);
    const std::uint32_t both = spudplate::crc32(as_bytes(full + 4), 4, first);
    EXPECT_EQ(whole, both);
}

TEST(Crc32, SingleZeroByteIsKnownValue) {
    // crc32 of one 0x00 byte is the standard 0xD202EF8D.
    const std::uint8_t z = 0;
    EXPECT_EQ(spudplate::crc32(&z, 1), 0xD202EF8Du);
}
