#include "spudplate/interpreter.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

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

void Environment::assign(const std::string& name, Value value) {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            found->second = std::move(value);
            return;
        }
    }
    throw std::logic_error("environment has no binding for '" + name +
                           "' to assign to");
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

// Verifies a path exists as a directory at flush time. Used by `copy` to
// enforce its "destination must already exist" rule, where "exist" means
// either pre-run or queued by an earlier `mkdir`/`mkdir from` op.
struct PendingCheckDir {
    std::string path;
    int line;
    int column;
};

// A `run` statement queued for flush-time execution. The command stored
// here is the *evaluated* expression result — what /bin/sh -c will run.
// `cwd` is the optional resolved working directory from the `in <path>`
// clause. The user authorised the literal source at the start of the run,
// before any expression evaluated. Source-vs-resolved drift (the shell-
// injection surface for `run "git clone " + url` patterns) is documented
// as a known caveat rather than mitigated at this layer.
struct PendingRun {
    std::string command;
    std::optional<std::string> cwd;
    int line;
    int column;
};

using PendingOp =
    std::variant<PendingMkdir, PendingFile, PendingCheckDir, PendingRun>;

// Read a regular file (cwd-relative) into a string. The position arguments
// point at the *.spud* statement that triggered the read so error messages
// surface there rather than at some unhelpful internal site.
std::string read_source_file(const std::string& path, int line, int column) {
    std::error_code ec;
    auto status = std::filesystem::status(path, ec);
    if (ec) {
        throw RuntimeError("cannot stat source '" + path + "': " + ec.message(),
                           line, column);
    }
    if (!std::filesystem::exists(status)) {
        throw RuntimeError("source file '" + path + "' does not exist", line,
                           column);
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw RuntimeError("source '" + path + "' is not a regular file", line,
                           column);
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw RuntimeError("cannot open source '" + path + "' for reading", line,
                           column);
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Substitute `{ident}` occurrences in `content` with the stringified value
// of `ident` looked up in `env`. The grammar is deliberately narrow for v1
// — only bare identifiers are accepted; arbitrary expressions are not. Use
// `verbatim` to copy file contents byte-for-byte without substitution.
std::string interpolate_content(const std::string& content,
                                const Environment& env, int line, int column) {
    std::string out;
    out.reserve(content.size());
    std::size_t i = 0;
    while (i < content.size()) {
        char c = content[i];
        if (c != '{') {
            out.push_back(c);
            ++i;
            continue;
        }
        std::size_t close = content.find('}', i + 1);
        if (close == std::string::npos) {
            throw RuntimeError(
                "unclosed '{' in source content; use 'verbatim' to suppress "
                "interpolation",
                line, column);
        }
        std::string name = content.substr(i + 1, close - i - 1);
        if (name.empty()) {
            throw RuntimeError("empty '{}' in source content", line, column);
        }
        auto is_ident_char = [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) != 0 ||
                   ch == '_';
        };
        bool head_ok = !name.empty() &&
                       (std::isalpha(static_cast<unsigned char>(name[0])) != 0 ||
                        name[0] == '_');
        bool body_ok = std::all_of(name.begin(), name.end(), is_ident_char);
        if (!head_ok || !body_ok) {
            throw RuntimeError(
                "source content interpolation only supports bare identifiers; "
                "got '{" +
                    name + "}' (use 'verbatim' to copy braces literally)",
                line, column);
        }
        auto v = env.lookup(name);
        if (!v.has_value()) {
            throw RuntimeError(
                "undefined variable '" + name + "' in source content", line,
                column);
        }
        out += value_to_string(*v);
        i = close + 1;
    }
    return out;
}

// Lower-case ASCII copy used for case-insensitive bool parsing.
std::string ascii_lower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Evaluate an optional `when` clause; returns true if it passes (clause
// absent or evaluates to true). Throws if the clause's value is non-bool.
bool when_passes(const std::optional<ExprPtr>& clause, const Environment& env) {
    if (!clause.has_value()) {
        return true;
    }
    Value v = evaluate_expr(**clause, env);
    if (!std::holds_alternative<bool>(v)) {
        const auto& expr = **clause;
        int line = 0;
        int col = 0;
        std::visit(
            [&](const auto& e) {
                line = e.line;
                col = e.column;
            },
            expr.data);
        throw RuntimeError("'when' condition must be bool", line, col);
    }
    return std::get<bool>(v);
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

// Parse a raw answer string into a Value of the requested type.
// Returns nullopt when the input is invalid (caller re-prompts).
std::optional<Value> parse_answer(const std::string& raw, VarType type) {
    if (type == VarType::String) {
        return Value{raw};
    }
    if (type == VarType::Bool) {
        std::string lc = ascii_lower(raw);
        if (lc == "true" || lc == "yes" || lc == "y") {
            return Value{true};
        }
        if (lc == "false" || lc == "no" || lc == "n") {
            return Value{false};
        }
        return std::nullopt;
    }
    // VarType::Int
    try {
        std::size_t consumed = 0;
        long long v = std::stoll(raw, &consumed);
        // Reject if any non-whitespace remains after the number.
        for (std::size_t i = consumed; i < raw.size(); ++i) {
            if (std::isspace(static_cast<unsigned char>(raw[i])) == 0) {
                return std::nullopt;
            }
        }
        return Value{static_cast<std::int64_t>(v)};
    } catch (...) {
        return std::nullopt;
    }
}

// If raw parses as a 1-based index into options, return the corresponding
// option string. Otherwise return raw unchanged.
std::string apply_option_index(const std::string& raw,
                               const std::vector<std::string>& options) {
    if (options.empty() || raw.empty()) {
        return raw;
    }
    for (char c : raw) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return raw;
        }
    }
    try {
        std::size_t idx = std::stoul(raw);
        if (idx >= 1 && idx <= options.size()) {
            return options[idx - 1];
        }
    } catch (...) {
    }
    return raw;
}

const char* expected_message(VarType type) {
    switch (type) {
        case VarType::Bool:
            return "expected yes or no";
        case VarType::Int:
            return "expected an integer";
        case VarType::String:
            return "invalid input";
    }
    return "invalid input";
}

// Render an expression as a readable approximation of its source text. Used
// only by the trust prompt — users see the *structure* of each `run`
// command before authorising, even though the actual value executed at
// flush time may differ (interpolated identifiers, computed strings, etc).
std::string preview_expr(const Expr& expr) {
    return std::visit(
        [](const auto& e) -> std::string {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                return "\"" + e.value + "\"";
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                return std::to_string(e.value);
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                return e.value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                return e.name;
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return "not " + preview_expr(*e.operand);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                const char* sym = "?";
                switch (e.op) {
                    case TokenType::PLUS: sym = "+"; break;
                    case TokenType::MINUS: sym = "-"; break;
                    case TokenType::STAR: sym = "*"; break;
                    case TokenType::SLASH: sym = "/"; break;
                    case TokenType::EQUALS: sym = "=="; break;
                    case TokenType::NOT_EQUALS: sym = "!="; break;
                    case TokenType::GREATER: sym = ">"; break;
                    case TokenType::LESS: sym = "<"; break;
                    case TokenType::GREATER_EQUAL: sym = ">="; break;
                    case TokenType::LESS_EQUAL: sym = "<="; break;
                    case TokenType::AND: sym = "and"; break;
                    case TokenType::OR: sym = "or"; break;
                    default: break;
                }
                return preview_expr(*e.left) + " " + sym + " " +
                       preview_expr(*e.right);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                std::string out = e.name + "(";
                for (std::size_t i = 0; i < e.arguments.size(); ++i) {
                    if (i != 0) {
                        out += ", ";
                    }
                    out += preview_expr(*e.arguments[i]);
                }
                out += ")";
                return out;
            } else if constexpr (std::is_same_v<T, TemplateStringExpr>) {
                std::string out = "\"";
                for (const auto& p : e.parts) {
                    if (std::holds_alternative<std::string>(p)) {
                        out += std::get<std::string>(p);
                    } else {
                        out += "{" +
                               preview_expr(*std::get<ExprPtr>(p)) + "}";
                    }
                }
                out += "\"";
                return out;
            }
            return "<unknown>";
        },
        expr.data);
}

