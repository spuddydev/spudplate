#include "spudplate/lexer.h"
#include "spudplate/token.h"

#include <gtest/gtest.h>

using spudplate::Lexer;
using spudplate::Token;
using spudplate::TokenType;
using spudplate::tokenTypeToString;

TEST(TokenTest, Construction) {
    Token tok(TokenType::ASK, "ask", 5, 3);
    EXPECT_EQ(tok.type, TokenType::ASK);
    EXPECT_EQ(tok.value, "ask");
    EXPECT_EQ(tok.line, 5);
    EXPECT_EQ(tok.column, 3);
}

TEST(TokenTest, DefaultConstructor) {
    Token tok;
    EXPECT_EQ(tok.type, TokenType::EOF_TOKEN);
    EXPECT_EQ(tok.value, "");
    EXPECT_EQ(tok.line, 0);
    EXPECT_EQ(tok.column, 0);
}

TEST(TokenTest, TypeToString) {
    EXPECT_EQ(tokenTypeToString(TokenType::ASK), "ASK");
    EXPECT_EQ(tokenTypeToString(TokenType::EOF_TOKEN), "EOF_TOKEN");
    EXPECT_EQ(tokenTypeToString(TokenType::ERROR), "ERROR");
    EXPECT_EQ(tokenTypeToString(TokenType::STRING_LITERAL), "STRING_LITERAL");
    EXPECT_EQ(tokenTypeToString(TokenType::ASSIGN), "ASSIGN");
}

TEST(LexerTest, EmptyInput) {
    Lexer lexer("");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::EOF_TOKEN);
    EXPECT_EQ(tok.line, 1);
    EXPECT_EQ(tok.column, 1);
}

TEST(LexerTest, WhitespaceOnly) {
    Lexer lexer("   \t  ");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, EofIsIdempotent) {
    Lexer lexer("");
    Token tok1 = lexer.nextToken();
    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::EOF_TOKEN);
    EXPECT_EQ(tok2.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, UnrecognizedCharReturnsError) {
    Lexer lexer("@");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_EQ(tok.value, "@");
    EXPECT_EQ(tok.line, 1);
    EXPECT_EQ(tok.column, 1);

    Token eof = lexer.nextToken();
    EXPECT_EQ(eof.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, WhitespaceBeforeUnrecognized) {
    Lexer lexer("  @");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_EQ(tok.value, "@");
    EXPECT_EQ(tok.column, 3);
}
