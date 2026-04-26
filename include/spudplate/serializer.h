#ifndef SPUDPLATE_SERIALIZER_H
#define SPUDPLATE_SERIALIZER_H

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

#include "spudplate/ast.h"

namespace spudplate {

/**
 * @brief Schema version embedded in every serialised program.
 *
 * Bumped on incompatible format changes. The decoder rejects any value
 * other than this constant.
 */
constexpr int SPUDPLATE_FORMAT_VERSION = 1;

/**
 * @brief Thrown by `program_from_json` on schema mismatch, missing fields,
 * unknown discriminators, or wrapped `nlohmann::json` exceptions.
 *
 * The `json_pointer` field carries an RFC 6901 pointer into the offending
 * location whenever it is available, so error messages can guide users
 * straight to the bad node. `line` and `column` mirror the AST's position
 * fields when the offending JSON object carries them.
 */
class DeserializeError : public std::runtime_error {
  public:
    DeserializeError(std::string message, std::string json_pointer = "",
                     std::optional<int> line = std::nullopt,
                     std::optional<int> column = std::nullopt);

    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::string& json_pointer() const noexcept { return json_pointer_; }
    [[nodiscard]] std::optional<int> line() const noexcept { return line_; }
    [[nodiscard]] std::optional<int> column() const noexcept { return column_; }

  private:
    std::string message_;
    std::string json_pointer_;
    std::optional<int> line_;
    std::optional<int> column_;
};

/**
 * @brief Encode a `Program` as a JSON document.
 *
 * The result has a top-level object with `format_version` and a
 * `statements` array. Every variant node carries a `type` field with a
 * stable string discriminator; positions (line/column) are encoded on
 * every node and round-trip exactly.
 */
nlohmann::json program_to_json(const Program& program);

/**
 * @brief Decode a JSON document produced by `program_to_json`.
 *
 * Throws `DeserializeError` if the schema is malformed, the
 * `format_version` is missing or not equal to `SPUDPLATE_FORMAT_VERSION`,
 * or an unknown `type` discriminator is encountered.
 */
Program program_from_json(const nlohmann::json& root);

/**
 * @brief Position-aware structural equality for two `Program` values.
 *
 * Compares every field including line/column so a successful round trip
 * through JSON can assert exact equality. Built on top of the existing
 * `exprs_equal` helper.
 */
[[nodiscard]] bool programs_equal(const Program& a, const Program& b);

}  // namespace spudplate

#endif  // SPUDPLATE_SERIALIZER_H