// Walk every RunStmt in the program (top-level and inside repeat bodies)
// and append a one-line preview of each command's source-form expression
// to `out`. Repeat-internal commands are tagged so the user knows they
// may run multiple times — or not at all.
// Render a path expression as a readable approximation of its source. Used
// by the trust prompt to surface the `in <path>` clause on `run` statements.
std::string preview_path(const PathExpr& path) {
    std::string out;
    for (const auto& seg : path.segments) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, PathLiteral>) {
                    out += s.value;
                } else if constexpr (std::is_same_v<T, PathVar>) {
                    out += s.name;
                } else if constexpr (std::is_same_v<T, PathInterp>) {
                    out += "{" + preview_expr(*s.expression) + "}";
                }
            },
            seg);
    }
    return out;
}

void collect_run_previews(const std::vector<StmtPtr>& body,
                          std::vector<std::string>& out, bool inside_repeat) {
    for (const auto& stmt : body) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, RunStmt>) {
                    std::string line = preview_expr(*s.command);
                    if (s.cwd.has_value()) {
                        line += " in " + preview_path(*s.cwd);
                    }
                    if (inside_repeat) {
                        line += "  (inside repeat — may run 0 or many times)";
                    }
                    if (s.when_clause.has_value()) {
                        line += "  (conditional)";
                    }
                    out.push_back(std::move(line));
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    collect_run_previews(s.body, out, /*inside_repeat=*/true);
                }
            },
            stmt->data);
    }
}

