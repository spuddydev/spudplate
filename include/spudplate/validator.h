#ifndef SPUDPLATE_VALIDATOR_H
#define SPUDPLATE_VALIDATOR_H

#include <stdexcept>
#include <string>
#include <unordered_map>

#include "spudplate/ast.h"

namespace spudplate {

/**
 * @brief Map from variable name to its declared type, built from `ask`
 * statements seen so far. Drives bool equivalence in `normalize`.
 */
using TypeMap = std::unordered_map<std::string, VarType>;

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

/**
 * @brief Deep-copy of an expression tree. Needed because `ExprPtr` is a
 * `unique_ptr` and cannot be copied implicitly.
 */
ExprPtr clone_expr(const Expr& expr);

/**
 * @brief Normalizes bool equivalences in `expr` using `tm` as the type map.
 *
 * Returns a freshly-allocated tree. Rules are applied bottom-up to fixpoint:
 * `x == true` ≡ `not not x` ≡ `x`; `not x` ≡ `x == false` ≡ `x != true`.
 * Non-bool comparisons pass through structurally. Commutativity is NOT applied.
 */
ExprPtr normalize(const Expr& expr, const TypeMap& tm);

/**
 * @brief Structural recursive equality for expression trees.
 *
 * Ignores `line` and `column` on every node. Recurses into every `ExprPtr`
 * child. For `FunctionCallExpr`, compares `name` then recurses into `argument`.
 */
bool exprs_equal(const Expr& a, const Expr& b);

}  // namespace spudplate

#endif  // SPUDPLATE_VALIDATOR_H
