#include "spudplate/interpreter.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "spudplate/ast.h"
#include "spudplate/token.h"

namespace spudplate {

void Environment::declare(const std::string& name, Value value) {
    if (lookup(name).has_value()) {
        throw std::logic_error("environment already has a binding for '" + name +
                               "'");
    }
    frames_.back().emplace(name, std::move(value));
}

std::optional<Value> Environment::lookup(const std::string& name) const {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

namespace {

// Deferred-write queue entries. The interpreter never touches the filesystem
// during statement execution; mkdir and file statements push one of these and
// the flush step at the end of a run executes them in order.
struct PendingMkdir {
    std::string path;
    std::optional<int> mode;
    int line;
    int column;
};

struct PendingFile {
    std::string path;
    std::string content;
    bool append;
    std::optional<int> mode;
    int line;
    int column;
};

using PendingOp = std::variant<PendingMkdir, PendingFile>;

// Until Part 4 lands, the Prompter forward-declared in the header has no
// concrete implementation. The skeleton interpreter never reaches a code
// path that calls it (every statement throws "not yet supported" first).
void unsupported(const std::string& stmt_name, int line, int column) {
    throw RuntimeError("statement '" + stmt_name +
                           "' not yet supported in this build",
                       line, column);
}

// Names a Value's variant alternative for error messages.
const char* type_name(const Value& v) {
    if (std::holds_alternative<std::string>(v)) {
        return "string";
    }
    if (std::holds_alternative<std::int64_t>(v)) {
        return "int";
    }
    return "bool";
}

[[noreturn]] void type_error(const std::string& msg, int line, int column) {
    throw RuntimeError(msg, line, column);
}

// Wrap-around helpers — signed overflow is UB in C++, so route arithmetic
// through unsigned and cast the bit pattern back.
std::int64_t wrap_add(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) +
                                     static_cast<std::uint64_t>(b));
}
std::int64_t wrap_sub(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) -
                                     static_cast<std::uint64_t>(b));
}
std::int64_t wrap_mul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) *
                                     static_cast<std::uint64_t>(b));
}

Value eval_unary(const UnaryExpr& un, const Environment& env) {
    Value operand = evaluate_expr(*un.operand, env);
    if (un.op != TokenType::NOT) {
        type_error("unsupported unary operator", un.line, un.column);
    }
    if (!std::holds_alternative<bool>(operand)) {
        type_error(std::string{"'not' requires bool, got "} + type_name(operand),
                   un.line, un.column);
    }
    return Value{!std::get<bool>(operand)};
}

bool int_compare(TokenType op, std::int64_t a, std::int64_t b) {
    switch (op) {
        case TokenType::GREATER:
            return a > b;
        case TokenType::LESS:
            return a < b;
        case TokenType::GREATER_EQUAL:
            return a >= b;
        case TokenType::LESS_EQUAL:
            return a <= b;
        default:
            return false;  // unreachable — caller restricts the op set
    }
}

Value eval_binary(const BinaryExpr& bin, const Environment& env) {
    // Short-circuit logical ops handled before evaluating the right side.
    if (bin.op == TokenType::AND || bin.op == TokenType::OR) {
        Value left = evaluate_expr(*bin.left, env);
        if (!std::holds_alternative<bool>(left)) {
            type_error(std::string{"logical operator requires bool, got "} +
                           type_name(left),
                       bin.line, bin.column);
        }
        bool lv = std::get<bool>(left);
        if (bin.op == TokenType::AND && !lv) {
            return Value{false};
        }
        if (bin.op == TokenType::OR && lv) {
            return Value{true};
        }
        Value right = evaluate_expr(*bin.right, env);
        if (!std::holds_alternative<bool>(right)) {
            type_error(std::string{"logical operator requires bool, got "} +
                           type_name(right),
                       bin.line, bin.column);
        }
        return Value{std::get<bool>(right)};
    }

    Value left = evaluate_expr(*bin.left, env);
    Value right = evaluate_expr(*bin.right, env);

    // Equality and inequality accept any operand types per docs/syntax.md.
    if (bin.op == TokenType::EQUALS || bin.op == TokenType::NOT_EQUALS) {
        bool eq = (left.index() == right.index()) && (left == right);
        return Value{bin.op == TokenType::EQUALS ? eq : !eq};
    }

    // Ordering comparisons require ints.
    if (bin.op == TokenType::GREATER || bin.op == TokenType::LESS ||
        bin.op == TokenType::GREATER_EQUAL || bin.op == TokenType::LESS_EQUAL) {
        if (!std::holds_alternative<std::int64_t>(left) ||
            !std::holds_alternative<std::int64_t>(right)) {
            type_error("ordering comparison requires int operands", bin.line,
                       bin.column);
        }
        return Value{int_compare(bin.op, std::get<std::int64_t>(left),
                                 std::get<std::int64_t>(right))};
    }

    // Arithmetic — `+` accepts both int+int and string+string; the others
    // are int-only.
    if (bin.op == TokenType::PLUS && std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
        return Value{std::get<std::string>(left) + std::get<std::string>(right)};
    }
    if (!std::holds_alternative<std::int64_t>(left) ||
        !std::holds_alternative<std::int64_t>(right)) {
        type_error("arithmetic requires matching int (or string for '+') operands",
                   bin.line, bin.column);
    }
    auto a = std::get<std::int64_t>(left);
    auto b = std::get<std::int64_t>(right);
    switch (bin.op) {
        case TokenType::PLUS:
            return Value{wrap_add(a, b)};
        case TokenType::MINUS:
            return Value{wrap_sub(a, b)};
        case TokenType::STAR:
            return Value{wrap_mul(a, b)};
        case TokenType::SLASH:
            if (b == 0) {
                type_error("division by zero", bin.line, bin.column);
            }
            if (a == INT64_MIN && b == -1) {
                type_error("integer overflow in division", bin.line, bin.column);
            }
            return Value{a / b};
        default:
            type_error("unsupported binary operator", bin.line, bin.column);
    }
}