std::string build_authorize_summary(const Program& program) {
    std::vector<std::string> previews;
    collect_run_previews(program.statements, previews, /*inside_repeat=*/false);
    if (previews.empty()) {
        return {};
    }
    std::string summary =
        "This template will execute the following shell command";
    summary += previews.size() == 1 ? "" : "s";
    summary += " via /bin/sh:\n";
    for (std::size_t i = 0; i < previews.size(); ++i) {
        summary += "  " + std::to_string(i + 1) + ". " + previews[i] + "\n";
    }
    summary +=
        "\nValues interpolated from `ask`/`let` are not shown above and "
        "execute verbatim.\n";
    return summary;
}

void execute_ask(const AskStmt& stmt, Environment& env, Prompter& prompter,
                 int question_index, int question_total, int indent_level) {
    if (!when_passes(stmt.when_clause, env)) {
        return;
    }

    std::optional<Value> default_value;
    if (stmt.default_value.has_value()) {
        default_value = evaluate_expr(**stmt.default_value, env);
    }

    std::vector<std::string> option_strings;
    option_strings.reserve(stmt.options.size());
    for (const auto& opt : stmt.options) {
        option_strings.push_back(value_to_string(evaluate_expr(*opt, env)));
    }

    PromptRequest req{
        .text = stmt.prompt,
        .type = stmt.var_type,
        .options = option_strings,
        .default_value =
            default_value.has_value()
                ? std::optional<std::string>{value_to_string(*default_value)}
                : std::nullopt,
        .previous_error = std::nullopt,
        .question_index = question_index,
        .question_total = question_total,
        .indent_level = indent_level,
    };

    while (true) {
        std::string raw = prompter.prompt(req);
        if (raw.empty()) {
            if (default_value.has_value()) {
                env.declare(stmt.name, *default_value);
                return;
            }
            req.previous_error = "this question is required";
            continue;
        }
        std::string mapped = apply_option_index(raw, option_strings);
        auto parsed = parse_answer(mapped, stmt.var_type);
        if (!parsed.has_value()) {
            req.previous_error = expected_message(stmt.var_type);
            continue;
        }
        if (!option_strings.empty()) {
            std::string parsed_str = value_to_string(*parsed);
            bool match = false;
            for (const auto& s : option_strings) {
                if (s == parsed_str) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                req.previous_error = "not one of the listed options";
                continue;
            }
        }
        env.declare(stmt.name, std::move(*parsed));
        return;
    }
}

Value eval_call(const FunctionCallExpr& fc, const Environment& env) {
    auto require_arity = [&](size_t expected) {
        if (fc.arguments.size() != expected) {
            type_error("function '" + fc.name + "' takes " +
                           std::to_string(expected) + " argument(s), got " +
                           std::to_string(fc.arguments.size()),
                       fc.line, fc.column);
        }
    };

    auto eval_string_arg = [&](const Expr& e, size_t idx) -> std::string {
        Value v = evaluate_expr(e, env);
        if (!std::holds_alternative<std::string>(v)) {
            type_error("function '" + fc.name + "' argument " +
                           std::to_string(idx + 1) +
                           " must be string, got " + type_name(v),
                       fc.line, fc.column);
        }
        return std::get<std::string>(std::move(v));
    };

    if (fc.name == "lower") {
        require_arity(1);
        std::string s = eval_string_arg(*fc.arguments[0], 0);
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            out.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        }
        return Value{out};
    }
    if (fc.name == "upper") {
        require_arity(1);
        std::string s = eval_string_arg(*fc.arguments[0], 0);
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            out.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(c))));
        }
        return Value{out};
    }
    if (fc.name == "trim") {
        require_arity(1);
        std::string s = eval_string_arg(*fc.arguments[0], 0);
        auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) {
                       return std::isspace(c) != 0;
                   }).base();
        return Value{(start < end) ? std::string{start, end} : std::string{}};
    }
    if (fc.name == "replace") {
        require_arity(3);
        std::string s = eval_string_arg(*fc.arguments[0], 0);
        std::string from = eval_string_arg(*fc.arguments[1], 1);
        std::string to = eval_string_arg(*fc.arguments[2], 2);
        if (from.empty()) {
            type_error("function 'replace' search string must be non-empty",
                       fc.line, fc.column);
        }
        std::string out;
        out.reserve(s.size());
        size_t pos = 0;
        while (pos < s.size()) {
            size_t found = s.find(from, pos);
            if (found == std::string::npos) {
                out.append(s, pos, std::string::npos);
                break;
            }
            out.append(s, pos, found - pos);
            out.append(to);
            pos = found + from.size();
        }
        return Value{std::move(out)};
    }
    type_error("unknown function '" + fc.name + "'", fc.line, fc.column);
}

