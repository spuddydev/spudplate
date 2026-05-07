#include "spudplate/introspect.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace spudplate {

namespace {

void walk(const std::vector<StmtPtr>& body, bool nested,
          std::vector<TopLevelAsk>& out) {
    for (const auto& sp : body) {
        if (!sp) continue;
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, AskStmt>) {
                    if (nested) return;
                    out.push_back(TopLevelAsk{
                        .name = s.name,
                        .var_type = s.var_type,
                        .prompt = s.prompt,
                        .default_value = s.default_value.has_value()
                                             ? s.default_value->get()
                                             : nullptr,
                        .options = &s.options,
                        .when_clause = s.when_clause.has_value()
                                           ? s.when_clause->get()
                                           : nullptr,
                        .line = s.line,
                        .column = s.column,
                    });
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    walk(s.body, true, out);
                } else if constexpr (std::is_same_v<T, IfStmt>) {
                    walk(s.body, true, out);
                }
            },
            sp->data);
    }
}

const char* type_name(VarType t) {
    switch (t) {
        case VarType::String: return "string";
        case VarType::Bool:   return "bool";
        case VarType::Int:    return "int";
    }
    return "unknown";
}

// Render a literal expression to its YAML form. Strings get JSON-style
// double quotes with `\` and `"` escaped (any other character is emitted
// as-is, which is fine for the printable content templates produce).
// Returns false for non-literal expressions; the caller emits a placeholder.
bool format_literal(const Expr* e, std::string& out) {
    if (e == nullptr) return false;
    return std::visit(
        [&](const auto& v) -> bool {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                out += '"';
                for (char c : v.value) {
                    if (c == '\\' || c == '"') out += '\\';
                    out += c;
                }
                out += '"';
                return true;
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                out += std::to_string(v.value);
                return true;
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                out += v.value ? "true" : "false";
                return true;
            } else {
                return false;
            }
        },
        e->data);
}

// Empty placeholder used when no default exists or it is a non-literal.
std::string empty_placeholder(VarType t) {
    switch (t) {
        case VarType::String: return "\"\"";
        case VarType::Bool:   return "false";
        case VarType::Int:    return "0";
    }
    return "\"\"";
}

void format_options(std::string& out, const std::vector<ExprPtr>& options) {
    bool first = true;
    for (const auto& opt : options) {
        if (!first) out += ", ";
        first = false;
        std::string rendered;
        if (opt && format_literal(opt.get(), rendered)) {
            out += rendered;
        } else {
            out += "?";
        }
    }
}

// A stripped lower(trim()) for case-insensitive boolean parsing.
std::string lower_trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string out;
    out.reserve(b - a);
    for (std::size_t i = a; i < b; ++i) {
        out += static_cast<char>(
            std::tolower(static_cast<unsigned char>(s[i])));
    }
    return out;
}

bool parse_int_literal(const std::string& tok, std::int64_t& out) {
    if (tok.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (tok[0] == '+' || tok[0] == '-') {
        neg = (tok[0] == '-');
        ++i;
        if (i == tok.size()) return false;
    }
    std::int64_t acc = 0;
    for (; i < tok.size(); ++i) {
        char c = tok[i];
        if (c < '0' || c > '9') return false;
        acc = acc * 10 + (c - '0');
    }
    out = neg ? -acc : acc;
    return true;
}

// Strip the surrounding quotes from a YAML scalar and process the minimal
// escape set we emit (`\\`, `\"`). Other backslash escapes are left as-is
// so users can paste paths like `C:\Users\name` unquoted with no surprises.
bool parse_quoted_string(const std::string& tok, std::string& out, int line) {
    if (tok.size() < 2 || tok.front() != '"' || tok.back() != '"') {
        return false;
    }
    out.clear();
    for (std::size_t i = 1; i + 1 < tok.size(); ++i) {
        char c = tok[i];
        if (c == '\\' && i + 2 < tok.size()) {
            char nxt = tok[i + 1];
            if (nxt == '"' || nxt == '\\') {
                out += nxt;
                ++i;
                continue;
            }
        }
        if (c == '"') {
            throw AnswersParseError(
                "unescaped quote inside string", line);
        }
        out += c;
    }
    return true;
}

}  // namespace

std::vector<TopLevelAsk> collect_top_level_asks(const Program& program) {
    std::vector<TopLevelAsk> out;
    walk(program.statements, /*nested=*/false, out);
    return out;
}

void emit_questions_human(std::ostream& out,
                          const std::vector<TopLevelAsk>& asks) {
    if (asks.empty()) {
        out << "(no top-level questions)\n";
        return;
    }
    out << "Questions:\n";
    for (const auto& a : asks) {
        out << "  " << a.name << " (" << type_name(a.var_type) << "): "
            << a.prompt;
        std::string suffix;
        if (a.default_value != nullptr) {
            std::string lit;
            if (format_literal(a.default_value, lit)) {
                suffix += " [default: " + lit + "]";
            } else {
                suffix += " [default: <expression>]";
            }
        }
        if (a.options != nullptr && !a.options->empty()) {
            std::string opts;
            format_options(opts, *a.options);
            suffix += " [options: " + opts + "]";
        }
        if (a.when_clause != nullptr) {
            suffix += " [when: gated]";
        }
        out << suffix << "\n";
    }
}

