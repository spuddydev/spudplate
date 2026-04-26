#include "spudplate/serializer.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "spudplate/validator.h"

namespace spudplate {

namespace {

// Encoding any new variant requires a matching arm in `expr_to_json` below;
// these asserts make growing ExprData/StmtData/PathSegment/FileSource a
// build error until every arm has been added.
static_assert(std::variant_size_v<ExprData> == 8,
              "serializer: ExprData arity changed; update expr_to_json");
static_assert(std::variant_size_v<StmtData> == 9,
              "serializer: StmtData arity changed; update stmt_to_json");
static_assert(std::variant_size_v<PathSegment> == 3,
              "serializer: PathSegment arity changed; update path_segment_to_json");
static_assert(std::variant_size_v<FileSource> == 2,
              "serializer: FileSource arity changed; update file_source_to_json");

// Operators reachable from a parsed AST. The lexer can produce many token
// types but the parser only stores these on UnaryExpr/BinaryExpr — keeping
// the table closed catches accidental drift between parser and serializer.
std::string op_to_string(TokenType op) {
    switch (op) {
        case TokenType::NOT:
            return "NOT";
        case TokenType::PLUS:
            return "PLUS";
        case TokenType::MINUS:
            return "MINUS";
        case TokenType::STAR:
            return "STAR";
        case TokenType::SLASH:
            return "SLASH";
        case TokenType::EQUALS:
            return "EQUALS";
        case TokenType::NOT_EQUALS:
            return "NOT_EQUALS";
        case TokenType::GREATER:
            return "GREATER";
        case TokenType::LESS:
            return "LESS";
        case TokenType::GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TokenType::LESS_EQUAL:
            return "LESS_EQUAL";
        case TokenType::AND:
            return "AND";
        case TokenType::OR:
            return "OR";
        default:
            throw std::logic_error("serializer: unexpected operator token in AST");
    }
}

std::string var_type_to_string(VarType t) {
    switch (t) {
        case VarType::String:
            return "string";
        case VarType::Bool:
            return "bool";
        case VarType::Int:
            return "int";
    }
    throw std::logic_error("serializer: unexpected VarType");
}

nlohmann::json expr_to_json(const Expr& expr);

nlohmann::json path_segment_to_json(const PathSegment& seg) {
    return std::visit(
        [](const auto& s) -> nlohmann::json {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, PathLiteral>) {
                return {{"type", "Literal"},
                        {"value", s.value},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, PathVar>) {
                return {{"type", "Var"},
                        {"name", s.name},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, PathInterp>) {
                return {{"type", "Interp"},
                        {"expression", expr_to_json(*s.expression)},
                        {"line", s.line},
                        {"column", s.column}};
            }
        },
        seg);
}

nlohmann::json path_expr_to_json(const PathExpr& path) {
    nlohmann::json segs = nlohmann::json::array();
    for (const auto& seg : path.segments) {
        segs.push_back(path_segment_to_json(seg));
    }
    return {{"segments", std::move(segs)},
            {"line", path.line},
            {"column", path.column}};
}

nlohmann::json expr_to_json(const Expr& expr) {
    return std::visit(
        [](const auto& n) -> nlohmann::json {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                return {{"type", "StringLiteral"},
                        {"value", n.value},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                return {{"type", "IntegerLiteral"},
                        {"value", n.value},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                return {{"type", "BoolLiteral"},
                        {"value", n.value},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                return {{"type", "Identifier"},
                        {"name", n.name},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                return {{"type", "Unary"},
                        {"op", op_to_string(n.op)},
                        {"operand", expr_to_json(*n.operand)},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return {{"type", "Binary"},
                        {"op", op_to_string(n.op)},
                        {"left", expr_to_json(*n.left)},
                        {"right", expr_to_json(*n.right)},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                nlohmann::json args = nlohmann::json::array();
                for (const auto& a : n.arguments) {
                    args.push_back(expr_to_json(*a));
                }
                return {{"type", "FunctionCall"},
                        {"name", n.name},
                        {"arguments", std::move(args)},
                        {"line", n.line},
                        {"column", n.column}};
            } else if constexpr (std::is_same_v<T, TemplateStringExpr>) {
                nlohmann::json parts = nlohmann::json::array();
                for (const auto& p : n.parts) {
                    if (std::holds_alternative<std::string>(p)) {
                        parts.push_back(
                            {{"type", "Literal"},
                             {"value", std::get<std::string>(p)}});
                    } else {
                        parts.push_back(
                            {{"type", "Expression"},
                             {"expression",
                              expr_to_json(*std::get<ExprPtr>(p))}});
                    }
                }
                return {{"type", "TemplateString"},
                        {"parts", std::move(parts)},
                        {"line", n.line},
                        {"column", n.column}};
            }
        },
        expr.data);
}

nlohmann::json optional_expr_to_json(const std::optional<ExprPtr>& opt) {
    if (!opt.has_value()) {
        return nullptr;
    }
    return expr_to_json(**opt);
}

nlohmann::json optional_path_to_json(const std::optional<PathExpr>& opt) {
    if (!opt.has_value()) {
        return nullptr;
    }
    return path_expr_to_json(*opt);
}

nlohmann::json file_source_to_json(const FileSource& src) {
    return std::visit(
        [](const auto& s) -> nlohmann::json {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, FileFromSource>) {
                return {{"type", "FromSource"},
                        {"path", path_expr_to_json(s.path)},
                        {"verbatim", s.verbatim}};
            } else if constexpr (std::is_same_v<T, FileContentSource>) {
                return {{"type", "Content"},
                        {"value", expr_to_json(*s.value)}};
            }
        },
        src);
}

nlohmann::json stmt_to_json(const Stmt& stmt);

nlohmann::json stmt_to_json(const Stmt& stmt) {
    return std::visit(
        [](const auto& s) -> nlohmann::json {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, AskStmt>) {
                nlohmann::json options = nlohmann::json::array();
                for (const auto& opt : s.options) {
                    options.push_back(expr_to_json(*opt));
                }
                return {{"type", "Ask"},
                        {"name", s.name},
                        {"prompt", s.prompt},
                        {"var_type", var_type_to_string(s.var_type)},
                        {"default_value", optional_expr_to_json(s.default_value)},
                        {"options", std::move(options)},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                return {{"type", "Let"},
                        {"name", s.name},
                        {"value", expr_to_json(*s.value)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, AssignStmt>) {
                return {{"type", "Assign"},
                        {"name", s.name},
                        {"value", expr_to_json(*s.value)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                return {{"type", "Mkdir"},
                        {"path", path_expr_to_json(s.path)},
                        {"alias", s.alias.has_value()
                                      ? nlohmann::json(*s.alias)
                                      : nlohmann::json(nullptr)},
                        {"mkdir_p", s.mkdir_p},
                        {"from_source", optional_path_to_json(s.from_source)},
                        {"verbatim", s.verbatim},
                        {"mode", s.mode.has_value()
                                     ? nlohmann::json(*s.mode)
                                     : nlohmann::json(nullptr)},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, FileStmt>) {
                return {{"type", "File"},
                        {"path", path_expr_to_json(s.path)},
                        {"alias", s.alias.has_value()
                                      ? nlohmann::json(*s.alias)
                                      : nlohmann::json(nullptr)},
                        {"source", file_source_to_json(s.source)},
                        {"append", s.append},
                        {"mode", s.mode.has_value()
                                     ? nlohmann::json(*s.mode)
                                     : nlohmann::json(nullptr)},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                nlohmann::json body = nlohmann::json::array();
                for (const auto& inner : s.body) {
                    body.push_back(stmt_to_json(*inner));
                }
                return {{"type", "Repeat"},
                        {"collection_var", s.collection_var},
                        {"iterator_var", s.iterator_var},
                        {"body", std::move(body)},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, CopyStmt>) {
                return {{"type", "Copy"},
                        {"source", path_expr_to_json(s.source)},
                        {"destination", path_expr_to_json(s.destination)},
                        {"verbatim", s.verbatim},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                return {{"type", "Include"},
                        {"name", s.name},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            } else if constexpr (std::is_same_v<T, RunStmt>) {
                return {{"type", "Run"},
                        {"command", expr_to_json(*s.command)},
                        {"cwd", optional_path_to_json(s.cwd)},
                        {"when_clause", optional_expr_to_json(s.when_clause)},
                        {"line", s.line},
                        {"column", s.column}};
            }
        },
        stmt.data);
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const std::string& message, const std::string& pointer,
                       std::optional<int> line = std::nullopt,
                       std::optional<int> column = std::nullopt) {
    throw DeserializeError(message, pointer, line, column);
}

ExprPtr make_expr(ExprData data) {
    return std::unique_ptr<Expr>(new Expr{std::move(data)});
}

StmtPtr make_stmt(StmtData data) {
    return std::unique_ptr<Stmt>(new Stmt{std::move(data)});
}

const nlohmann::json& require_field(const nlohmann::json& obj,
                                    const std::string& field,
                                    const std::string& pointer) {
    auto it = obj.find(field);
    if (it == obj.end()) {
        fail("missing field '" + field + "'", pointer);
    }
    return *it;
}

std::string require_string(const nlohmann::json& obj, const std::string& field,
                           const std::string& pointer) {
    const auto& v = require_field(obj, field, pointer);
    if (!v.is_string()) {
        fail("field '" + field + "' must be a string", pointer);
    }
    return v.get<std::string>();
}

int require_int(const nlohmann::json& obj, const std::string& field,
                const std::string& pointer) {
    const auto& v = require_field(obj, field, pointer);
    if (!v.is_number_integer()) {
        fail("field '" + field + "' must be an integer", pointer);
    }
    return v.get<int>();
}

bool require_bool(const nlohmann::json& obj, const std::string& field,
                  const std::string& pointer) {
    const auto& v = require_field(obj, field, pointer);
    if (!v.is_boolean()) {
        fail("field '" + field + "' must be a boolean", pointer);
    }
    return v.get<bool>();
}

std::optional<int> require_optional_int(const nlohmann::json& obj,
                                        const std::string& field,
                                        const std::string& pointer) {
    const auto& v = require_field(obj, field, pointer);
    if (v.is_null()) {
        return std::nullopt;
    }
    if (!v.is_number_integer()) {
        fail("field '" + field + "' must be an integer or null", pointer);
    }
    return v.get<int>();
}

std::optional<std::string> require_optional_string(const nlohmann::json& obj,
                                                   const std::string& field,
                                                   const std::string& pointer) {
    const auto& v = require_field(obj, field, pointer);
    if (v.is_null()) {
        return std::nullopt;
    }
    if (!v.is_string()) {
        fail("field '" + field + "' must be a string or null", pointer);
    }
    return v.get<std::string>();
}

const nlohmann::json& require_array(const nlohmann::json& obj,
                                    const std::string& field,
                                    const std::string& pointer) {
    const auto& v = require_field(obj, field, pointer);
    if (!v.is_array()) {
        fail("field '" + field + "' must be an array", pointer);
    }
    return v;
}

TokenType string_to_op(const std::string& s, const std::string& pointer) {
    if (s == "NOT") return TokenType::NOT;
    if (s == "PLUS") return TokenType::PLUS;
    if (s == "MINUS") return TokenType::MINUS;
    if (s == "STAR") return TokenType::STAR;
    if (s == "SLASH") return TokenType::SLASH;
    if (s == "EQUALS") return TokenType::EQUALS;
    if (s == "NOT_EQUALS") return TokenType::NOT_EQUALS;
    if (s == "GREATER") return TokenType::GREATER;
    if (s == "LESS") return TokenType::LESS;
    if (s == "GREATER_EQUAL") return TokenType::GREATER_EQUAL;
    if (s == "LESS_EQUAL") return TokenType::LESS_EQUAL;
    if (s == "AND") return TokenType::AND;
    if (s == "OR") return TokenType::OR;
    fail("unknown operator '" + s + "'", pointer);
}

VarType string_to_var_type(const std::string& s, const std::string& pointer) {
    if (s == "string") return VarType::String;
    if (s == "bool") return VarType::Bool;
    if (s == "int") return VarType::Int;
    fail("unknown var_type '" + s + "'", pointer);
}

ExprPtr expr_from_json(const nlohmann::json& j, const std::string& pointer);

PathSegment path_segment_from_json(const nlohmann::json& j,
                                   const std::string& pointer) {
    if (!j.is_object()) {
        fail("path segment must be an object", pointer);
    }
    std::string type = require_string(j, "type", pointer);
    int line = require_int(j, "line", pointer);
    int column = require_int(j, "column", pointer);
    if (type == "Literal") {
        return PathLiteral{
            .value = require_string(j, "value", pointer),
            .line = line,
            .column = column,
        };
    }
    if (type == "Var") {
        return PathVar{
            .name = require_string(j, "name", pointer),
            .line = line,
            .column = column,
        };
    }
    if (type == "Interp") {
        return PathInterp{
            .expression = expr_from_json(require_field(j, "expression", pointer),
                                         pointer + "/expression"),
            .line = line,
            .column = column,
        };
    }
    fail("unknown path segment type '" + type + "'", pointer, line, column);
}

PathExpr path_expr_from_json(const nlohmann::json& j,
                             const std::string& pointer) {
    if (!j.is_object()) {
        fail("path expression must be an object", pointer);
    }
    const auto& segs_json = require_array(j, "segments", pointer);
    std::vector<PathSegment> segs;
    segs.reserve(segs_json.size());
    for (size_t i = 0; i < segs_json.size(); ++i) {
        segs.push_back(path_segment_from_json(
            segs_json[i], pointer + "/segments/" + std::to_string(i)));
    }
    return PathExpr{
        .segments = std::move(segs),
        .line = require_int(j, "line", pointer),
        .column = require_int(j, "column", pointer),
    };
}

ExprPtr expr_from_json(const nlohmann::json& j, const std::string& pointer) {
    if (!j.is_object()) {
        fail("expression must be an object", pointer);
    }
    std::string type = require_string(j, "type", pointer);
    int line = require_int(j, "line", pointer);
    int column = require_int(j, "column", pointer);
    if (type == "StringLiteral") {
        return make_expr(StringLiteralExpr{
            .value = require_string(j, "value", pointer),
            .line = line,
            .column = column,
        });
    }
    if (type == "IntegerLiteral") {
        return make_expr(IntegerLiteralExpr{
            .value = require_int(j, "value", pointer),
            .line = line,
            .column = column,
        });
    }
    if (type == "BoolLiteral") {
        return make_expr(BoolLiteralExpr{
            .value = require_bool(j, "value", pointer),
            .line = line,
            .column = column,
        });
    }
    if (type == "Identifier") {
        return make_expr(IdentifierExpr{
            .name = require_string(j, "name", pointer),
            .line = line,
            .column = column,
        });
    }
    if (type == "Unary") {
        return make_expr(UnaryExpr{
            .op = string_to_op(require_string(j, "op", pointer), pointer + "/op"),
            .operand = expr_from_json(require_field(j, "operand", pointer),
                                      pointer + "/operand"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Binary") {
        return make_expr(BinaryExpr{
            .op = string_to_op(require_string(j, "op", pointer), pointer + "/op"),
            .left = expr_from_json(require_field(j, "left", pointer),
                                   pointer + "/left"),
            .right = expr_from_json(require_field(j, "right", pointer),
                                    pointer + "/right"),
            .line = line,
            .column = column,
        });
    }
    if (type == "FunctionCall") {
        const auto& args_json = require_array(j, "arguments", pointer);
        std::vector<ExprPtr> args;
        args.reserve(args_json.size());
        for (size_t i = 0; i < args_json.size(); ++i) {
            args.push_back(expr_from_json(
                args_json[i], pointer + "/arguments/" + std::to_string(i)));
        }
        return make_expr(FunctionCallExpr{
            .name = require_string(j, "name", pointer),
            .arguments = std::move(args),
            .line = line,
            .column = column,
        });
    }
    if (type == "TemplateString") {
        const auto& parts_json = require_array(j, "parts", pointer);
        std::vector<std::variant<std::string, ExprPtr>> parts;
        parts.reserve(parts_json.size());
        for (size_t i = 0; i < parts_json.size(); ++i) {
            std::string part_ptr = pointer + "/parts/" + std::to_string(i);
            const auto& part = parts_json[i];
            if (!part.is_object()) {
                fail("template-string part must be an object", part_ptr);
            }
            std::string part_type = require_string(part, "type", part_ptr);
            if (part_type == "Literal") {
                parts.emplace_back(require_string(part, "value", part_ptr));
            } else if (part_type == "Expression") {
                parts.emplace_back(expr_from_json(
                    require_field(part, "expression", part_ptr),
                    part_ptr + "/expression"));
            } else {
                fail("unknown template-string part type '" + part_type + "'",
                     part_ptr);
            }
        }
        return make_expr(TemplateStringExpr{
            .parts = std::move(parts),
            .line = line,
            .column = column,
        });
    }
    fail("unknown expression type '" + type + "'", pointer, line, column);
}

std::optional<ExprPtr> optional_expr_from_json(const nlohmann::json& j,
                                               const std::string& pointer) {
    if (j.is_null()) {
        return std::nullopt;
    }
    return expr_from_json(j, pointer);
}

std::optional<PathExpr> optional_path_from_json(const nlohmann::json& j,
                                                const std::string& pointer) {
    if (j.is_null()) {
        return std::nullopt;
    }
    return path_expr_from_json(j, pointer);
}

FileSource file_source_from_json(const nlohmann::json& j,
                                 const std::string& pointer) {
    if (!j.is_object()) {
        fail("file source must be an object", pointer);
    }
    std::string type = require_string(j, "type", pointer);
    if (type == "FromSource") {
        return FileFromSource{
            .path = path_expr_from_json(require_field(j, "path", pointer),
                                        pointer + "/path"),
            .verbatim = require_bool(j, "verbatim", pointer),
        };
    }
    if (type == "Content") {
        return FileContentSource{
            .value = expr_from_json(require_field(j, "value", pointer),
                                    pointer + "/value"),
        };
    }
    fail("unknown file source type '" + type + "'", pointer);
}

StmtPtr stmt_from_json(const nlohmann::json& j, const std::string& pointer);

StmtPtr stmt_from_json(const nlohmann::json& j, const std::string& pointer) {
    if (!j.is_object()) {
        fail("statement must be an object", pointer);
    }
    std::string type = require_string(j, "type", pointer);
    int line = require_int(j, "line", pointer);
    int column = require_int(j, "column", pointer);
    if (type == "Ask") {
        const auto& options_json = require_array(j, "options", pointer);
        std::vector<ExprPtr> options;
        options.reserve(options_json.size());
        for (size_t i = 0; i < options_json.size(); ++i) {
            options.push_back(expr_from_json(
                options_json[i], pointer + "/options/" + std::to_string(i)));
        }
        return make_stmt(AskStmt{
            .name = require_string(j, "name", pointer),
            .prompt = require_string(j, "prompt", pointer),
            .var_type = string_to_var_type(
                require_string(j, "var_type", pointer), pointer + "/var_type"),
            .default_value = optional_expr_from_json(
                require_field(j, "default_value", pointer),
                pointer + "/default_value"),
            .options = std::move(options),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Let") {
        return make_stmt(LetStmt{
            .name = require_string(j, "name", pointer),
            .value = expr_from_json(require_field(j, "value", pointer),
                                    pointer + "/value"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Assign") {
        return make_stmt(AssignStmt{
            .name = require_string(j, "name", pointer),
            .value = expr_from_json(require_field(j, "value", pointer),
                                    pointer + "/value"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Mkdir") {
        return make_stmt(MkdirStmt{
            .path = path_expr_from_json(require_field(j, "path", pointer),
                                        pointer + "/path"),
            .alias = require_optional_string(j, "alias", pointer),
            .mkdir_p = require_bool(j, "mkdir_p", pointer),
            .from_source = optional_path_from_json(
                require_field(j, "from_source", pointer),
                pointer + "/from_source"),
            .verbatim = require_bool(j, "verbatim", pointer),
            .mode = require_optional_int(j, "mode", pointer),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    if (type == "File") {
        return make_stmt(FileStmt{
            .path = path_expr_from_json(require_field(j, "path", pointer),
                                        pointer + "/path"),
            .alias = require_optional_string(j, "alias", pointer),
            .source = file_source_from_json(require_field(j, "source", pointer),
                                            pointer + "/source"),
            .append = require_bool(j, "append", pointer),
            .mode = require_optional_int(j, "mode", pointer),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Repeat") {
        const auto& body_json = require_array(j, "body", pointer);
        std::vector<StmtPtr> body;
        body.reserve(body_json.size());
        for (size_t i = 0; i < body_json.size(); ++i) {
            body.push_back(stmt_from_json(
                body_json[i], pointer + "/body/" + std::to_string(i)));
        }
        return make_stmt(RepeatStmt{
            .collection_var = require_string(j, "collection_var", pointer),
            .iterator_var = require_string(j, "iterator_var", pointer),
            .body = std::move(body),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Copy") {
        return make_stmt(CopyStmt{
            .source = path_expr_from_json(require_field(j, "source", pointer),
                                          pointer + "/source"),
            .destination = path_expr_from_json(
                require_field(j, "destination", pointer),
                pointer + "/destination"),
            .verbatim = require_bool(j, "verbatim", pointer),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Include") {
        return make_stmt(IncludeStmt{
            .name = require_string(j, "name", pointer),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    if (type == "Run") {
        return make_stmt(RunStmt{
            .command = expr_from_json(require_field(j, "command", pointer),
                                      pointer + "/command"),
            .cwd = optional_path_from_json(require_field(j, "cwd", pointer),
                                           pointer + "/cwd"),
            .when_clause = optional_expr_from_json(
                require_field(j, "when_clause", pointer),
                pointer + "/when_clause"),
            .line = line,
            .column = column,
        });
    }
    fail("unknown statement type '" + type + "'", pointer, line, column);
}

}  // namespace

DeserializeError::DeserializeError(std::string message, std::string json_pointer,
                                   std::optional<int> line,
                                   std::optional<int> column)
    : std::runtime_error(message),
      message_(std::move(message)),
      json_pointer_(std::move(json_pointer)),
      line_(line),
      column_(column) {}

nlohmann::json program_to_json(const Program& program) {
    nlohmann::json statements = nlohmann::json::array();
    for (const auto& stmt : program.statements) {
        statements.push_back(stmt_to_json(*stmt));
    }
    return {{"format_version", SPUDPLATE_FORMAT_VERSION},
            {"statements", std::move(statements)}};
}

Program program_from_json(const nlohmann::json& root) {
    try {
        if (!root.is_object()) {
            throw DeserializeError("root must be a JSON object", "");
        }
        const auto& version = require_field(root, "format_version", "");
        if (!version.is_number_integer()) {
            throw DeserializeError("'format_version' must be an integer",
                                   "/format_version");
        }
        int got = version.get<int>();
        if (got != SPUDPLATE_FORMAT_VERSION) {
            throw DeserializeError(
                "format_version mismatch: expected " +
                    std::to_string(SPUDPLATE_FORMAT_VERSION) + ", got " +
                    std::to_string(got),
                "/format_version");
        }
        const auto& stmts_json = require_array(root, "statements", "");
        Program out;
        out.statements.reserve(stmts_json.size());
        for (size_t i = 0; i < stmts_json.size(); ++i) {
            out.statements.push_back(stmt_from_json(
                stmts_json[i], "/statements/" + std::to_string(i)));
        }
        return out;
    } catch (const DeserializeError&) {
        throw;
    } catch (const nlohmann::json::exception& e) {
        throw DeserializeError(std::string("json error: ") + e.what(), "");
    }
}

namespace {

// `exprs_equal` (validator.h) compares semantics but ignores line/column —
// good for the validator's normalised-when-clause check, wrong for a
// serialiser round-trip test. These helpers wrap it and also walk the
// position fields, so a successful `program_to_json` -> `program_from_json`
// round trip can assert exact equality (including positions).

bool positions_equal(int la, int ca, int lb, int cb) {
    return la == lb && ca == cb;
}

bool optional_expr_equal(const std::optional<ExprPtr>& a,
                         const std::optional<ExprPtr>& b);
bool path_segments_equal(const PathSegment& a, const PathSegment& b);
bool paths_equal(const PathExpr& a, const PathExpr& b);
bool stmts_equal(const Stmt& a, const Stmt& b);

bool exprs_equal_with_positions(const Expr& a, const Expr& b) {
    if (a.data.index() != b.data.index()) {
        return false;
    }
    if (!exprs_equal(a, b)) {
        return false;
    }
    return std::visit(
        [&](const auto& ax) -> bool {
            using T = std::decay_t<decltype(ax)>;
            const auto& bx = std::get<T>(b.data);
            if (!positions_equal(ax.line, ax.column, bx.line, bx.column)) {
                return false;
            }
            if constexpr (std::is_same_v<T, UnaryExpr>) {
                return exprs_equal_with_positions(*ax.operand, *bx.operand);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                return exprs_equal_with_positions(*ax.left, *bx.left) &&
                       exprs_equal_with_positions(*ax.right, *bx.right);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                for (size_t i = 0; i < ax.arguments.size(); ++i) {
                    if (!exprs_equal_with_positions(*ax.arguments[i],
                                                    *bx.arguments[i])) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, TemplateStringExpr>) {
                for (size_t i = 0; i < ax.parts.size(); ++i) {
                    if (std::holds_alternative<ExprPtr>(ax.parts[i]) &&
                        !exprs_equal_with_positions(
                            *std::get<ExprPtr>(ax.parts[i]),
                            *std::get<ExprPtr>(bx.parts[i]))) {
                        return false;
                    }
                }
                return true;
            } else {
                return true;
            }
        },
        a.data);
}

bool optional_expr_equal(const std::optional<ExprPtr>& a,
                         const std::optional<ExprPtr>& b) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a.has_value()) {
        return true;
    }
    return exprs_equal_with_positions(**a, **b);
}

bool path_segments_equal(const PathSegment& a, const PathSegment& b) {
    if (a.index() != b.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& ax) -> bool {
            using T = std::decay_t<decltype(ax)>;
            const auto& bx = std::get<T>(b);
            if (!positions_equal(ax.line, ax.column, bx.line, bx.column)) {
                return false;
            }
            if constexpr (std::is_same_v<T, PathLiteral>) {
                return ax.value == bx.value;
            } else if constexpr (std::is_same_v<T, PathVar>) {
                return ax.name == bx.name;
            } else if constexpr (std::is_same_v<T, PathInterp>) {
                return exprs_equal_with_positions(*ax.expression, *bx.expression);
            }
        },
        a);
}

bool paths_equal(const PathExpr& a, const PathExpr& b) {
    if (!positions_equal(a.line, a.column, b.line, b.column)) {
        return false;
    }
    if (a.segments.size() != b.segments.size()) {
        return false;
    }
    for (size_t i = 0; i < a.segments.size(); ++i) {
        if (!path_segments_equal(a.segments[i], b.segments[i])) {
            return false;
        }
    }
    return true;
}

bool optional_path_equal(const std::optional<PathExpr>& a,
                         const std::optional<PathExpr>& b) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a.has_value()) {
        return true;
    }
    return paths_equal(*a, *b);
}

bool file_sources_equal(const FileSource& a, const FileSource& b) {
    if (a.index() != b.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& ax) -> bool {
            using T = std::decay_t<decltype(ax)>;
            const auto& bx = std::get<T>(b);
            if constexpr (std::is_same_v<T, FileFromSource>) {
                return paths_equal(ax.path, bx.path) &&
                       ax.verbatim == bx.verbatim;
            } else if constexpr (std::is_same_v<T, FileContentSource>) {
                return exprs_equal_with_positions(*ax.value, *bx.value);
            }
        },
        a);
}

bool stmts_equal(const Stmt& a, const Stmt& b) {
    if (a.data.index() != b.data.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& ax) -> bool {
            using T = std::decay_t<decltype(ax)>;
            const auto& bx = std::get<T>(b.data);
            if (!positions_equal(ax.line, ax.column, bx.line, bx.column)) {
                return false;
            }
            if constexpr (std::is_same_v<T, AskStmt>) {
                if (ax.name != bx.name || ax.prompt != bx.prompt ||
                    ax.var_type != bx.var_type ||
                    ax.options.size() != bx.options.size()) {
                    return false;
                }
                if (!optional_expr_equal(ax.default_value, bx.default_value) ||
                    !optional_expr_equal(ax.when_clause, bx.when_clause)) {
                    return false;
                }
                for (size_t i = 0; i < ax.options.size(); ++i) {
                    if (!exprs_equal_with_positions(*ax.options[i],
                                                    *bx.options[i])) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                return ax.name == bx.name &&
                       exprs_equal_with_positions(*ax.value, *bx.value);
            } else if constexpr (std::is_same_v<T, AssignStmt>) {
                return ax.name == bx.name &&
                       exprs_equal_with_positions(*ax.value, *bx.value);
            } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                return paths_equal(ax.path, bx.path) && ax.alias == bx.alias &&
                       ax.mkdir_p == bx.mkdir_p &&
                       optional_path_equal(ax.from_source, bx.from_source) &&
                       ax.verbatim == bx.verbatim && ax.mode == bx.mode &&
                       optional_expr_equal(ax.when_clause, bx.when_clause);
            } else if constexpr (std::is_same_v<T, FileStmt>) {
                return paths_equal(ax.path, bx.path) && ax.alias == bx.alias &&
                       file_sources_equal(ax.source, bx.source) &&
                       ax.append == bx.append && ax.mode == bx.mode &&
                       optional_expr_equal(ax.when_clause, bx.when_clause);
            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                if (ax.collection_var != bx.collection_var ||
                    ax.iterator_var != bx.iterator_var ||
                    ax.body.size() != bx.body.size() ||
                    !optional_expr_equal(ax.when_clause, bx.when_clause)) {
                    return false;
                }
                for (size_t i = 0; i < ax.body.size(); ++i) {
                    if (!stmts_equal(*ax.body[i], *bx.body[i])) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, CopyStmt>) {
                return paths_equal(ax.source, bx.source) &&
                       paths_equal(ax.destination, bx.destination) &&
                       ax.verbatim == bx.verbatim &&
                       optional_expr_equal(ax.when_clause, bx.when_clause);
            } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                return ax.name == bx.name &&
                       optional_expr_equal(ax.when_clause, bx.when_clause);
            } else if constexpr (std::is_same_v<T, RunStmt>) {
                return exprs_equal_with_positions(*ax.command, *bx.command) &&
                       optional_path_equal(ax.cwd, bx.cwd) &&
                       optional_expr_equal(ax.when_clause, bx.when_clause);
            }
        },
        a.data);
}

}  // namespace

bool programs_equal(const Program& a, const Program& b) {
    if (a.statements.size() != b.statements.size()) {
        return false;
    }
    for (size_t i = 0; i < a.statements.size(); ++i) {
        if (!stmts_equal(*a.statements[i], *b.statements[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace spudplate