class Interpreter {
  public:
    explicit Interpreter(Prompter& prompter) : prompter_(prompter) {}

    void set_ask_total(int total) { ask_total_ = total; }

    void execute(const Stmt& stmt) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, AskStmt>) {
                    bool show_counter = ask_total_ > 0 && repeat_depth_ == 0;
                    int index = show_counter ? ++ask_index_ : 0;
                    int total = show_counter ? ask_total_ : 0;
                    execute_ask(s, env_, prompter_, index, total,
                                repeat_depth_);
                } else if constexpr (std::is_same_v<T, LetStmt>) {
                    Value v = evaluate_expr(*s.value, env_);
                    env_.declare(s.name, std::move(v));
                } else if constexpr (std::is_same_v<T, AssignStmt>) {
                    Value v = evaluate_expr(*s.value, env_);
                    env_.assign(s.name, std::move(v));
                } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                    execute_mkdir(s);
                } else if constexpr (std::is_same_v<T, FileStmt>) {
                    execute_file(s);
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    execute_repeat(s);
                } else if constexpr (std::is_same_v<T, CopyStmt>) {
                    execute_copy(s);
                } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                    throw RuntimeError(
                        "statement 'include' not yet supported in this build",
                        s.line, s.column);
                } else if constexpr (std::is_same_v<T, RunStmt>) {
                    execute_run(s);
                }
            },
            stmt.data);
    }

    [[nodiscard]] Environment& env() { return env_; }
    [[nodiscard]] const std::vector<PendingOp>& pending() const {
        return pending_;
    }

    void flush() {
        for (const auto& op : pending_) {
            std::visit([&](const auto& o) { run_op(o); }, op);
        }
    }

  private:
    void execute_repeat(const RepeatStmt& s) {
        if (!when_passes(s.when_clause, env_)) {
            return;
        }
        auto count_v = env_.lookup(s.collection_var);
        if (!count_v.has_value()) {
            throw RuntimeError("undefined variable '" + s.collection_var + "'",
                               s.line, s.column);
        }
        if (!std::holds_alternative<std::int64_t>(*count_v)) {
            throw RuntimeError("'repeat' count must be int", s.line, s.column);
        }
        std::int64_t count = std::get<std::int64_t>(*count_v);
        if (count < 0) {
            return;  // matches n == 0 — empty loop, no throw
        }

        for (std::int64_t i = 0; i < count; ++i) {
            // Snapshot which alias names existed before the iteration so we
            // can drop anything the body adds. Aliases declared in the body
            // do not leak across iterations or out of the loop.
            std::unordered_set<std::string> outer_aliases;
            outer_aliases.reserve(alias_map_.size());
            for (const auto& kv : alias_map_) {
                outer_aliases.insert(kv.first);
            }

            env_.push();
            env_.declare(s.iterator_var, Value{i});
            ++repeat_depth_;
            try {
                for (const auto& stmt : s.body) {
                    execute(*stmt);
                }
            } catch (...) {
                // Restore scope before propagating; aliases stay restored too.
                for (auto it = alias_map_.begin(); it != alias_map_.end();) {
                    if (outer_aliases.find(it->first) == outer_aliases.end()) {
                        it = alias_map_.erase(it);
                    } else {
                        ++it;
                    }
                }
                --repeat_depth_;
                env_.pop();
                throw;
            }

            for (auto it = alias_map_.begin(); it != alias_map_.end();) {
                if (outer_aliases.find(it->first) == outer_aliases.end()) {
                    it = alias_map_.erase(it);
                } else {
                    ++it;
                }
            }
            --repeat_depth_;
            env_.pop();
        }
    }

    void execute_mkdir(const MkdirStmt& s) {
        if (!when_passes(s.when_clause, env_)) {
            return;
        }
        std::string dst = evaluate_path(s.path, env_, alias_map_);
        if (s.alias.has_value()) {
            alias_map_[*s.alias] = dst;
        }
        pending_.push_back(PendingMkdir{.path = dst,
                                        .mode = s.mode,
                                        .line = s.line,
                                        .column = s.column});
        if (s.from_source.has_value()) {
            std::string src = evaluate_path(*s.from_source, env_, alias_map_);
            walk_source_into_pending(src, dst, s.verbatim, s.line, s.column);
        }
    }

    void execute_run(const RunStmt& s) {
        if (!when_passes(s.when_clause, env_)) {
            return;
        }
        Value v = evaluate_expr(*s.command, env_);
        if (!std::holds_alternative<std::string>(v)) {
            throw RuntimeError("'run' command must evaluate to a string",
                               s.line, s.column);
        }
        std::optional<std::string> cwd;
        if (s.cwd.has_value()) {
            cwd = evaluate_path(*s.cwd, env_, alias_map_);
        }
        pending_.push_back(PendingRun{.command = std::get<std::string>(v),
                                      .cwd = std::move(cwd),
                                      .line = s.line,
                                      .column = s.column});
    }

    void execute_copy(const CopyStmt& s) {
        if (!when_passes(s.when_clause, env_)) {
            return;
        }
        std::string dst = evaluate_path(s.destination, env_, alias_map_);
        std::string src = evaluate_path(s.source, env_, alias_map_);
        pending_.push_back(
            PendingCheckDir{.path = dst, .line = s.line, .column = s.column});
        walk_source_into_pending(src, dst, s.verbatim, s.line, s.column);
    }

    // Walks a source directory and pushes pending mkdir/file ops mirroring
    // its tree under `dst_root`. recursive_directory_iterator visits parents
    // before children, so the queued ops naturally satisfy create-parent-
    // first ordering at flush time. Symlinks and other non-regular entries
    // are skipped — v1 has no defined behaviour for them.
    void walk_source_into_pending(const std::string& src,
                                  const std::string& dst_root, bool verbatim,
                                  int line, int column) {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto src_status = fs::status(src, ec);
        if (ec) {
            throw RuntimeError(
                "cannot stat source '" + src + "': " + ec.message(), line,
                column);
        }
        if (!fs::exists(src_status)) {
            throw RuntimeError("source directory '" + src + "' does not exist",
                               line, column);
        }
        if (!fs::is_directory(src_status)) {
            throw RuntimeError("source '" + src + "' is not a directory", line,
                               column);
        }
        fs::path src_path{src};
        fs::path dst_path{dst_root};
        for (auto it = fs::recursive_directory_iterator(src_path);
             it != fs::recursive_directory_iterator{}; ++it) {
            auto rel = fs::relative(it->path(), src_path);
            auto target = (dst_path / rel).string();
            if (it->is_directory()) {
                pending_.push_back(PendingMkdir{.path = std::move(target),
                                                .mode = std::nullopt,
                                                .line = line,
                                                .column = column});
            } else if (it->is_regular_file()) {
                std::string content =
                    read_source_file(it->path().string(), line, column);
                if (!verbatim) {
                    content = interpolate_content(content, env_, line, column);
                }
                pending_.push_back(PendingFile{.path = std::move(target),
                                               .content = std::move(content),
                                               .append = false,
                                               .mode = std::nullopt,
                                               .line = line,
                                               .column = column});
            }
            // symlinks, fifos, etc. are silently skipped for v1
        }
    }

    void check_pre_existing(const std::string& path, int line, int col) {
        // Snapshot is taken at flush time, not run start; valid only because
        // v1 does not write before flush. If a side-effecting statement is
        // added before flush (e.g. `run`), revisit.
        if (created_during_run_.find(path) == created_during_run_.end() &&
            std::filesystem::exists(path)) {
            throw RuntimeError("refusing to write to pre-existing path '" + path +
                                   "'",
                               line, col);
        }
    }

    void run_op(const PendingMkdir& op) {
        check_pre_existing(op.path, op.line, op.column);
        std::filesystem::create_directories(op.path);
        if (op.mode.has_value()) {
            std::filesystem::permissions(
                op.path, static_cast<std::filesystem::perms>(*op.mode),
                std::filesystem::perm_options::replace);
        }
        created_during_run_.insert(op.path);
    }

    void execute_file(const FileStmt& s) {
        if (!when_passes(s.when_clause, env_)) {
            return;
        }
        std::string content;
        if (std::holds_alternative<FileFromSource>(s.source)) {
            const auto& fs = std::get<FileFromSource>(s.source);
            std::string src_path = evaluate_path(fs.path, env_, alias_map_);
            content = read_source_file(src_path, s.line, s.column);
            if (!fs.verbatim) {
                content = interpolate_content(content, env_, s.line, s.column);
            }
        } else {
            const auto& content_src = std::get<FileContentSource>(s.source);
            content = value_to_string(evaluate_expr(*content_src.value, env_));
        }

        std::string path_str = evaluate_path(s.path, env_, alias_map_);
        if (s.alias.has_value()) {
            alias_map_[*s.alias] = path_str;
        }

        pending_.push_back(PendingFile{.path = std::move(path_str),
                                       .content = std::move(content),
                                       .append = s.append,
                                       .mode = s.mode,
                                       .line = s.line,
                                       .column = s.column});
    }

    void run_op(const PendingRun& op) const {
        // /bin/sh -c is what std::system invokes on POSIX. Output streams
        // through to stdout/stderr inherited from the parent. A non-zero
        // exit aborts the rest of the flush. If `op.cwd` is set, the chdir
        // is restored on every exit path via the destructor.
        struct ChdirScope {
            std::filesystem::path saved;
            bool active{false};
            ~ChdirScope() {
                if (active) {
                    std::error_code ec;
                    std::filesystem::current_path(saved, ec);
                }
            }
        };
        ChdirScope scope;
        if (op.cwd.has_value()) {
            std::error_code ec;
            scope.saved = std::filesystem::current_path(ec);
            if (ec) {
                throw RuntimeError(
                    "cannot read current directory: " + ec.message(), op.line,
                    op.column);
            }
            std::filesystem::current_path(*op.cwd, ec);
            if (ec) {
                throw RuntimeError("cannot chdir to '" + *op.cwd + "' for run: " +
                                       ec.message(),
                                   op.line, op.column);
            }
            scope.active = true;
        }
        int rc = std::system(op.command.c_str());
        if (rc == -1) {
            throw RuntimeError("failed to invoke shell for command '" +
                                   op.command + "': " +
                                   std::strerror(errno),
                               op.line, op.column);
        }
        if (WIFSIGNALED(rc)) {
            throw RuntimeError("command killed by signal " +
                                   std::to_string(WTERMSIG(rc)) + ": " +
                                   op.command,
                               op.line, op.column);
        }
        if (WIFEXITED(rc) && WEXITSTATUS(rc) != 0) {
            throw RuntimeError("command exited with status " +
                                   std::to_string(WEXITSTATUS(rc)) + ": " +
                                   op.command,
                               op.line, op.column);
        }
    }

    void run_op(const PendingCheckDir& op) const {
        if (!std::filesystem::is_directory(op.path)) {
            throw RuntimeError(
                "destination '" + op.path +
                    "' does not exist (use 'mkdir from' to create it)",
                op.line, op.column);
        }
    }

    void run_op(const PendingFile& op) {
        // For non-append, the path may have been written earlier in this run
        // (a prior `PendingFile` to the same path) — that's an in-run
        // overwrite, allowed. The pre-existing check only fires for paths
        // that existed before the run started.
        check_pre_existing(op.path, op.line, op.column);
        std::ios::openmode mode = std::ios::out;
        if (op.append) {
            mode |= std::ios::app;
        } else {
            mode |= std::ios::trunc;
        }
        {
            std::ofstream out(op.path, mode);
            if (!out) {
                throw RuntimeError("cannot open '" + op.path + "' for writing",
                                   op.line, op.column);
            }
            out << op.content;
        }
        if (op.mode.has_value()) {
            std::filesystem::permissions(
                op.path, static_cast<std::filesystem::perms>(*op.mode),
                std::filesystem::perm_options::replace);
        }
        created_during_run_.insert(op.path);
    }

    Environment env_;
    Prompter& prompter_;
    AliasMap alias_map_;
    std::vector<PendingOp> pending_;
    std::unordered_set<std::string> created_during_run_;
    int ask_total_{0};
    int ask_index_{0};
    int repeat_depth_{0};
};

