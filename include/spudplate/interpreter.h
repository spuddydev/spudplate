#ifndef SPUDPLATE_INTERPRETER_H
#define SPUDPLATE_INTERPRETER_H

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "spudplate/ast.h"
#include "spudplate/spudpack.h"

namespace spudplate {

/**
 * @brief Exception thrown when the interpreter encounters a runtime failure.
 *
 * Mirrors ParseError and SemanticError in shape: carries the source line and
 * column of the offending node alongside a bare message string.
 */
class RuntimeError : public std::runtime_error {
  public:
    /** @brief Construct an error tagged with the offending source position. */
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
    /** @brief Start with one program-level frame already on the stack. */
    Environment() { push(); }

    /** @brief Push a fresh frame for a nested scope (e.g. `repeat` body). */
    void push() { frames_.emplace_back(); }
    /** @brief Pop the innermost frame, discarding any bindings it held. */
    void pop() { frames_.pop_back(); }

    /**
     * @brief Bind `name` to `value` in the innermost frame.
     *
     * Throws `std::logic_error` if `name` is already visible in any frame —
     * the language never silently shadows.
     */
    void declare(const std::string& name, Value value);

    /**
     * @brief Replace the value of an existing binding, innermost first.
     *
     * Throws `std::logic_error` if `name` is not visible — the validator
     * is expected to have rejected such inputs before the interpreter runs.
     */
    void assign(const std::string& name, Value value);

    /** @brief Find `name` in any frame, innermost first. */
    [[nodiscard]] std::optional<Value> lookup(const std::string& name) const;

  private:
    std::vector<std::unordered_map<std::string, Value>> frames_;
};

/**
 * @brief Structured description of an `ask` prompt for the prompter to render.
 *
 * The interpreter populates this struct from an `AskStmt` and the live
 * environment, then hands it to the prompter. Rendering — colour, layout,
 * `[Y/n]` hints, numbered option menus, rejection feedback — is the
 * prompter's responsibility.
 */
struct PromptRequest {
    std::string text;                         ///< The question text from the `.spud` source.
    VarType type;                             ///< Expected answer type.
    std::vector<std::string> options;         ///< Stringified allowed answers; empty means any.
    std::optional<std::string> default_value; ///< Stringified default; empty means required.
    std::optional<std::string> previous_error;///< Set on retry; describes why the prior answer was rejected.
    int question_index{0};                    ///< 1-based position of this question among presented ones; 0 suppresses the counter.
    int question_total{0};                    ///< Total static `ask` statements in the program; 0 suppresses the counter.
    int indent_level{0};                      ///< Number of nested `repeat` blocks above this prompt; renders as 2 spaces per level.
};

/**
 * @brief Abstract source of user input for `ask` statements.
 */
class Prompter {
  public:
    virtual ~Prompter() = default;

    /**
     * @brief Display the prompt described by `req` and return the user's raw answer.
     *
     * The interpreter parses the returned string, validates it against type
     * and options, and on rejection calls back with `previous_error` set.
     * Implementations deliver one raw line per call.
     */
    virtual std::string prompt(const PromptRequest& req) = 0;

    /**
     * @brief Display a security summary and return whether to proceed.
     *
     * Called once, before any statement runs, when the program contains
     * `run` clauses. The summary is multi-line and lists every literal
     * command that may execute. Returning `false` aborts the run cleanly
     * with no side effects.
     */
    virtual bool authorize(const std::string& summary) = 0;
};

/**
 * @brief Production prompter: writes to a stream and reads a line from another.
 *
 * Defaults to `std::cin` and `std::cout` with auto-detected colour support
 * (suppressed when `NO_COLOR` is set or stdout is not a tty). The
 * stream-injecting constructor exists for tests.
 */
class StdinPrompter : public Prompter {
  public:
    /** @brief Default constructor: read from `std::cin`, write to `std::cout`,
     *         auto-detect colour support. */
    StdinPrompter();
    /** @brief Inject custom streams and explicit colour mode (test-only). */
    StdinPrompter(std::istream& in, std::ostream& out, bool use_colour);

    std::string prompt(const PromptRequest& req) override;
    bool authorize(const std::string& summary) override;

  private:
    std::istream& in_;
    std::ostream& out_;
    bool use_colour_;
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
    /** @brief Construct with the canned answers `prompt` will replay in order. */
    explicit ScriptedPrompter(std::vector<std::string> answers)
        : answers_(std::move(answers)) {}

    std::string prompt(const PromptRequest& req) override;
    bool authorize(const std::string& summary) override;

    /** @brief Most recent request seen, for assertions on rendering inputs. */
    [[nodiscard]] const std::optional<PromptRequest>& last_request() const {
        return last_;
    }

    /** @brief Set what `authorize` returns. Defaults to true (accept). */
    void set_authorize_response(bool value) { authorize_response_ = value; }

    /** @brief Most recent authorize summary seen, for assertions. */
    [[nodiscard]] const std::optional<std::string>& last_authorize_summary() const {
        return last_authorize_summary_;
    }

