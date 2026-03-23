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

TEST(LexerTest, AllKeywords) {
    struct Case {
        const char *input;
        TokenType expected;
    };
    Case cases[] = {
        {"ask", TokenType::ASK},
        {"let", TokenType::LET},
        {"mkdir", TokenType::MKDIR},
        {"file", TokenType::FILE},
        {"from", TokenType::FROM},
        {"content", TokenType::CONTENT},
        {"when", TokenType::WHEN},
        {"repeat", TokenType::REPEAT},
        {"end", TokenType::END},
        {"required", TokenType::REQUIRED},
        {"verbatim", TokenType::VERBATIM},
        {"mode", TokenType::MODE},
        {"as", TokenType::AS},
        {"and", TokenType::AND},
        {"or", TokenType::OR},
        {"not", TokenType::NOT},
        {"string", TokenType::STRING_TYPE},
        {"bool", TokenType::BOOL_TYPE},
        {"int", TokenType::INT_TYPE},
    };
    for (const auto &c : cases) {
        Lexer lexer(c.input);
        Token tok = lexer.nextToken();
        EXPECT_EQ(tok.type, c.expected)
            << "Failed for keyword: " << c.input;
        EXPECT_EQ(tok.value, c.input);
        EXPECT_EQ(tok.line, 1);
        EXPECT_EQ(tok.column, 1);
    }
}

TEST(LexerTest, Identifier) {
    Lexer lexer("my_var");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.value, "my_var");
}

TEST(LexerTest, IdentifierWithDigits) {
    Lexer lexer("name2");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.value, "name2");
}

TEST(LexerTest, UnderscoreIdentifier) {
    Lexer lexer("_private");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.value, "_private");
}

TEST(LexerTest, KeywordsCaseSensitive) {
    Lexer lexer("Ask");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.value, "Ask");
}

TEST(LexerTest, MultipleTokens) {
    Lexer lexer("ask name");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);
    EXPECT_EQ(tok1.column, 1);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok2.value, "name");
    EXPECT_EQ(tok2.column, 5);
}

TEST(LexerTest, KeywordPrefixIsIdentifier) {
    Lexer lexer("asking");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok.value, "asking");
}
