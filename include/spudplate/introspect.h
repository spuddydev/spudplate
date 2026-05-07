#ifndef SPUDPLATE_INTROSPECT_H
#define SPUDPLATE_INTROSPECT_H

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "spudplate/ast.h"
#include "spudplate/interpreter.h"

namespace spudplate {

/**
 * @brief Information about a single top-level `ask` in a program.
 *
 * "Top-level" means the `ask` statement is at program scope, not nested
 * inside `repeat` or `if`. These are exactly the questions that can be
 * pre-answered, via either `include` `with`-args or `run --answers`.
 *
 * The pointers reference nodes owned by the source `Program`. They remain
 * valid for the lifetime of that program and become dangling once it is
 * destroyed.
 */
struct TopLevelAsk {
    std::string name;             ///< Variable name bound by the ask.
    VarType var_type;             ///< Declared answer type.
    std::string prompt;           ///< Human-readable prompt text.
    const Expr* default_value;    ///< Default-value expression, or null.
    const std::vector<ExprPtr>* options;  ///< Allowed values, never null.
    const Expr* when_clause;      ///< Conditional gate, or null.
    int line;
    int column;
};

/**
 * @brief Walk a program and collect every top-level `ask` in source order.
 *
 * Skips asks nested inside `repeat` or `if` bodies, which cannot be
 * pre-answered (the bundler enforces the same rule for `with`-args).
 */
std::vector<TopLevelAsk> collect_top_level_asks(const Program& program);

/**
 * @brief Render a human-readable list of top-level questions.
 *
 * One line per question, of the form
 * `  name (type): prompt [default: ...] [options: ...] [when: gated]`.
 * The `when` annotation appears only when the ask carries a `when` clause.
 */
void emit_questions_human(std::ostream& out,
                          const std::vector<TopLevelAsk>& asks);

/**
 * @brief Render a YAML answer template for the given questions.
 *
 * The output is a top-level mapping of `name: value` pairs preceded by a
 * comment block summarising each ask. Defaults are pre-filled where they
 * are literal; non-literal defaults yield an empty placeholder.
 */
void emit_questions_yaml(std::ostream& out,
                         const std::vector<TopLevelAsk>& asks);

/**
 * @brief Error thrown when a YAML answers file cannot be parsed or coerced.
 *
 * `line` is 1-based and refers to the answers file. Zero means the error
 * has no line context (e.g. an unknown key reported by the caller).
 */
struct AnswersParseError : std::runtime_error {
    AnswersParseError(std::string msg, int line)
        : std::runtime_error(std::move(msg)), line_(line) {}
    [[nodiscard]] int line() const noexcept { return line_; }

  private:
    int line_;
};

/**
 * @brief Parse a YAML answers file and coerce values against the asks.
 *
 * Accepts a deliberately small subset of YAML: a single top-level mapping
 * of `key: value` entries, where values are strings (quoted or bare),
 * integers, or booleans. Comments (`#`) and blank lines are ignored. No
 * nesting, lists, or block scalars are supported.
 *
 * Each key must match the name of a top-level ask; unknown keys throw
 * `AnswersParseError`. Each value is coerced to the matching ask's
 * declared type; type mismatches throw `AnswersParseError` with the
 * offending line.
 */
std::unordered_map<std::string, Value> parse_answers_yaml(
    const std::string& source,
    const std::vector<TopLevelAsk>& asks);

}  // namespace spudplate

#endif  // SPUDPLATE_INTROSPECT_H