int count_ask_statements(const Program& program) {
    int total = 0;
    for (const auto& stmt : program.statements) {
        if (std::holds_alternative<AskStmt>(stmt->data)) {
            ++total;
        }
    }
    return total;
}

void run_program(const Program& program, Interpreter& interp) {
    interp.set_ask_total(count_ask_statements(program));
    for (const auto& stmt : program.statements) {
        interp.execute(*stmt);
    }
    interp.flush();
}

}  // namespace

namespace {

bool detect_colour_support() {
    const char* nc = std::getenv("NO_COLOR");
    if (nc != nullptr && nc[0] != '\0') {
        return false;
    }
    return ::isatty(::fileno(stdout)) != 0;
}

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kDim = "\x1b[38;5;250m";  // light grey
constexpr const char* kRed = "\x1b[31m";
// Single source of truth for the accent colour used on counter, option
// numbers, and the input colon. Swap this one constant to retheme.
constexpr const char* kAccent = "\x1b[38;5;226m";  // pure bright yellow

std::string wrap(const std::string& s, const char* code, bool on) {
    if (!on) {
        return s;
    }
    return std::string{code} + s + kReset;
}

std::string bool_hint(const std::optional<std::string>& default_value) {
    if (!default_value.has_value()) {
        return "[y/n]";
    }
    if (*default_value == "true") {
        return "[Y/n]";
    }
    return "[y/N]";
}

void render_request(std::ostream& out, const PromptRequest& req,
                    bool use_colour) {
    std::string indent(static_cast<std::size_t>(req.indent_level) * 2, ' ');

    if (req.previous_error.has_value()) {
        out << indent
            << wrap("! " + *req.previous_error, kRed, use_colour) << '\n';
    }

    bool has_options = !req.options.empty();
    bool is_bool_inline = req.type == VarType::Bool && !has_options;
    std::string colon = wrap(":", kAccent, use_colour);

    out << indent;

    if (req.question_total > 0 && req.question_index > 0) {
        std::string counter = "(" + std::to_string(req.question_index) + "/" +
                              std::to_string(req.question_total) + ") ";
        out << wrap(counter, kAccent, use_colour);
    }

    out << wrap(req.text, kBold, use_colour);

    if (has_options) {
        out << '\n';
        for (std::size_t i = 0; i < req.options.size(); ++i) {
            std::string marker = "[" + std::to_string(i + 1) + "]";
            out << indent << "  " << wrap(marker, kAccent, use_colour) << ' '
                << req.options[i] << '\n';
        }
        out << indent;
        if (req.default_value.has_value()) {
            out << wrap("[" + *req.default_value + "]", kDim,
                        use_colour);
        }
        out << colon << ' ' << std::flush;
        return;
    }

    if (is_bool_inline) {
        out << ' '
            << wrap(bool_hint(req.default_value), kDim, use_colour);
    } else if (req.default_value.has_value()) {
        out << ' '
            << wrap("[" + *req.default_value + "]", kDim, use_colour);
    }

    out << colon << ' ' << std::flush;
}

}  // namespace

