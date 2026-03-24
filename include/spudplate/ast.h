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

// Variable types for ask statements
enum class VarType { String, Bool, Int };

// File source: either a path (from) or an expression (content)
struct FileFromSource {
    std::string path;
    bool verbatim;
};

struct FileContentSource {
    ExprPtr value;
};

using FileSource = std::variant<FileFromSource, FileContentSource>;

// Statement types

struct AskStmt {
    std::string name;
    std::string prompt;
    VarType var_type;
    bool required;
    std::optional<ExprPtr> when_clause;
    int line;
    int column;
};

struct LetStmt {
    std::string name;
    ExprPtr value;
    int line;
    int column;
};

struct MkdirStmt {
    std::string path;
    std::optional<int> mode;
    std::optional<ExprPtr> when_clause;
    int line;
    int column;
};

struct FileStmt {
    std::string path;
    FileSource source;
    std::optional<int> mode;
    std::optional<ExprPtr> when_clause;
    int line;
    int column;
};

// Forward declare RepeatStmt — defined in branch 3
struct RepeatStmt;

// Stmt variant and pointer type
using Stmt =
    std::variant<AskStmt, LetStmt, MkdirStmt, FileStmt>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Program {
    // Populated in branch 3
};

} // namespace spudplate

#endif // SPUDPLATE_AST_H
