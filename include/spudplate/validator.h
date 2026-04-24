#ifndef SPUDPLATE_VALIDATOR_H
#define SPUDPLATE_VALIDATOR_H

#include <stdexcept>
#include <string>

#include "spudplate/ast.h"

namespace spudplate {

/**
 * @brief Exception thrown when the semantic validator finds a rule violation.
 *
 * Mirrors ParseError in shape: carries the source line and column of the
 * offending node alongside a bare message string.
 */
class SemanticError : public std::runtime_error {
  public:
    SemanticError(const std::string& message, int line, int column)
        : std::runtime_error(message), line_(line), column_(column) {}

    /** @brief 1-based line number of the offending statement or expression. */
    [[nodiscard]] int line() const { return line_; }
    /** @brief 1-based column number of the offending statement or expression. */
    [[nodiscard]] int column() const { return column_; }

  private:
    int line_;
    int column_;
};

/**
 * @brief Walks a parsed Program and enforces spudlang semantic rules.
 *
 * Throws SemanticError on the first rule violation. Returns normally if the
 * program is valid.
 */
void validate(const Program& program);

}  // namespace spudplate

#endif  // SPUDPLATE_VALIDATOR_H
