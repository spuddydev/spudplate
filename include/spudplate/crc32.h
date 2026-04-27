#ifndef SPUDPLATE_CRC32_H
#define SPUDPLATE_CRC32_H

#include <cstddef>
#include <cstdint>

namespace spudplate {

/**
 * @brief CRC-32/ISO-HDLC checksum.
 *
 * Polynomial 0xEDB88320 (reflected), init 0xFFFFFFFF, xorout 0xFFFFFFFF.
 * Calling with the default seed produces the standard digest used by zlib,
 * gzip, png, and friends. Test vectors:
 *
 * - `crc32(nullptr, 0)` returns 0
 * - `crc32("123456789", 9)` returns 0xCBF43926
 *
 * The seed parameter lets callers fold a checksum across non-contiguous
 * buffers without re-walking previously-checked bytes; pass the previous
 * return value as the next seed.
 */
std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                    std::uint32_t seed = 0) noexcept;

}  // namespace spudplate

#endif  // SPUDPLATE_CRC32_H
