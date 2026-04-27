#include "test_helpers.h"

#include <variant>

namespace spudplate::test {

namespace {

bool optional_expr_equal(const std::optional<ExprPtr>& a,
                         const std::optional<ExprPtr>& b);
bool optional_path_expr_equal(const std::optional<PathExpr>& a,
                              const std::optional<PathExpr>& b);

bool ptr_expr_equal(const ExprPtr& a, const ExprPtr& b) {
    if (!a || !b) return !a && !b;
    return exprs_equal(*a, *b);
}

bool template_part_equal(const std::variant<std::string, ExprPtr>& a,
                         const std::variant<std::string, ExprPtr>& b) {
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<std::string>(a)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    }
    return ptr_expr_equal(std::get<ExprPtr>(a), std::get<ExprPtr>(b));
}

bool string_expr_vector_equal(const std::vector<ExprPtr>& a,
                              const std::vector<ExprPtr>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!ptr_expr_equal(a[i], b[i])) return false;
    }
    return true;
}

bool path_segment_equal(const PathSegment& a, const PathSegment& b) {
    if (a.index() != b.index()) return false;
    return std::visit(
        [&](const auto& av) -> bool {
            using T = std::decay_t<decltype(av)>;
            const auto& bv = std::get<T>(b);
            if constexpr (std::is_same_v<T, PathLiteral>) {
                return av.value == bv.value && av.line == bv.line &&
                       av.column == bv.column;
            } else if constexpr (std::is_same_v<T, PathVar>) {
                return av.name == bv.name && av.line == bv.line &&
                       av.column == bv.column;
            } else {
                static_assert(std::is_same_v<T, PathInterp>);
                return ptr_expr_equal(av.expression, bv.expression) &&
                       av.line == bv.line && av.column == bv.column;
            }
        },
        a);
}

bool optional_expr_equal(const std::optional<ExprPtr>& a,
                         const std::optional<ExprPtr>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a.has_value()) return true;
    return ptr_expr_equal(*a, *b);
}

bool optional_path_expr_equal(const std::optional<PathExpr>& a,
                              const std::optional<PathExpr>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a.has_value()) return true;
    return path_exprs_equal(*a, *b);
}

}  // namespace

bool exprs_equal(const Expr& a, const Expr& b) {
    if (a.data.index() != b.data.index()) return false;
    return std::visit(
        [&](const auto& av) -> bool {
            using T = std::decay_t<decltype(av)>;
            const auto& bv = std::get<T>(b.data);
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                return av.value == bv.value && av.line == bv.line &&
                       av.column == bv.column;
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                return av.value == bv.value && av.line == bv.line &&
                       av.column == bv.column;
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                return av.value == bv.value && av.line == bv.line &&
                       av.column == bv.column;
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                return av.name == bv.name && av.line == bv.line &&
                       av.column == bv.column;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return av.op == bv.op &&
                       ptr_expr_equal(av.operand, bv.operand) &&
                       av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return av.op == bv.op && ptr_expr_equal(av.left, bv.left) &&
                       ptr_expr_equal(av.right, bv.right) &&
                       av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                return av.name == bv.name &&
                       string_expr_vector_equal(av.arguments, bv.arguments) &&
                       av.line == bv.line && av.column == bv.column;
            } else {
                static_assert(std::is_same_v<T, TemplateStringExpr>);
                if (av.parts.size() != bv.parts.size()) return false;
                for (std::size_t i = 0; i < av.parts.size(); ++i) {
                    if (!template_part_equal(av.parts[i], bv.parts[i])) {
                        return false;
                    }
                }
                return av.line == bv.line && av.column == bv.column;
            }
        },
        a.data);
}

bool path_exprs_equal(const PathExpr& a, const PathExpr& b) {
    if (a.segments.size() != b.segments.size()) return false;
    if (a.line != b.line || a.column != b.column) return false;
    for (std::size_t i = 0; i < a.segments.size(); ++i) {
        if (!path_segment_equal(a.segments[i], b.segments[i])) return false;
    }
    return true;
}

