#ifndef SPUDPLATE_AST_H
#define SPUDPLATE_AST_H

#include "spudplate/token.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace spudplate {

struct StringLiteralExpr {
    std::string value;
    int line;
    int column;
};

struct IntegerLiteralExpr {
    int value;
    int line;
    int column;
};

struct IdentifierExpr {
    std::string name;
    int line;
    int column;
};

// Forward declare Expr so recursive types can hold pointers to it
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct UnaryExpr {
    TokenType op;
    ExprPtr operand;
    int line;
    int column;
};

struct BinaryExpr {
    TokenType op;
    ExprPtr left;
    ExprPtr right;
    int line;
    int column;
};

struct FunctionCallExpr {
    std::string name;
    ExprPtr argument;
    int line;
    int column;
};

using ExprData = std::variant<StringLiteralExpr, IntegerLiteralExpr,
                              IdentifierExpr, UnaryExpr, BinaryExpr,
                              FunctionCallExpr>;

struct Expr {
    ExprData data;
};

// Statement types — stubs for branch 2
struct AskStmt;
struct LetStmt;
struct MkdirStmt;
struct FileStmt;
struct RepeatStmt;

struct Program {
    // Populated in branch 3
};

} // namespace spudplate

#endif // SPUDPLATE_AST_H
