#include "spudplate/validator.h"

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace spudplate {

namespace {

ExprPtr make_expr(ExprData data) {
    auto expr = std::make_unique<Expr>();
    expr->data = std::move(data);
    return expr;
}

bool is_bool_id(const Expr& e, const TypeMap& tm) {
    if (const auto* id = std::get_if<IdentifierExpr>(&e.data); id != nullptr) {
        auto it = tm.find(id->name);
        return it != tm.end() && it->second == VarType::Bool;
    }
    return false;
}

// Apply a single top-level normalization rule. Sets `fired` if a rule matched.
// Children are assumed already normalized.
ExprPtr apply_rule_once(const Expr& expr, const TypeMap& tm, bool& fired) {
    // Rule: `not not X` -> X
    if (const auto* un = std::get_if<UnaryExpr>(&expr.data);
        un != nullptr && un->op == TokenType::NOT) {
        if (const auto* inner = std::get_if<UnaryExpr>(&un->operand->data);
            inner != nullptr && inner->op == TokenType::NOT) {
            fired = true;
            return clone_expr(*inner->operand);
        }
    }

    // Rules: `x == true` / `x == false` / `x != true` / `x != false` where x is bool.
    // Symmetric: `true == x` etc. are also recognised.
    if (const auto* bin = std::get_if<BinaryExpr>(&expr.data);
        bin != nullptr &&
        (bin->op == TokenType::EQUALS || bin->op == TokenType::NOT_EQUALS)) {
        const Expr* id_side = nullptr;
        const BoolLiteralExpr* lit = nullptr;
        if (is_bool_id(*bin->left, tm)) {
            if (const auto* l = std::get_if<BoolLiteralExpr>(&bin->right->data);
                l != nullptr) {
                id_side = bin->left.get();
                lit = l;
            }
        }
        if (id_side == nullptr && is_bool_id(*bin->right, tm)) {
            if (const auto* l = std::get_if<BoolLiteralExpr>(&bin->left->data);
                l != nullptr) {
                id_side = bin->right.get();
                lit = l;
            }
        }
        if (id_side != nullptr && lit != nullptr) {
            const bool positive = (bin->op == TokenType::EQUALS && lit->value) ||
                                  (bin->op == TokenType::NOT_EQUALS && !lit->value);
            fired = true;
            if (positive) {
                return clone_expr(*id_side);
            }
            return make_expr(UnaryExpr{.op = TokenType::NOT,
                                       .operand = clone_expr(*id_side),
                                       .line = bin->line,
                                       .column = bin->column});
        }
    }

    return clone_expr(expr);
}

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

// AliasCtx carries state that accumulates as the walker moves through the
// program: the alias registry (populated for mkdir/file aliases declared
// outside any repeat, Part E) and the type map used to normalize when clauses
// (Part D).
struct AliasCtx {
    std::unordered_map<std::string, std::optional<ExprPtr>> registry;
    TypeMap type_map;
};

void walk_expr(const Expr& expr, const Scope& scope);
void walk_path(const PathExpr& path, const Scope& scope, const AliasCtx& ctx,
               const std::optional<ExprPtr>& current_when);
void validate_stmt(const Stmt& stmt, Scope& scope, AliasCtx& ctx);

void check_reference(const std::string& name, int line, int column,
                     const Scope& scope) {
    if (scope.out_of_scope(name)) {
        throw SemanticError("reference to out-of-scope name '" + name + "'", line,
                            column);
    }
}

void check_alias(const PathVar& pv, const std::optional<ExprPtr>& current_when,
                 const AliasCtx& ctx) {
    auto it = ctx.registry.find(pv.name);
    if (it == ctx.registry.end()) {
        return;  // Not a registered alias (for example one rejected by Part C).
    }
    const auto& stored = it->second;
    if (!stored.has_value()) {
        return;  // Unconditional binding — references are unrestricted.
    }
    if (!current_when.has_value()) {
        throw SemanticError("alias '" + pv.name +
                                "' is conditional; reference requires a matching when clause",
                            pv.line, pv.column);
    }
    auto normalized_current = normalize(**current_when, ctx.type_map);
    if (!exprs_equal(**stored, *normalized_current)) {
        throw SemanticError(
            "alias '" + pv.name + "' referenced under a different condition than its binding",
            pv.line, pv.column);
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
                for (const auto& arg : e.arguments) {
                    walk_expr(*arg, scope);
                }
            }
            // String, Integer, Bool literals have no children.
        },
        expr.data);
}

