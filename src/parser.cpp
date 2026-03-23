#include "spudplate/parser.h"

namespace spudplate {

Parser::Parser(Lexer lexer) : lexer_(std::move(lexer)) {
    current_ = lexer_.nextToken();
}

Token Parser::advance() {
    Token prev = current_;
    current_ = lexer_.nextToken();
    return prev;
}

bool Parser::check(TokenType type) const { return current_.type == type; }

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenType type, const std::string &message) {
    if (check(type)) {
        return advance();
    }
    throw ParseError(message, current_.line, current_.column);
}

void Parser::skip_newlines() {
    while (check(TokenType::NEWLINE)) {
        advance();
    }
}

// Expression precedence: or < and < comparison < addition < multiplication <
// unary < primary

ExprPtr Parser::parseExpression() { return parse_or(); }

ExprPtr Parser::parse_or() {
    auto left = parse_and();

    while (check(TokenType::OR)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_and();

        auto expr = std::make_unique<Expr>();
        expr->data = BinaryExpr{TokenType::OR, std::move(left), std::move(right),
                                line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_and() {
    auto left = parse_comparison();

    while (check(TokenType::AND)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_comparison();

        auto expr = std::make_unique<Expr>();
        expr->data = BinaryExpr{TokenType::AND, std::move(left),
                                std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_comparison() {
    auto left = parse_addition();

    while (check(TokenType::EQUALS) || check(TokenType::NOT_EQUALS) ||
           check(TokenType::GREATER) || check(TokenType::LESS) ||
           check(TokenType::GREATER_EQUAL) || check(TokenType::LESS_EQUAL)) {
        TokenType op = current_.type;
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_addition();

        auto expr = std::make_unique<Expr>();
        expr->data =
            BinaryExpr{op, std::move(left), std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_addition() {
    auto left = parse_multiplication();

    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        TokenType op = current_.type;
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_multiplication();

        auto expr = std::make_unique<Expr>();
        expr->data =
            BinaryExpr{op, std::move(left), std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_multiplication() {
    auto left = parse_unary();

    while (check(TokenType::STAR) || check(TokenType::SLASH)) {
        TokenType op = current_.type;
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_unary();

        auto expr = std::make_unique<Expr>();
        expr->data =
            BinaryExpr{op, std::move(left), std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenType::NOT)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto operand = parse_unary();

        auto expr = std::make_unique<Expr>();
        expr->data = UnaryExpr{TokenType::NOT, std::move(operand), line, col};
        return expr;
    }

    return parse_primary();
}

ExprPtr Parser::parse_primary() {
    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data = StringLiteralExpr{tok.value, tok.line, tok.column};
        return expr;
    }

    if (check(TokenType::INTEGER_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data =
            IntegerLiteralExpr{std::stoi(tok.value), tok.line, tok.column};
        return expr;
    }

    if (check(TokenType::IDENTIFIER)) {
        Token tok = advance();

        // Check for function call: name(expr)
        if (check(TokenType::LPAREN)) {
            advance();
            auto arg = parseExpression();
            expect(TokenType::RPAREN, "expected ')' after function argument");

            auto expr = std::make_unique<Expr>();
            expr->data = FunctionCallExpr{tok.value, std::move(arg), tok.line,
                                          tok.column};
            return expr;
        }

        auto expr = std::make_unique<Expr>();
        expr->data = IdentifierExpr{tok.value, tok.line, tok.column};
        return expr;
    }

    if (check(TokenType::LPAREN)) {
        advance();
        auto inner = parseExpression();
        expect(TokenType::RPAREN, "expected ')' after expression");
        return inner;
    }

    throw ParseError("unexpected token: " + current_.value, current_.line,
                     current_.column);
}

} // namespace spudplate
