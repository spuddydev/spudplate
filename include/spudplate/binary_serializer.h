#ifndef SPUDPLATE_BINARY_SERIALIZER_H
#define SPUDPLATE_BINARY_SERIALIZER_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "spudplate/ast.h"

namespace spudplate {

/**
 * @brief Tag values used on the wire to identify each AST variant arm.
 *
 * Each enum value equals the corresponding `std::variant::index()` for the
 * variant declared in `ast.h`. The variant declaration order is therefore
 * load-bearing for wire compatibility — adding a new arm is fine, but
 * reordering existing arms silently breaks every previously-produced
 * `.spp` file. Round-trip tests in `tests/binary_serializer_test.cpp`
 * exercise every arm by name to guard against accidental reordering.
 */
enum class ExprTag : std::uint8_t {
    StringLiteral = 0,
    IntegerLiteral = 1,
    BoolLiteral = 2,
    Identifier = 3,
    Unary = 4,
    Binary = 5,
    FunctionCall = 6,
    TemplateString = 7,
};

/** @brief Tag values for `StmtData` arms. See `ExprTag`. */
enum class StmtTag : std::uint8_t {
    Ask = 0,
    Let = 1,
    Assign = 2,
    Mkdir = 3,
    File = 4,
    Repeat = 5,
    Copy = 6,
    Include = 7,
    Run = 8,
};

/** @brief Tag values for `PathSegment` arms. See `ExprTag`. */
enum class PathSegTag : std::uint8_t {
    Literal = 0,
    Var = 1,
    Interp = 2,
};

/** @brief Tag values for `FileSource` arms. See `ExprTag`. */
enum class FileSrcTag : std::uint8_t {
    From = 0,
    Content = 1,
};

/** @brief Sub-tag for `TemplateStringExpr.parts` entries. */
enum class TemplatePartTag : std::uint8_t {
    Literal = 0,
    Expression = 1,
};

/** @brief Raised when an AST cannot be encoded (e.g. invalid operator token). */
class BinarySerializeError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Raised when the byte stream cannot be decoded.
 *
 * Carries the byte offset at which decoding gave up so a hex-dumped
 * payload can be inspected at the right location.
 */
class BinaryDeserializeError : public std::runtime_error {
  public:
    BinaryDeserializeError(std::string message, std::size_t offset);

    /** @brief Byte offset within the decoded buffer where the failure occurred. */
    std::size_t offset() const noexcept { return offset_; }

  private:
    std::size_t offset_;
};

/**
 * @brief Encode a parsed `Program` to a tightly packed byte stream.
 *
 * The output is opaque to the caller. The encoding mirrors the AST
 * declared in `ast.h` field-for-field: each variant arm gets a one-byte
 * tag (`variant::index()`), strings and byte vectors are length-prefixed
 * with LEB128 varints, optionals are a one-byte present-flag plus the
 * payload, signed integer fields are zigzag-encoded inside the varint.
 *
 * Throws `BinarySerializeError` only for inputs that violate the AST's
 * own contract — e.g. a `UnaryExpr.op` outside the language's operator
 * subset.
 */
std::vector<std::uint8_t> serialize_program(const Program& program);

/**
 * @brief Decode a byte stream back into a `Program`.
 *
 * Throws `BinaryDeserializeError` with a byte offset on:
 *   - truncated input mid-tag, mid-varint, or mid-payload
 *   - varints longer than 10 bytes
 *   - tag values outside the legal range for their position
 */
Program deserialize_program(const std::uint8_t* data, std::size_t size);

}  // namespace spudplate

#endif  // SPUDPLATE_BINARY_SERIALIZER_H
