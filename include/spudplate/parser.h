#ifndef SPUDPLATE_PARSER_H
#define SPUDPLATE_PARSER_H

#include "spudplate/ast.h"
#include "spudplate/lexer.h"

#include <stdexcept>
#include <string>

namespace spudplate {

class ParseError : public std::runtime_error {
  public:
    ParseError(const std::string &message, int line, int column)
        : std::runtime_error(message), line_(line), column_(column) {}

    int line() const { return line_; }
    int column() const { return column_; }

  private:
    int line_;
    int column_;
};

class Parser {
  public:
    explicit Parser(Lexer lexer);

    Program parse();

    ExprPtr parseExpression();

    StmtPtr parseAsk();
    StmtPtr parseLet();
    StmtPtr parseMkdir();
    StmtPtr parseFile();
    StmtPtr parseRepeat();
    StmtPtr parseStatement();

  private:
    Lexer lexer_;
    Token current_;

    // Token consumption
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    Token expect(TokenType type, const std::string &message);
    void skip_newlines();

    // Statement helpers
    VarType parse_var_type();
    std::optional<ExprPtr> parse_when_clause();
    std::optional<int> parse_mode_clause();
    void expect_newline(const std::string &context);

    // Expression precedence climbing
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_comparison();
    ExprPtr parse_addition();
    ExprPtr parse_multiplication();
    ExprPtr parse_unary();
    ExprPtr parse_primary();
};

} // namespace spudplate

#endif // SPUDPLATE_PARSER_H