StdinPrompter::StdinPrompter()
    : in_(std::cin), out_(std::cout), use_colour_(detect_colour_support()) {}

StdinPrompter::StdinPrompter(std::istream& in, std::ostream& out, bool use_colour)
    : in_(in), out_(out), use_colour_(use_colour) {}

std::string StdinPrompter::prompt(const PromptRequest& req) {
    render_request(out_, req, use_colour_);
    std::string line;
    std::getline(in_, line);
    return line;
}

bool StdinPrompter::authorize(const std::string& summary) {
    out_ << '\n' << summary;
    out_ << "Authorise these commands? [y/N] " << std::flush;
    std::string line;
    std::getline(in_, line);
    if (line.empty()) {
        return false;
    }
    char first = static_cast<char>(
        std::tolower(static_cast<unsigned char>(line[0])));
    return first == 'y';
}

std::string ScriptedPrompter::prompt(const PromptRequest& req) {
    last_ = req;
    if (index_ >= answers_.size()) {
        throw std::logic_error("ScriptedPrompter exhausted");
    }
    return answers_[index_++];
}

bool ScriptedPrompter::authorize(const std::string& summary) {
    last_authorize_summary_ = summary;
    return authorize_response_;
}

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
            } else if constexpr (std::is_same_v<T, TemplateStringExpr>) {
                std::string out;
                for (const auto& p : e.parts) {
                    if (std::holds_alternative<std::string>(p)) {
                        out += std::get<std::string>(p);
                    } else {
                        out += value_to_string(
                            evaluate_expr(*std::get<ExprPtr>(p), env));
                    }
                }
                return Value{std::move(out)};
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

void run(const Program& program, Prompter& prompter, bool skip_authorization,
         const SourceProvider* source) {
    if (!skip_authorization) {
        std::string summary = build_authorize_summary(program);
        if (!summary.empty() && !prompter.authorize(summary)) {
            return;  // declined: clean exit, no side effects, no statements ran
        }
    }
    (void)source;  // wired in by the next commit
    Interpreter interp(prompter);
    run_program(program, interp);
}

Environment run_for_tests(const Program& program, Prompter& prompter,
                          const SourceProvider* source) {
    (void)source;
    Interpreter interp(prompter);
    run_program(program, interp);
    return std::move(interp.env());
}

namespace {

// In-memory tree built from the deferred-op queue. `std::map` keeps
// children sorted alphabetically so the rendered output is stable.
struct DryRunNode {
    bool is_dir{true};
    bool is_append{false};
    std::map<std::string, DryRunNode> children;
};

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(std::move(current));
    }
    return parts;
}

