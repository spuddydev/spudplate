// Sarwate table-driven CRC-32/ISO-HDLC. Public-domain reference: see e.g.
// the CRC-32 entry on Wikipedia (Sarwate 1988). The polynomial is the
// standard reflected form 0xEDB88320; the seed/xorout convention matches
// zlib so the well-known test vectors hold.

#include "spudplate/crc32.h"

#include <array>

namespace spudplate {

namespace {

constexpr std::array<std::uint32_t, 256> make_table() noexcept {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}

constexpr auto kTable = make_table();

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                    std::uint32_t seed) noexcept {
    std::uint32_t c = seed ^ 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        c = kTable[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

}  // namespace spudplate