void walk_path(const PathExpr& path, const Scope& scope, const AliasCtx& ctx,
               const std::optional<ExprPtr>& current_when) {
    for (const auto& seg : path.segments) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, PathVar>) {
                    check_reference(s.name, s.line, s.column, scope);
                    check_alias(s, current_when, ctx);
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

// Called by mkdir/file to record an alias binding in the registry if the
// binding is outside any repeat body.
void register_alias_binding(const std::string& name,
                            const std::optional<ExprPtr>& when_clause,
                            const Scope& scope, AliasCtx& ctx) {
    if (scope.inside_repeat()) {
        return;  // Part C already rejects out-of-repeat references to these.
    }
    std::optional<ExprPtr> stored;
    if (when_clause.has_value()) {
        stored = normalize(**when_clause, ctx.type_map);
    }
    ctx.registry[name] = std::move(stored);
}

void validate_stmt(const Stmt& stmt, Scope& scope, AliasCtx& ctx) {
    std::visit(
        [&](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, AskStmt>) {
                if (s.default_value.has_value()) {
                    walk_expr(**s.default_value, scope);
                }
                for (const auto& opt : s.options) {
                    walk_expr(*opt, scope);
                }
                walk_optional_expr(s.when_clause, scope);
                check_shadowing(s.name, s.line, s.column, scope);
                scope.declare(s.name);
                ctx.type_map[s.name] = s.var_type;
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                walk_expr(*s.value, scope);
                check_shadowing(s.name, s.line, s.column, scope);
                scope.declare(s.name);
            } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                walk_path(s.path, scope, ctx, s.when_clause);
                if (s.from_source.has_value()) {
                    walk_path(*s.from_source, scope, ctx, s.when_clause);
                }
                walk_optional_expr(s.when_clause, scope);
                if (s.alias.has_value()) {
                    check_shadowing(*s.alias, s.line, s.column, scope);
                    scope.declare(*s.alias);
                    register_alias_binding(*s.alias, s.when_clause, scope, ctx);
                }
            } else if constexpr (std::is_same_v<T, FileStmt>) {
                walk_path(s.path, scope, ctx, s.when_clause);
                std::visit(
                    [&](const auto& src) {
                        using ST = std::decay_t<decltype(src)>;
                        if constexpr (std::is_same_v<ST, FileFromSource>) {
                            walk_path(src.path, scope, ctx, s.when_clause);
                        } else if constexpr (std::is_same_v<ST, FileContentSource>) {
                            walk_expr(*src.value, scope);
                        }
                    },
                    s.source);
                walk_optional_expr(s.when_clause, scope);
                if (s.alias.has_value()) {
                    check_shadowing(*s.alias, s.line, s.column, scope);
                    scope.declare(*s.alias);
                    register_alias_binding(*s.alias, s.when_clause, scope, ctx);
                }
            } else if constexpr (std::is_same_v<T, CopyStmt>) {
                walk_path(s.source, scope, ctx, s.when_clause);
                walk_path(s.destination, scope, ctx, s.when_clause);
                walk_optional_expr(s.when_clause, scope);
            } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                walk_optional_expr(s.when_clause, scope);
            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                check_reference(s.collection_var, s.line, s.column, scope);
                walk_optional_expr(s.when_clause, scope);
                check_shadowing(s.iterator_var, s.line, s.column, scope);
                scope.push();
                scope.declare(s.iterator_var);
                for (const auto& inner : s.body) {
                    validate_stmt(*inner, scope, ctx);
                }
                scope.pop();
            }
        },
        stmt.data);
}

}  // namespace