Value eval_call(const FunctionCallExpr& fc, const Environment& env) {
    Value arg = evaluate_expr(*fc.argument, env);
    if (!std::holds_alternative<std::string>(arg)) {
        type_error(std::string{"function '"} + fc.name +
                       "' requires string argument, got " + type_name(arg),
                   fc.line, fc.column);
    }
    const std::string& s = std::get<std::string>(arg);
    if (fc.name == "lower") {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            out.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        }
        return Value{out};
    }
    if (fc.name == "upper") {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            out.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(c))));
        }
        return Value{out};
    }
    if (fc.name == "trim") {
        auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) {
                       return std::isspace(c) != 0;
                   }).base();
        return Value{(start < end) ? std::string{start, end} : std::string{}};
    }
    type_error("unknown function '" + fc.name + "'", fc.line, fc.column);
}

class Interpreter {
  public:
    void execute(const Stmt& stmt) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, AskStmt>) {
                    unsupported("ask", s.line, s.column);
                } else if constexpr (std::is_same_v<T, LetStmt>) {
                    unsupported("let", s.line, s.column);
                } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                    unsupported("mkdir", s.line, s.column);
                } else if constexpr (std::is_same_v<T, FileStmt>) {
                    unsupported("file", s.line, s.column);
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    unsupported("repeat", s.line, s.column);
                } else if constexpr (std::is_same_v<T, CopyStmt>) {
                    unsupported("copy", s.line, s.column);
                } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                    unsupported("include", s.line, s.column);
                }
            },
            stmt.data);
    }

    [[nodiscard]] Environment& env() { return env_; }

  private:
    Environment env_;
    std::vector<PendingOp> pending_;
};

void run_program(const Program& program, Prompter& /*prompter*/,
                 Interpreter& interp) {
    for (const auto& stmt : program.statements) {
        interp.execute(*stmt);
    }
}

}  // namespace

Value evaluate_expr(const Expr& expr, const Environment& env) {
    return std::visit(
        [&](const auto& e) -> Value {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                return Value{e.value};
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                return Value{static_cast<std::int64_t>(e.value)};
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                return Value{e.value};
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                auto v = env.lookup(e.name);
                if (!v.has_value()) {
                    throw RuntimeError("undefined variable '" + e.name + "'",
                                       e.line, e.column);
                }
                return *v;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return eval_unary(e, env);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return eval_binary(e, env);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                return eval_call(e, env);
            }
        },
        expr.data);
}

std::string evaluate_path(const PathExpr& path, const Environment& env,
                          const AliasMap& aliases) {
    std::string out;
    for (const auto& seg : path.segments) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, PathLiteral>) {
                    out += s.value;
                } else if constexpr (std::is_same_v<T, PathVar>) {
                    auto it = aliases.find(s.name);
                    if (it == aliases.end()) {
                        throw RuntimeError(
                            "internal error: unbound path alias '" + s.name + "'",
                            s.line, s.column);
                    }
                    out += it->second;
                } else if constexpr (std::is_same_v<T, PathInterp>) {
                    out += value_to_string(evaluate_expr(*s.expression, env));
                }
            },
            seg);
    }
    return out;
}

std::string value_to_string(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else {
                return v ? "true" : "false";
            }
        },
        value);
}

void run(const Program& program, Prompter& prompter) {
    Interpreter interp;
    run_program(program, prompter, interp);
}

Environment run_for_tests(const Program& program, Prompter& prompter) {
    Interpreter interp;
    run_program(program, prompter, interp);
    return std::move(interp.env());
}

}  // namespace spudplate