// Insert `path` into the tree. `is_dir` and `is_append` apply to the leaf;
// every intermediate component is implicitly a directory.
void insert_path(DryRunNode& root, const std::string& path, bool is_dir,
                 bool is_append) {
    auto parts = split_path(path);
    if (parts.empty()) {
        return;
    }
    DryRunNode* cursor = &root;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& name = parts[i];
        auto& child = cursor->children[name];
        if (i + 1 < parts.size()) {
            child.is_dir = true;
        } else {
            // A path that appears first as an intermediate (auto-dir) and
            // later as an explicit mkdir keeps is_dir=true. A path that
            // appears as a file overrides any prior is_dir guess.
            if (!is_dir) {
                child.is_dir = false;
                child.is_append = is_append;
            }
        }
        cursor = &child;
    }
}

struct TreeGlyphs {
    const char* tee;       // child with siblings below
    const char* corner;    // last child
    const char* vertical;  // continuation through ancestor with siblings below
    const char* gap;       // continuation through ancestor that was last
};

constexpr TreeGlyphs kUtf8Glyphs{
    .tee = "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ",   // ├──
    .corner = "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 ",  // └──
    .vertical = "\xe2\x94\x82   ",                       // │
    .gap = "    ",
};

constexpr TreeGlyphs kAsciiGlyphs{
    .tee = "|-- ",
    .corner = "\\-- ",
    .vertical = "|   ",
    .gap = "    ",
};

