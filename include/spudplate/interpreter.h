#ifndef SPUDPLATE_INTERPRETER_H
#define SPUDPLATE_INTERPRETER_H

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "spudplate/ast.h"

namespace spudplate {

/**
 * @brief Exception thrown when the interpreter encounters a runtime failure.
 *
 * Mirrors ParseError and SemanticError in shape: carries the source line and
 * column of the offending node alongside a bare message string.
 */
class RuntimeError : public std::runtime_error {
  public:
    RuntimeError(const std::string& message, int line, int column)
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
 * @brief A runtime value carried in the interpreter's environment.
 *
 * Matches the three spudlang types: string, int, bool. The integer alternative
 * is `int64_t` to give arithmetic enough headroom; literals from
 * `IntegerLiteralExpr` are cast on entry.
 */
using Value = std::variant<std::string, std::int64_t, bool>;

/**
 * @brief Variable storage for a running interpreter.
 *
 * Frame stack mirroring the validator's `Scope` shape (see
 * `src/validator.cpp`). frames[0] is the program-level scope. Each `repeat`
 * body pushes a new frame on entry and pops it on exit.
 *
 * Exposed in the public header solely so tests can observe the final
 * environment after a run; production code should not depend on this type.
 */
class Environment {
  public:
    Environment() { push(); }

    void push() { frames_.emplace_back(); }
    void pop() { frames_.pop_back(); }

    /**
     * @brief Bind `name` to `value` in the innermost frame.
     *
     * Throws `std::logic_error` if `name` is already visible in any frame —
     * the language never silently shadows.
     */
    void declare(const std::string& name, Value value);

    /** @brief Find `name` in any frame, innermost first. */
    [[nodiscard]] std::optional<Value> lookup(const std::string& name) const;

  private:
    std::vector<std::unordered_map<std::string, Value>> frames_;
};

/**
 * @brief Abstract source of user input for `ask` statements.
 */
class Prompter {
  public:
    virtual ~Prompter() = default;

    /**
     * @brief Display `message` and obtain the user's raw answer string.
     *
     * The interpreter parses the returned string into a `Value` according to
     * the question's `type` and re-prompts on invalid input — implementations
     * just deliver one raw line per call.
     */
    virtual std::string prompt(const std::string& message, VarType type) = 0;
};

/**
 * @brief Production prompter: writes to stdout and reads a line from stdin.
 */
class StdinPrompter : public Prompter {
  public:
    std::string prompt(const std::string& message, VarType type) override;
};

/**
 * @brief Test prompter: replays a fixed sequence of canned answers.
 *
 * Constructed with a vector of strings; each call to `prompt` consumes the
 * next answer. Calling `prompt` after the queue is exhausted throws
 * `std::logic_error`, so test retry-loop bugs cannot hang.
 */
class ScriptedPrompter : public Prompter {
  public:
    explicit ScriptedPrompter(std::vector<std::string> answers)
        : answers_(std::move(answers)) {}

    std::string prompt(const std::string& message, VarType type) override;

  private:
    std::vector<std::string> answers_;
    std::size_t index_{0};
};

/**
 * @brief Run a validated program.
 *
 * Walks `program.statements` top-to-bottom, prompting through `prompter` for
 * `ask` statements and queueing filesystem operations to be flushed at the
 * end of the run. Throws `RuntimeError` on the first failure.
 */
void run(const Program& program, Prompter& prompter);

/**
 * @brief Test-only entry point: like `run`, but returns the final environment.
 *
 * Production callers should use `run`; this is exposed so tests can assert
 * on bindings without relying on filesystem side effects.
 */
Environment run_for_tests(const Program& program, Prompter& prompter);

/**
 * @brief Evaluate an expression against an environment.
 *
 * Throws `RuntimeError` on type mismatch, unknown identifier, division by
 * zero, integer overflow on division, or unknown function name. Arithmetic
 * `+`, `-`, `*` use signed two's-complement wrap-around. Logical `and`/`or`
 * short-circuit. Equality `==`/`!=` returns `false`/`true` for operands of
 * differing variant alternatives.
 */
Value evaluate_expr(const Expr& expr, const Environment& env);

/**
 * @brief Render a `Value` for use in path interpolation or file content.
 *
 * Strings pass through; ints become decimal with no padding; bools become
 * `"true"` or `"false"`.
 */
std::string value_to_string(const Value& value);

/**
 * @brief Resolved bindings for path aliases declared by `as` clauses.
 *
 * Maps the alias name to the already-resolved destination path string. The
 * parser guarantees that `PathVar` segments only ever name a registered alias,
 * so the evaluator never falls back to an environment lookup for them.
 */
using AliasMap = std::unordered_map<std::string, std::string>;

/**
 * @brief Evaluate a path expression to a flat string.
 *
 * Concatenates segment outputs verbatim (the parser already embeds `/` and
 * `.` inside `PathLiteral.value`, so no separator insertion is needed).
 * `PathInterp` segments evaluate their inner expression and stringify the
 * result via `value_to_string`. `PathVar` segments look up the alias name in
 * `aliases`; an unbound alias is a defensive runtime error since the
 * validator already guarantees presence in well-formed programs.
 */
std::string evaluate_path(const PathExpr& path, const Environment& env,
                          const AliasMap& aliases);

}  // namespace spudplate

#endif  // SPUDPLATE_INTERPRETER_H
