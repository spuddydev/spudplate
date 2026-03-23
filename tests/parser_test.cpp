#include "spudplate/ast.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"

#include <gtest/gtest.h>

using spudplate::BinaryExpr;
using spudplate::Expr;
using spudplate::ExprPtr;
using spudplate::FunctionCallExpr;
using spudplate::IdentifierExpr;
using spudplate::IntegerLiteralExpr;
using spudplate::Lexer;
using spudplate::ParseError;
using spudplate::Parser;
using spudplate::StringLiteralExpr;
using spudplate::TokenType;
using spudplate::UnaryExpr;

static ExprPtr parse_expr(const std::string &input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parseExpression();
}

TEST(ParserTest, StringLiteral) {
    auto expr = parse_expr("\"hello\"");
    auto &lit = std::get<StringLiteralExpr>(expr->data);
    EXPECT_EQ(lit.value, "hello");
    EXPECT_EQ(lit.line, 1);
    EXPECT_EQ(lit.column, 1);
}

TEST(ParserTest, IntegerLiteral) {
    auto expr = parse_expr("42");
    auto &lit = std::get<IntegerLiteralExpr>(expr->data);
    EXPECT_EQ(lit.value, 42);
}

TEST(ParserTest, Identifier) {
    auto expr = parse_expr("my_var");
    auto &id = std::get<IdentifierExpr>(expr->data);
    EXPECT_EQ(id.name, "my_var");
}

TEST(ParserTest, BinaryAddition) {
    auto expr = parse_expr("1 + 2");
    auto &bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::PLUS);

    auto &left = std::get<IntegerLiteralExpr>(bin.left->data);
    EXPECT_EQ(left.value, 1);

    auto &right = std::get<IntegerLiteralExpr>(bin.right->data);
    EXPECT_EQ(right.value, 2);
}

TEST(ParserTest, BinaryMultiplication) {
    auto expr = parse_expr("3 * 4");
    auto &bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::STAR);
}

TEST(ParserTest, PrecedenceMulOverAdd) {
    // 1 + 2 * 3 should parse as 1 + (2 * 3)
    auto expr = parse_expr("1 + 2 * 3");
    auto &add = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(add.op, TokenType::PLUS);

    auto &left = std::get<IntegerLiteralExpr>(add.left->data);
    EXPECT_EQ(left.value, 1);

    auto &mul = std::get<BinaryExpr>(add.right->data);
    EXPECT_EQ(mul.op, TokenType::STAR);
}

TEST(ParserTest, PrecedenceSubDiv) {
    // 10 - 5 / 2 should parse as 10 - (5 / 2)
    auto expr = parse_expr("10 - 5 / 2");
    auto &sub = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(sub.op, TokenType::MINUS);

    auto &div = std::get<BinaryExpr>(sub.right->data);
    EXPECT_EQ(div.op, TokenType::SLASH);
}

TEST(ParserTest, Comparison) {
    auto expr = parse_expr("x > 5");
    auto &cmp = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(cmp.op, TokenType::GREATER);

    auto &left = std::get<IdentifierExpr>(cmp.left->data);
    EXPECT_EQ(left.name, "x");

    auto &right = std::get<IntegerLiteralExpr>(cmp.right->data);
    EXPECT_EQ(right.value, 5);
}

TEST(ParserTest, EqualityComparison) {
    auto expr = parse_expr("name == \"foo\"");
    auto &cmp = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(cmp.op, TokenType::EQUALS);
}

TEST(ParserTest, LogicalAnd) {
    auto expr = parse_expr("a and b");
    auto &bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::AND);
}

TEST(ParserTest, LogicalOr) {
    auto expr = parse_expr("a or b");
    auto &bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::OR);
}

TEST(ParserTest, UnaryNot) {
    auto expr = parse_expr("not x");
    auto &un = std::get<UnaryExpr>(expr->data);
    EXPECT_EQ(un.op, TokenType::NOT);

    auto &operand = std::get<IdentifierExpr>(un.operand->data);
    EXPECT_EQ(operand.name, "x");
}

TEST(ParserTest, CombinedPrecedence) {
    // a + b > c and d → (((a + b) > c) and d)
    auto expr = parse_expr("a + b > c and d");
    auto &and_expr = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(and_expr.op, TokenType::AND);

    auto &cmp = std::get<BinaryExpr>(and_expr.left->data);
    EXPECT_EQ(cmp.op, TokenType::GREATER);

    auto &add = std::get<BinaryExpr>(cmp.left->data);
    EXPECT_EQ(add.op, TokenType::PLUS);
}

TEST(ParserTest, FunctionCall) {
    auto expr = parse_expr("lower(name)");
    auto &call = std::get<FunctionCallExpr>(expr->data);
    EXPECT_EQ(call.name, "lower");

    auto &arg = std::get<IdentifierExpr>(call.argument->data);
    EXPECT_EQ(arg.name, "name");
}

TEST(ParserTest, FunctionCallUpper) {
    auto expr = parse_expr("upper(x)");
    auto &call = std::get<FunctionCallExpr>(expr->data);
    EXPECT_EQ(call.name, "upper");
}

TEST(ParserTest, Parenthesized) {
    // (a + b) * c → should multiply, not add at top
    auto expr = parse_expr("(a + b) * c");
    auto &mul = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(mul.op, TokenType::STAR);

    auto &add = std::get<BinaryExpr>(mul.left->data);
    EXPECT_EQ(add.op, TokenType::PLUS);
}

TEST(ParserTest, StringConcat) {
    auto expr = parse_expr("\"hello\" + \" \" + name");
    auto &outer = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(outer.op, TokenType::PLUS);

    // Left-associative: ("hello" + " ") + name
    auto &inner = std::get<BinaryExpr>(outer.left->data);
    EXPECT_EQ(inner.op, TokenType::PLUS);
}

TEST(ParserTest, ErrorUnexpectedToken) {
    EXPECT_THROW(parse_expr("+"), ParseError);
}

TEST(ParserTest, ErrorMissingCloseParen) {
    EXPECT_THROW(parse_expr("(1 + 2"), ParseError);
}