void render_node(std::ostream& out, const DryRunNode& node,
                 const std::string& prefix, const TreeGlyphs& glyphs) {
    std::size_t i = 0;
    const std::size_t n = node.children.size();
    for (const auto& [name, child] : node.children) {
        bool last = (i + 1 == n);
        out << prefix << (last ? glyphs.corner : glyphs.tee) << name;
        if (child.is_dir) {
            out << '/';
        } else if (child.is_append) {
            out << " (append)";
        }
        out << '\n';
        std::string next_prefix = prefix + (last ? glyphs.gap : glyphs.vertical);
        render_node(out, child, next_prefix, glyphs);
        ++i;
    }
}

void render_pending(std::ostream& out, const std::vector<PendingOp>& pending,
                    const TreeGlyphs& glyphs) {
    DryRunNode root;
    for (const auto& op : pending) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, PendingMkdir>) {
                    insert_path(root, o.path, /*is_dir=*/true,
                                /*is_append=*/false);
                } else if constexpr (std::is_same_v<T, PendingFile>) {
                    insert_path(root, o.path, /*is_dir=*/false, o.append);
                }
                // PendingCheckDir is intentionally skipped — it doesn't
                // create anything and dry-run can't validate it without
                // hitting the real filesystem.
            },
            op);
    }
    out << "Would create:\n";
    if (root.children.empty()) {
        out << "  (nothing)\n";
    } else {
        render_node(out, root, "", glyphs);
    }

    bool has_run = false;
    for (const auto& op : pending) {
        if (std::holds_alternative<PendingRun>(op)) {
            has_run = true;
            break;
        }
    }
    if (!has_run) {
        return;
    }
    out << "\nWould execute:\n";
    std::size_t i = 0;
    for (const auto& op : pending) {
        if (const auto* r = std::get_if<PendingRun>(&op)) {
            ++i;
            out << "  " << i << ". " << r->command;
            if (r->cwd.has_value()) {
                out << " (in " << *r->cwd << ")";
            }
            out << '\n';
        }
    }
}

}  // namespace

bool locale_is_utf8() {
    for (const char* var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const char* value = std::getenv(var);
        if (value == nullptr || value[0] == '\0') {
            continue;
        }
        std::string s(value);
        for (auto& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (s.find("utf-8") != std::string::npos ||
            s.find("utf8") != std::string::npos) {
            return true;
        }
        // The first set variable wins; an explicitly non-UTF-8 locale should
        // not be overridden by a later UTF-8 fallback.
        return false;
    }
    return false;
}

void dry_run(const Program& program, Prompter& prompter, std::ostream& out,
             bool ascii_only, const SourceProvider* source) {
    (void)source;
    Interpreter interp(prompter);
    interp.set_ask_total(count_ask_statements(program));
    for (const auto& stmt : program.statements) {
        interp.execute(*stmt);
    }
    render_pending(out, interp.pending(),
                   ascii_only ? kAsciiGlyphs : kUtf8Glyphs);
}

}  // namespace spudplate
