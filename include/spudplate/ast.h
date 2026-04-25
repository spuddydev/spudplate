#ifndef SPUDPLATE_AST_H
#define SPUDPLATE_AST_H

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "spudplate/token.h"

namespace spudplate {

/** @brief A string literal expression node, e.g. `"hello"`. */
struct StringLiteralExpr {
    std::string value;  ///< The literal string value (without quotes).
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief An integer literal expression node, e.g. `42`. */
struct IntegerLiteralExpr {
    int value;  ///< The literal integer value.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief A bool literal expression node, e.g. `true` or `false`. */
struct BoolLiteralExpr {
    bool value;  ///< The literal boolean value.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief A variable reference expression node, e.g. `project_name`. */
struct IdentifierExpr {
    std::string name;  ///< The variable name.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

// Forward declare Expr so recursive types can hold pointers to it
struct Expr;
/** @brief Owning pointer to an expression node. */
using ExprPtr = std::unique_ptr<Expr>;

/** @brief A unary expression node, e.g. `not flag`. */
struct UnaryExpr {
    TokenType op;     ///< The operator token (NOT).
    ExprPtr operand;  ///< The operand expression.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A binary expression node, e.g. `a + b` or `x == y`.
 *
 * Covers arithmetic, comparison, and logical binary operators.
 */
struct BinaryExpr {
    TokenType op;   ///< The operator token.
    ExprPtr left;   ///< Left-hand operand.
    ExprPtr right;  ///< Right-hand operand.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A built-in function call expression, e.g. `lower(name)` or `replace(s, " ", "-")`.
 *
 * Arity is checked at evaluation time per-function. Supported functions:
 * `lower`, `upper`, `trim` (1 arg), `replace` (3 args).
 */
struct FunctionCallExpr {
    std::string name;                  ///< Function name.
    std::vector<ExprPtr> arguments;    ///< Argument expressions.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A string literal containing one or more `{expr}` interpolations.
 *
 * Produced by the parser when a `"..."` string literal contains any
 * `{...}` segment. Each `parts` entry is either a literal string fragment
 * (the text between braces) or an expression to evaluate and stringify.
 * Evaluation concatenates the stringified parts in order. A plain string
 * literal (no `{...}`) parses as a `StringLiteralExpr`; this type is used
 * only when at least one interpolation is present.
 */
struct TemplateStringExpr {
    std::vector<std::variant<std::string, ExprPtr>> parts;  ///< Ordered literal/expression segments.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief Discriminated union of all expression node types. */
using ExprData = std::variant<StringLiteralExpr, IntegerLiteralExpr, BoolLiteralExpr,
                              IdentifierExpr, UnaryExpr, BinaryExpr, FunctionCallExpr,
                              TemplateStringExpr>;

/** @brief An expression node wrapping an ExprData variant. */
struct Expr {
    ExprData data;  ///< The concrete expression alternative.
};

/** @brief The type of a variable declared by an `ask` statement. */
enum class VarType { String, Bool, Int };

// ---------------------------------------------------------------------------
// Path expression types
// ---------------------------------------------------------------------------

/** @brief A literal component of a path expression, e.g. `src` or `README.md`. */
struct PathLiteral {
    std::string value;  ///< Literal text (identifiers, dots, slashes already joined).
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief A reference to a previously-bound path alias, e.g. `staticpath` in `staticpath/notes`. */
struct PathVar {
    std::string name;  ///< The alias name.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief An inline `{expr}` interpolation inside a path, e.g. `week_{n}`. */
struct PathInterp {
    ExprPtr expression;  ///< The expression whose string value is substituted at runtime.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief Discriminated union of the three path segment kinds. */
using PathSegment = std::variant<PathLiteral, PathVar, PathInterp>;

/**
 * @brief A path expression — an ordered sequence of segments.
 *
 * Used by `mkdir`, `file`, and `file ... from` for destination and source paths.
 * Segments are concatenated at runtime, with literals inserted verbatim and
 * interpolations/aliases resolved from the current scope.
 */
struct PathExpr {
    std::vector<PathSegment> segments;  ///< Ordered path components.
    int line;                           ///< Line of the first segment.
    int column;                         ///< Column of the first segment.
};

/** @brief File source: content comes from an embedded source file. */
struct FileFromSource {
    PathExpr path;  ///< Path to the source file (resolved at compile time).
    bool verbatim;  ///< If true, suppress `{var}` interpolation at runtime.
};

/** @brief File source: content comes from an inline expression. */
struct FileContentSource {
    ExprPtr value;  ///< The expression whose string value becomes the file content.
};

/** @brief Discriminated union of the two file content sources. */
using FileSource = std::variant<FileFromSource, FileContentSource>;

// ---------------------------------------------------------------------------
// Statement types
// ---------------------------------------------------------------------------

/**
 * @brief An `ask` statement — declares a variable and prompts the user.
 *
 * Example: `ask license "License?" string default "MIT"`
 *
 * Without a `default` clause the prompt is required; with one, the answer
 * may be skipped and the default literal is used in its place.
 */
struct AskStmt {
    std::string name;                         ///< Variable name to bind the answer to.
    std::string prompt;                       ///< The prompt string shown to the user.
    VarType var_type;                         ///< Expected type of the answer.
    std::optional<ExprPtr> default_value;     ///< Expression evaluated when the user skips the prompt.
    std::vector<ExprPtr> options;             ///< Allowed literal values; empty means any.
    std::optional<ExprPtr> when_clause;       ///< Optional condition guarding the prompt.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A `let` statement — declares a derived variable.
 *
 * Example: `let slug = lower(trim(name))`
 */
struct LetStmt {
    std::string name;  ///< Variable name to bind.
    ExprPtr value;     ///< Expression whose value is assigned.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A bare assignment statement — rebinds an existing variable.
 *
 * Example: `count = count + 1`
 *
 * Unlike `let`, the name must already be declared in an enclosing scope and
 * the new value must match the original type. Path aliases bound by `as`,
 * repeat iterators, and `ask`-bound names are read-only and may not be
 * reassigned.
 */
struct AssignStmt {
    std::string name;  ///< Variable name to rebind.
    ExprPtr value;     ///< Expression whose value replaces the current binding.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A `mkdir` statement — creates a directory.
 *
 * Example: `mkdir src/modules mode 0755`
 *
 * With a `from <source>` clause, the directory is created and populated from
 * the source directory atomically. Without `from`, an empty directory is made.
 */
struct MkdirStmt {
    PathExpr path;                         ///< Directory path expression.
    std::optional<std::string> alias;      ///< Optional `as <name>` binding; empty if no `as` clause.
    bool mkdir_p;                          ///< Always true — create intermediate directories.
    std::optional<PathExpr> from_source;   ///< Optional source directory to populate from.
    bool verbatim;                         ///< If true with from_source, suppress interpolation.
    std::optional<int> mode;               ///< Optional permission bits (e.g. 0755).
    std::optional<ExprPtr> when_clause;    ///< Optional condition guarding creation.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A `file` statement — creates or appends to a file.
 *
 * Example: `file "{slug}/README.md" content "# " + name`
 */
struct FileStmt {
    PathExpr path;                        ///< File path expression.
    std::optional<std::string> alias;     ///< Optional `as <name>` binding; empty if no `as` clause.
    FileSource source;                    ///< Where the file content comes from.
    bool append;                          ///< If true, append; otherwise create/overwrite.
    std::optional<int> mode;              ///< Optional permission bits.
    std::optional<ExprPtr> when_clause;   ///< Optional condition guarding creation.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

// Forward declare Stmt so RepeatStmt can hold a vector of them
struct Stmt;
/** @brief Owning pointer to a statement node. */
using StmtPtr = std::unique_ptr<Stmt>;

/**
 * @brief A `repeat` statement — loops a block N times.
 *
 * Example:
 * @code
 * repeat count as i
 *     mkdir "module_{i}"
 * end
 * @endcode
 */
struct RepeatStmt {
    std::string collection_var;          ///< Name of the int variable holding the count.
    std::string iterator_var;            ///< Name of the loop index variable (0-based).
    std::vector<StmtPtr> body;           ///< Statements inside the loop body.
    std::optional<ExprPtr> when_clause;  ///< Optional condition guarding the whole loop.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A `copy` statement — copies contents from a source directory into
 * an existing destination directory.
 *
 * Example: `copy standard_templates/ into templatepath`
 *
 * Unlike `mkdir ... from`, the destination directory must already exist. Use
 * `copy` to merge multiple sources into a single destination.
 */
struct CopyStmt {
    PathExpr source;                     ///< Source directory path.
    PathExpr destination;                ///< Existing destination directory path.
    bool verbatim;                       ///< If true, suppress `{var}` interpolation.
    std::optional<ExprPtr> when_clause;  ///< Optional condition guarding the copy.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief A `run` statement — executes a shell command after user authorisation.
 *
 * Example: `run "git init" in {dir} when use_git`
 *
 * The command expression is evaluated to a string at runtime and dispatched
 * via `/bin/sh -c`. The optional `in <path>` clause pins the working
 * directory for the command — without it, the command inherits the cwd of
 * the `spudplate` process. Each `spudplate run` invocation prompts the
 * user once, up front, listing every literal command (and its `in <path>`,
 * if any) before any statement executes; declining aborts cleanly with no
 * side effects.
 */
struct RunStmt {
    ExprPtr command;                     ///< Expression whose string value is the command.
    std::optional<PathExpr> cwd;         ///< Optional `in <path>` working directory.
    std::optional<ExprPtr> when_clause;  ///< Optional condition guarding execution.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/**
 * @brief An `include` statement — runs another installed template as a subprocess.
 *
 * Example: `include claude_setup when use_claude`
 *
 * The named template is referenced by its installed name and runs in its own
 * scope at runtime. The optional `when` clause skips the include if false.
 */
struct IncludeStmt {
    std::string name;                    ///< Name of the installed template to run.
    std::optional<ExprPtr> when_clause;  ///< Optional condition guarding the include.
    int line;    ///< 1-based source line where this node begins.
    int column;  ///< 1-based source column where this node begins.
};

/** @brief Discriminated union of all statement node types. */
using StmtData = std::variant<AskStmt, LetStmt, AssignStmt, MkdirStmt, FileStmt,
                              RepeatStmt, CopyStmt, IncludeStmt, RunStmt>;

/** @brief A statement node wrapping a StmtData variant. */
struct Stmt {
    StmtData data;  ///< The concrete statement alternative.
};

/** @brief The top-level AST node representing a complete `.spud` program. */
struct Program {
    std::vector<StmtPtr> statements;  ///< Ordered list of top-level statements.
};

}  // namespace spudplate

#endif  // SPUDPLATE_AST_H