ExprPtr clone_expr(const Expr& expr) {
    return std::visit(
        [](const auto& n) -> ExprPtr {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                return make_expr(StringLiteralExpr{
                    .value = n.value, .line = n.line, .column = n.column});
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                return make_expr(IntegerLiteralExpr{
                    .value = n.value, .line = n.line, .column = n.column});
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                return make_expr(BoolLiteralExpr{
                    .value = n.value, .line = n.line, .column = n.column});
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                return make_expr(
                    IdentifierExpr{.name = n.name, .line = n.line, .column = n.column});
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return make_expr(UnaryExpr{.op = n.op,
                                           .operand = clone_expr(*n.operand),
                                           .line = n.line,
                                           .column = n.column});
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return make_expr(BinaryExpr{.op = n.op,
                                            .left = clone_expr(*n.left),
                                            .right = clone_expr(*n.right),
                                            .line = n.line,
                                            .column = n.column});
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                std::vector<ExprPtr> cloned_args;
                cloned_args.reserve(n.arguments.size());
                for (const auto& a : n.arguments) {
                    cloned_args.push_back(clone_expr(*a));
                }
                return make_expr(FunctionCallExpr{.name = n.name,
                                                  .arguments = std::move(cloned_args),
                                                  .line = n.line,
                                                  .column = n.column});
            }
        },
        expr.data);
}

ExprPtr normalize(const Expr& expr, const TypeMap& tm) {
    // Step 1: deep-clone and normalize children bottom-up.
    ExprPtr out;
    std::visit(
        [&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, UnaryExpr>) {
                out = make_expr(UnaryExpr{.op = n.op,
                                          .operand = normalize(*n.operand, tm),
                                          .line = n.line,
                                          .column = n.column});
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                out = make_expr(BinaryExpr{.op = n.op,
                                           .left = normalize(*n.left, tm),
                                           .right = normalize(*n.right, tm),
                                           .line = n.line,
                                           .column = n.column});
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                std::vector<ExprPtr> normed_args;
                normed_args.reserve(n.arguments.size());
                for (const auto& a : n.arguments) {
                    normed_args.push_back(normalize(*a, tm));
                }
                out = make_expr(FunctionCallExpr{.name = n.name,
                                                 .arguments = std::move(normed_args),
                                                 .line = n.line,
                                                 .column = n.column});
            } else {
                out = clone_expr(expr);
            }
        },
        expr.data);

    // Step 2: apply top-level rules to fixpoint.
    while (true) {
        bool fired = false;
        out = apply_rule_once(*out, tm, fired);
        if (!fired) {
            break;
        }
    }
    return out;
}

bool exprs_equal(const Expr& a, const Expr& b) {
    if (a.data.index() != b.data.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& ax) -> bool {
            using T = std::decay_t<decltype(ax)>;
            const auto& bx = std::get<T>(b.data);
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                return ax.value == bx.value;
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                return ax.value == bx.value;
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                return ax.value == bx.value;
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                return ax.name == bx.name;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return ax.op == bx.op && exprs_equal(*ax.operand, *bx.operand);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return ax.op == bx.op && exprs_equal(*ax.left, *bx.left) &&
                       exprs_equal(*ax.right, *bx.right);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                if (ax.name != bx.name ||
                    ax.arguments.size() != bx.arguments.size()) {
                    return false;
                }
                for (size_t i = 0; i < ax.arguments.size(); ++i) {
                    if (!exprs_equal(*ax.arguments[i], *bx.arguments[i])) {
                        return false;
                    }
                }
                return true;
            }
        },
        a.data);
}

void validate(const Program& program) {
    Scope scope;
    scope.push();  // program-level frame, never popped.
    AliasCtx ctx;
    for (const auto& stmt : program.statements) {
        validate_stmt(*stmt, scope, ctx);
    }
}

}  // namespace spudplate