bool file_sources_equal(const FileSource& a, const FileSource& b) {
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<FileFromSource>(a)) {
        const auto& av = std::get<FileFromSource>(a);
        const auto& bv = std::get<FileFromSource>(b);
        return path_exprs_equal(av.path, bv.path) && av.verbatim == bv.verbatim;
    }
    const auto& av = std::get<FileContentSource>(a);
    const auto& bv = std::get<FileContentSource>(b);
    return ptr_expr_equal(av.value, bv.value);
}

bool stmts_equal(const Stmt& a, const Stmt& b) {
    if (a.data.index() != b.data.index()) return false;
    return std::visit(
        [&](const auto& av) -> bool {
            using T = std::decay_t<decltype(av)>;
            const auto& bv = std::get<T>(b.data);
            if constexpr (std::is_same_v<T, AskStmt>) {
                if (av.name != bv.name || av.prompt != bv.prompt ||
                    av.var_type != bv.var_type) {
                    return false;
                }
                if (!optional_expr_equal(av.default_value, bv.default_value)) {
                    return false;
                }
                if (!string_expr_vector_equal(av.options, bv.options)) {
                    return false;
                }
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                return av.name == bv.name &&
                       ptr_expr_equal(av.value, bv.value) &&
                       av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, AssignStmt>) {
                return av.name == bv.name &&
                       ptr_expr_equal(av.value, bv.value) &&
                       av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                if (!path_exprs_equal(av.path, bv.path)) return false;
                if (av.alias != bv.alias) return false;
                if (av.mkdir_p != bv.mkdir_p) return false;
                if (!optional_path_expr_equal(av.from_source, bv.from_source)) {
                    return false;
                }
                if (av.verbatim != bv.verbatim) return false;
                if (av.mode != bv.mode) return false;
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, FileStmt>) {
                if (!path_exprs_equal(av.path, bv.path)) return false;
                if (av.alias != bv.alias) return false;
                if (!file_sources_equal(av.source, bv.source)) return false;
                if (av.append != bv.append) return false;
                if (av.mode != bv.mode) return false;
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                if (av.collection_var != bv.collection_var ||
                    av.iterator_var != bv.iterator_var) {
                    return false;
                }
                if (av.body.size() != bv.body.size()) return false;
                for (std::size_t i = 0; i < av.body.size(); ++i) {
                    if (!av.body[i] || !bv.body[i]) {
                        if (av.body[i] || bv.body[i]) return false;
                        continue;
                    }
                    if (!stmts_equal(*av.body[i], *bv.body[i])) return false;
                }
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, CopyStmt>) {
                if (!path_exprs_equal(av.source, bv.source)) return false;
                if (!path_exprs_equal(av.destination, bv.destination)) {
                    return false;
                }
                if (av.verbatim != bv.verbatim) return false;
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                if (av.name != bv.name) return false;
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            } else {
                static_assert(std::is_same_v<T, RunStmt>);
                if (!ptr_expr_equal(av.command, bv.command)) return false;
                if (!optional_path_expr_equal(av.cwd, bv.cwd)) return false;
                if (!optional_expr_equal(av.when_clause, bv.when_clause)) {
                    return false;
                }
                return av.line == bv.line && av.column == bv.column;
            }
        },
        a.data);
}

bool programs_equal(const Program& a, const Program& b) {
    if (a.statements.size() != b.statements.size()) return false;
    for (std::size_t i = 0; i < a.statements.size(); ++i) {
        if (!a.statements[i] || !b.statements[i]) {
            if (a.statements[i] || b.statements[i]) return false;
            continue;
        }
        if (!stmts_equal(*a.statements[i], *b.statements[i])) return false;
    }
    return true;
}

}  // namespace spudplate::test
