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
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

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

void execute_ask(const AskStmt& stmt, Environment& env, Prompter& prompter) {
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

    void execute(const Stmt& stmt) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, AskStmt>) {
                    execute_ask(s, env_, prompter_);
                } else if constexpr (std::is_same_v<T, LetStmt>) {
                    Value v = evaluate_expr(*s.value, env_);
                    env_.declare(s.name, std::move(v));
                } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                    execute_mkdir(s);
                } else if constexpr (std::is_same_v<T, FileStmt>) {
                    execute_file(s);
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    execute_repeat(s);
                } else if constexpr (std::is_same_v<T, CopyStmt>) {
                    unsupported("copy", s.line, s.column);
                } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                    unsupported("include", s.line, s.column);
                }
            },
            stmt.data);
    }

    [[nodiscard]] Environment& env() { return env_; }

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
            env_.pop();
        }
    }

    void execute_mkdir(const MkdirStmt& s) {
        if (!when_passes(s.when_clause, env_)) {
            return;
        }
        if (s.from_source.has_value() || s.verbatim) {
            throw RuntimeError(
                "'mkdir from' / 'verbatim' not yet supported in this build",
                s.line, s.column);
        }
        std::string path_str = evaluate_path(s.path, env_, alias_map_);
        if (s.alias.has_value()) {
            alias_map_[*s.alias] = path_str;
        }
        pending_.push_back(PendingMkdir{.path = std::move(path_str),
                                        .mode = s.mode,
                                        .line = s.line,
                                        .column = s.column});
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
        // Reject `from` first so a thrown statement leaves no stale alias.
        if (std::holds_alternative<FileFromSource>(s.source)) {
            throw RuntimeError("'file from' not yet supported in this build",
                               s.line, s.column);
        }
        const auto& content_src = std::get<FileContentSource>(s.source);
        std::string content =
            value_to_string(evaluate_expr(*content_src.value, env_));

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
};

void run_program(const Program& program, Interpreter& interp) {
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
constexpr const char* kDim = "\x1b[2m";
constexpr const char* kRed = "\x1b[31m";
constexpr const char* kCyan = "\x1b[36m";

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
    if (req.previous_error.has_value()) {
        out << wrap("> " + *req.previous_error, kRed, use_colour) << '\n';
    }

    out << wrap(req.text, kBold, use_colour);

    if (req.type == VarType::Bool && req.options.empty()) {
        out << ' ' << wrap(bool_hint(req.default_value), kDim, use_colour);
    }

    if (!req.options.empty()) {
        out << '\n';
        for (std::size_t i = 0; i < req.options.size(); ++i) {
            std::string entry =
                "  [" + std::to_string(i + 1) + "] " + req.options[i];
            out << wrap(entry, kDim, use_colour) << '\n';
        }
    }

    if (req.default_value.has_value()) {
        std::string hint;
        if (req.type == VarType::Bool && req.options.empty()) {
            // [Y/n] already encodes the default — skip the explicit hint.
        } else {
            hint = " [default: " + *req.default_value + "]";
            out << wrap(hint, kDim, use_colour);
        }
    }

    out << '\n' << wrap("> ", kCyan, use_colour) << std::flush;
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

std::string ScriptedPrompter::prompt(const PromptRequest& req) {
    last_ = req;
    if (index_ >= answers_.size()) {
        throw std::logic_error("ScriptedPrompter exhausted");
    }
    return answers_[index_++];
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
    Interpreter interp(prompter);
    run_program(program, interp);
}

Environment run_for_tests(const Program& program, Prompter& prompter) {
    Interpreter interp(prompter);
    run_program(program, interp);
    return std::move(interp.env());
}

}  // namespace spudplate
