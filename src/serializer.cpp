#include "spudplate/serializer.h"

#include <stdexcept>
#include <string>
#include <utility>

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

}  // namespace

DeserializeError::DeserializeError(std::string message, std::string json_pointer,
                                   std::optional<int> line,
                                   std::optional<int> column)
    : std::runtime_error(message),
      message_(std::move(message)),
      json_pointer_(std::move(json_pointer)),
      line_(line),
      column_(column) {}

nlohmann::json program_to_json(const Program& /*program*/) {
    throw std::logic_error("program_to_json: not implemented yet");
}

Program program_from_json(const nlohmann::json& /*root*/) {
    throw std::logic_error("program_from_json: not implemented yet");
}

bool programs_equal(const Program& /*a*/, const Program& /*b*/) {
    throw std::logic_error("programs_equal: not implemented yet");
}

}  // namespace spudplate