void emit_questions_yaml(std::ostream& out,
                         const std::vector<TopLevelAsk>& asks) {
    out << "# Spudplate answer template.\n"
           "# Edit each value below, then pass the file to "
           "`spudplate run <name> --answers <this-file>`.\n"
           "# Lines beginning with `#` are comments. `when`-gated questions "
           "are skipped when the\n"
           "# condition evaluates false; in that case the value here is "
           "ignored and the\n"
           "# template's default fires instead.\n";
    if (asks.empty()) {
        out << "\n# (this template has no top-level questions)\n";
        return;
    }
    bool first = true;
    for (const auto& a : asks) {
        if (!first) out << "\n";
        first = false;
        out << "\n# " << a.name << " (" << type_name(a.var_type) << "): "
            << a.prompt << "\n";
        if (a.options != nullptr && !a.options->empty()) {
            out << "# options: ";
            std::string opts;
            format_options(opts, *a.options);
            out << opts << "\n";
        }
        if (a.when_clause != nullptr) {
            out << "# when: gated (answer applies only if the condition is "
                   "true at runtime)\n";
        }
        std::string value;
        if (a.default_value != nullptr &&
            format_literal(a.default_value, value)) {
            // Use the literal default verbatim.
        } else {
            value = empty_placeholder(a.var_type);
        }
        out << a.name << ": " << value << "\n";
    }
}

std::unordered_map<std::string, Value> parse_answers_yaml(
    const std::string& source,
    const std::vector<TopLevelAsk>& asks) {
    std::unordered_map<std::string, VarType> by_name;
    by_name.reserve(asks.size());
    for (const auto& a : asks) {
        by_name.emplace(a.name, a.var_type);
    }

    std::unordered_map<std::string, Value> out;
    std::unordered_set<std::string> seen;
    std::istringstream in(source);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        // Strip a trailing carriage return for CRLF files.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::size_t i = 0;
        while (i < line.size() &&
               std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i == line.size() || line[i] == '#') continue;

        // Bare key up to ':'
        std::size_t key_start = i;
        while (i < line.size() && line[i] != ':' &&
               !std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (key_start == i) {
            throw AnswersParseError("expected key", line_no);
        }
        std::string key = line.substr(key_start, i - key_start);
        while (i < line.size() &&
               std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i >= line.size() || line[i] != ':') {
            throw AnswersParseError(
                "expected ':' after key '" + key + "'", line_no);
        }
        ++i;  // past ':'
        while (i < line.size() &&
               std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }

        // Trailing inline comment is permitted only after a quoted value.
        // For bare values we treat ` # ...` as a comment, but `#` inside a
        // bare token is part of the token. Keep this dead simple: trim
        // trailing whitespace and accept the rest as the raw value token.
        std::string raw;
        if (i < line.size() && line[i] == '"') {
            // Quoted string: scan to closing quote, allowing `\"` and `\\`.
            std::size_t j = i + 1;
            while (j < line.size()) {
                if (line[j] == '\\' && j + 1 < line.size()) {
                    j += 2;
                    continue;
                }
                if (line[j] == '"') break;
                ++j;
            }
            if (j >= line.size()) {
                throw AnswersParseError(
                    "unterminated quoted string", line_no);
            }
            raw = line.substr(i, j - i + 1);
            // Anything after the closing quote (besides whitespace and a
            // trailing `#` comment) is an error.
            std::size_t k = j + 1;
            while (k < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[k]))) {
                ++k;
            }
            if (k < line.size() && line[k] != '#') {
                throw AnswersParseError(
                    "unexpected text after quoted value", line_no);
            }
        } else {
            std::size_t end = line.size();
            while (end > i &&
                   std::isspace(
                       static_cast<unsigned char>(line[end - 1]))) {
                --end;
            }
            raw = line.substr(i, end - i);
        }

        if (raw.empty()) {
            throw AnswersParseError(
                "empty value for key '" + key + "'", line_no);
        }

        auto it = by_name.find(key);
        if (it == by_name.end()) {
            throw AnswersParseError(
                "unknown question '" + key + "'", line_no);
        }
        if (!seen.insert(key).second) {
            throw AnswersParseError(
                "duplicate key '" + key + "'", line_no);
        }

        switch (it->second) {
            case VarType::String: {
                std::string s;
                if (!raw.empty() && raw.front() == '"') {
                    if (!parse_quoted_string(raw, s, line_no)) {
                        throw AnswersParseError(
                            "malformed quoted string for '" + key + "'",
                            line_no);
                    }
                } else {
                    s = raw;
                }
                out.emplace(key, Value{std::move(s)});
                break;
            }
            case VarType::Int: {
                std::int64_t n = 0;
                if (!parse_int_literal(raw, n)) {
                    throw AnswersParseError(
                        "value for '" + key + "' is not an integer",
                        line_no);
                }
                out.emplace(key, Value{n});
                break;
            }
            case VarType::Bool: {
                std::string lc = lower_trim(raw);
                if (lc == "true") {
                    out.emplace(key, Value{true});
                } else if (lc == "false") {
                    out.emplace(key, Value{false});
                } else {
                    throw AnswersParseError(
                        "value for '" + key +
                            "' is not a boolean (use true or false)",
                        line_no);
                }
                break;
            }
        }
    }
    return out;
}

}  // namespace spudplate
