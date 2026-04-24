#include "spudplate/validator.h"

#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace spudplate {

namespace {

// Scope stack. frames[0] is the program-level scope and is never popped. Each
// `repeat` body pushes a new frame on entry and pops it on exit. Names that
// leave a popped frame go into `popped` so we can reject later references.
struct Scope {
    std::vector<std::unordered_set<std::string>> frames;
    std::unordered_set<std::string> popped;

    void push() { frames.emplace_back(); }

    void pop() {
        for (const auto& n : frames.back()) {
            popped.insert(n);
        }
        frames.pop_back();
    }

    [[nodiscard]] bool visible(const std::string& name) const {
        for (const auto& f : frames) {
            if (f.count(name) != 0U) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool out_of_scope(const std::string& name) const {
        return popped.count(name) != 0U && !visible(name);
    }

    void declare(const std::string& name) { frames.back().insert(name); }

    [[nodiscard]] bool inside_repeat() const { return frames.size() > 1; }
};

void walk_expr(const Expr& expr, const Scope& scope);
void walk_path(const PathExpr& path, const Scope& scope);
void validate_stmt(const Stmt& stmt, Scope& scope);

void check_reference(const std::string& name, int line, int column,
                     const Scope& scope) {
    if (scope.out_of_scope(name)) {
        throw SemanticError("reference to out-of-scope name '" + name + "'", line,
                            column);
    }
}

void walk_expr(const Expr& expr, const Scope& scope) {
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, IdentifierExpr>) {
                check_reference(e.name, e.line, e.column, scope);
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                walk_expr(*e.operand, scope);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                walk_expr(*e.left, scope);
                walk_expr(*e.right, scope);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                walk_expr(*e.argument, scope);
            }
            // String, Integer, Bool literals have no children.
        },
        expr.data);
}

void walk_path(const PathExpr& path, const Scope& scope) {
    for (const auto& seg : path.segments) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, PathVar>) {
                    check_reference(s.name, s.line, s.column, scope);
                } else if constexpr (std::is_same_v<T, PathInterp>) {
                    walk_expr(*s.expression, scope);
                }
                // PathLiteral has no children.
            },
            seg);
    }
}

void walk_optional_expr(const std::optional<ExprPtr>& opt, const Scope& scope) {
    if (opt.has_value()) {
        walk_expr(**opt, scope);
    }
}

void check_shadowing(const std::string& name, int line, int column,
                     const Scope& scope) {
    if (scope.visible(name)) {
        throw SemanticError("shadowing of visible name '" + name + "'", line, column);
    }
}

void validate_stmt(const Stmt& stmt, Scope& scope) {
    std::visit(
        [&](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, AskStmt>) {
                if (scope.inside_repeat()) {
                    throw SemanticError("'ask' not allowed inside 'repeat'", s.line,
                                        s.column);
                }
                if (s.default_value.has_value()) {
                    walk_expr(**s.default_value, scope);
                }
                for (const auto& opt : s.options) {
                    walk_expr(*opt, scope);
                }
                walk_optional_expr(s.when_clause, scope);
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                walk_expr(*s.value, scope);
                check_shadowing(s.name, s.line, s.column, scope);
                scope.declare(s.name);
            } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                walk_path(s.path, scope);
                if (s.from_source.has_value()) {
                    walk_path(*s.from_source, scope);
                }
                walk_optional_expr(s.when_clause, scope);
                if (s.alias.has_value()) {
                    check_shadowing(*s.alias, s.line, s.column, scope);
                    scope.declare(*s.alias);
                }
            } else if constexpr (std::is_same_v<T, FileStmt>) {
                walk_path(s.path, scope);
                std::visit(
                    [&](const auto& src) {
                        using ST = std::decay_t<decltype(src)>;
                        if constexpr (std::is_same_v<ST, FileFromSource>) {
                            walk_path(src.path, scope);
                        } else if constexpr (std::is_same_v<ST, FileContentSource>) {
                            walk_expr(*src.value, scope);
                        }
                    },
                    s.source);
                walk_optional_expr(s.when_clause, scope);
                if (s.alias.has_value()) {
                    check_shadowing(*s.alias, s.line, s.column, scope);
                    scope.declare(*s.alias);
                }
            } else if constexpr (std::is_same_v<T, CopyStmt>) {
                walk_path(s.source, scope);
                walk_path(s.destination, scope);
                walk_optional_expr(s.when_clause, scope);
            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                check_reference(s.collection_var, s.line, s.column, scope);
                walk_optional_expr(s.when_clause, scope);
                check_shadowing(s.iterator_var, s.line, s.column, scope);
                scope.push();
                scope.declare(s.iterator_var);
                for (const auto& inner : s.body) {
                    validate_stmt(*inner, scope);
                }
                scope.pop();
            }
        },
        stmt.data);
}

}  // namespace

void validate(const Program& program) {
    Scope scope;
    scope.push();  // program-level frame, never popped.
    for (const auto& stmt : program.statements) {
        validate_stmt(*stmt, scope);
    }
}

}  // namespace spudplate