  private:
    std::vector<std::string> answers_;
    std::size_t index_{0};
    std::optional<PromptRequest> last_;
    bool authorize_response_{true};
    std::optional<std::string> last_authorize_summary_;
};

/**
 * @brief Source of bundled assets the interpreter draws from for `from`/`copy`.
 *
 * Two concrete implementations live in `src/interpreter.cpp`: a disk-backed
 * provider (cwd-relative reads, used when no asset map is supplied) and an
 * asset-map provider (used when running an installed `.spp`).
 *
 * Asset paths are bundle-root-relative and pre-normalised by
 * `spudplate::normalize_asset_path` (forward-slash, no leading `/`, no `.`
 * or `..` segments, no embedded NUL). A trailing `/` on a returned `Entry`
 * path means "empty leaf directory".
 *
 * `mode == 0` is the documented "no mode information" sentinel — the
 * disk-backed provider always reports zero so callers preserve the
 * pre-existing `nullopt`-mode behaviour of bare-`.spud` runs.
 */
class SourceProvider {
  public:
    virtual ~SourceProvider() = default;

    /** @brief One entry returned by `list_under`. */
    struct Entry {
        std::string path;       ///< Normalised bundle-root-relative path.
        bool is_directory;      ///< True for directories (intermediate or empty leaf).
        std::uint16_t mode;     ///< Permission bits, or 0 for "no mode information".
    };

    /**
     * @brief Read the bytes of an asset by its normalised path.
     *
     * Returns the content and the asset mode (or 0 when the provider does
     * not carry mode information).
     */
    virtual std::pair<std::vector<std::uint8_t>, std::uint16_t> read(
        std::string_view path) const = 0;

    /**
     * @brief List every entry under the given prefix, including
     * synthesised intermediate directories.
     *
     * The returned list is in pre-order (parent before children) so callers
     * that map entries to filesystem operations naturally satisfy the
     * create-parent-first ordering.
     */
    virtual std::vector<Entry> list_under(std::string_view prefix) const = 0;
};

/**
 * @brief `SourceProvider` backed by a borrowed asset map.
 *
 * Used by `cmd_run` when running an installed `.spp`. The provider holds
 * a reference to the asset vector inside the decoded `Spudpack`; the
 * caller must keep that pack alive for the duration of any call into the
 * interpreter. `cmd_run` does so by scoping the `Spudpack` and the
 * provider in the same lexical block.
 */
class AssetMapSourceProvider final : public SourceProvider {
  public:
    /** @brief Construct over a borrowed asset vector; the caller owns the lifetime. */
    explicit AssetMapSourceProvider(const std::vector<SpudpackAsset>& assets);

    std::pair<std::vector<std::uint8_t>, std::uint16_t> read(
        std::string_view path) const override;
    std::vector<Entry> list_under(std::string_view prefix) const override;

  private:
    const std::vector<SpudpackAsset>& assets_;
    std::unordered_map<std::string, std::size_t> index_;
};

/**
 * @brief Run a validated program.
 *
 * Walks `program.statements` top-to-bottom, prompting through `prompter` for
 * `ask` statements and queueing filesystem operations to be flushed at the
 * end of the run. Throws `RuntimeError` on the first failure.
 *
 * If the program contains any `run` statements, `prompter.authorize` is
 * called once before any statement executes, with a summary of every
 * literal command. A `false` return aborts the run cleanly with no side
 * effects. The `skip_authorization` flag bypasses the prompt for non-
 * interactive callers — they take responsibility for having vetted the
 * source.
 *
 * `source` overrides where `from`/`copy` reads come from. A null `source`
 * (the default) routes reads through a disk-backed provider so existing
 * bare-`.spud` callers see the historical cwd-relative behaviour.
 */
void run(const Program& program, Prompter& prompter,
         bool skip_authorization = false,
         const SourceProvider* source = nullptr);

/**
 * @brief Test-only entry point: like `run`, but returns the final environment.
 *
 * Production callers should use `run`; this is exposed so tests can assert
 * on bindings without relying on filesystem side effects.
 */
Environment run_for_tests(const Program& program, Prompter& prompter,
                          const SourceProvider* source = nullptr);

/**
 * @brief Run a program without touching the filesystem.
 *
 * Walks the program exactly like `run` — same prompts, same expression
 * evaluation, same alias bindings — but instead of flushing the deferred
 * write queue prints a `Would create:` tree of every path that would have
 * been created to `out`. `copy` destination-existence checks are skipped
 * (they would always fail in dry-run since nothing was written).
 *
 * Tree glyphs default to UTF-8 box drawing (`├──`/`└──`/`│  `). Pass
 * `ascii_only = true` to fall back to plain ASCII (`|--`/`\\--`/`|  `) for
 * terminals that don't render the box-drawing characters cleanly.
 *
 * Throws `RuntimeError` on any failure that `run` would also throw before
 * the flush step.
 */
void dry_run(const Program& program, Prompter& prompter, std::ostream& out,
             bool ascii_only = false,
             const SourceProvider* source = nullptr);

/**
 * @brief Heuristic check: does the current environment look UTF-8 capable?
 *
 * Looks at `LC_ALL`, `LC_CTYPE`, `LANG` (in that order) and returns true if
 * any contains "UTF-8" or "utf8" (case-insensitive). Used by the CLI to
 * decide whether to render the dry-run tree with UTF-8 box-drawing glyphs
 * or fall back to ASCII.
 */
bool locale_is_utf8();

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
