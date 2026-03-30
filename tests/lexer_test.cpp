#include "spudplate/lexer.h"

#include <gtest/gtest.h>

#include "spudplate/token.h"

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
        const char* input;
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
    for (const auto& c : cases) {
        Lexer lexer(c.input);
        Token tok = lexer.nextToken();
        EXPECT_EQ(tok.type, c.expected) << "Failed for keyword: " << c.input;
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

TEST(LexerTest, SimpleString) {
    Lexer lexer("\"hello\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok.value, "hello");
    EXPECT_EQ(tok.line, 1);
    EXPECT_EQ(tok.column, 1);
}

TEST(LexerTest, EmptyString) {
    Lexer lexer("\"\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok.value, "");
}

TEST(LexerTest, StringWithSpaces) {
    Lexer lexer("\"hello world\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok.value, "hello world");
}

TEST(LexerTest, UnterminatedString) {
    Lexer lexer("\"hello");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_EQ(tok.value, "unterminated string");
    EXPECT_EQ(tok.line, 1);
    EXPECT_EQ(tok.column, 1);
}

TEST(LexerTest, StringThenKeyword) {
    Lexer lexer("\"path\" file");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok1.value, "path");

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::FILE);
}

TEST(LexerTest, StringColumnTracking) {
    Lexer lexer("  \"hi\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok.column, 3);
}

TEST(LexerTest, SimpleInteger) {
    Lexer lexer("42");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tok.value, "42");
    EXPECT_EQ(tok.line, 1);
    EXPECT_EQ(tok.column, 1);
}

TEST(LexerTest, MultiDigitInteger) {
    Lexer lexer("12345");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tok.value, "12345");
}

TEST(LexerTest, SingleDigitZero) {
    Lexer lexer("0");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tok.value, "0");
}

TEST(LexerTest, IntegerThenKeyword) {
    Lexer lexer("100 let");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tok1.value, "100");

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::LET);
}

TEST(LexerTest, IntegerColumnTracking) {
    Lexer lexer("  99");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tok.column, 3);
}

TEST(LexerTest, SingleCharOperators) {
    struct Case {
        const char* input;
        TokenType expected;
        const char* value;
    };
    Case cases[] = {
        {"+", TokenType::PLUS, "+"},   {"-", TokenType::MINUS, "-"},
        {"*", TokenType::STAR, "*"},   {"/", TokenType::SLASH, "/"},
        {"(", TokenType::LPAREN, "("}, {")", TokenType::RPAREN, ")"},
    };
    for (const auto& c : cases) {
        Lexer lexer(c.input);
        Token tok = lexer.nextToken();
        EXPECT_EQ(tok.type, c.expected) << "Failed for: " << c.input;
        EXPECT_EQ(tok.value, c.value);
        EXPECT_EQ(tok.line, 1);
        EXPECT_EQ(tok.column, 1);
    }
}

TEST(LexerTest, AssignOperator) {
    Lexer lexer("=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ASSIGN);
    EXPECT_EQ(tok.value, "=");
}

TEST(LexerTest, EqualsOperator) {
    Lexer lexer("==");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::EQUALS);
    EXPECT_EQ(tok.value, "==");
}

TEST(LexerTest, NotEqualsOperator) {
    Lexer lexer("!=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NOT_EQUALS);
    EXPECT_EQ(tok.value, "!=");
}

TEST(LexerTest, BangAloneIsError) {
    Lexer lexer("!");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::ERROR);
    EXPECT_EQ(tok.value, "!");
}

TEST(LexerTest, GreaterOperator) {
    Lexer lexer(">");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::GREATER);
    EXPECT_EQ(tok.value, ">");
}

TEST(LexerTest, GreaterEqualOperator) {
    Lexer lexer(">=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::GREATER_EQUAL);
    EXPECT_EQ(tok.value, ">=");
}

TEST(LexerTest, LessOperator) {
    Lexer lexer("<");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::LESS);
    EXPECT_EQ(tok.value, "<");
}

TEST(LexerTest, LessEqualOperator) {
    Lexer lexer("<=");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::LESS_EQUAL);
    EXPECT_EQ(tok.value, "<=");
}

TEST(LexerTest, OperatorColumnTracking) {
    Lexer lexer("  + ==");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::PLUS);
    EXPECT_EQ(tok1.column, 3);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::EQUALS);
    EXPECT_EQ(tok2.column, 5);
}

TEST(LexerTest, OperatorsInExpression) {
    Lexer lexer("x >= 10");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok1.value, "x");

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::GREATER_EQUAL);

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tok3.value, "10");
}

TEST(LexerTest, NewlineToken) {
    Lexer lexer("\n");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::NEWLINE);
    EXPECT_EQ(tok.value, "\\n");
    EXPECT_EQ(tok.line, 1);
    EXPECT_EQ(tok.column, 1);
}

TEST(LexerTest, NewlineBetweenTokens) {
    Lexer lexer("ask\nname");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::NEWLINE);
    EXPECT_EQ(tok2.line, 1);

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok3.value, "name");
    EXPECT_EQ(tok3.line, 2);
    EXPECT_EQ(tok3.column, 1);
}

TEST(LexerTest, MultipleNewlines) {
    Lexer lexer("\n\n");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::NEWLINE);
    EXPECT_EQ(tok1.line, 1);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::NEWLINE);
    EXPECT_EQ(tok2.line, 2);

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, CommentSkipped) {
    Lexer lexer("# this is a comment");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, CommentBeforeNewline) {
    Lexer lexer("# comment\nask");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::NEWLINE);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::ASK);
}

TEST(LexerTest, TokenBeforeComment) {
    Lexer lexer("ask # get name");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, CommentDoesNotConsumeNewline) {
    Lexer lexer("ask # comment\nlet");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::NEWLINE);

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::LET);
}

TEST(LexerTest, LineContinuationTwoLines) {
    Lexer lexer("ask \\\nname");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok2.value, "name");

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, LineContinuationThreeLines) {
    Lexer lexer("ask \\\n\"prompt\" \\\nstring");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok2.value, "prompt");

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::STRING_TYPE);

    Token tok4 = lexer.nextToken();
    EXPECT_EQ(tok4.type, TokenType::EOF_TOKEN);
}

TEST(LexerTest, LineContinuationNoNewline) {
    // Backslash not followed by newline is an error token
    Lexer lexer("ask \\name");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::ERROR);
    EXPECT_EQ(tok2.value, "\\");
}

TEST(LexerTest, LineContinuationTrailingWhitespace) {
    // Trailing spaces/tabs between `\` and newline are silently ignored
    Lexer lexer("ask \\  \t\nname");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok2.value, "name");
}

TEST(LexerTest, LineContinuationInsideStringIsNotContinuation) {
    // A backslash inside a string literal is just a character, not continuation
    Lexer lexer("\"hello\\nworld\"");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok.value, "hello\\nworld");
}

TEST(LexerTest, LineContinuationLineTracking) {
    // After a continuation, tokens on the next physical line have correct line numbers
    Lexer lexer("ask \\\nname \\\n\"prompt\"");
    Token tok1 = lexer.nextToken();
    EXPECT_EQ(tok1.type, TokenType::ASK);
    EXPECT_EQ(tok1.line, 1);

    Token tok2 = lexer.nextToken();
    EXPECT_EQ(tok2.type, TokenType::IDENTIFIER);
    EXPECT_EQ(tok2.line, 2);

    Token tok3 = lexer.nextToken();
    EXPECT_EQ(tok3.type, TokenType::STRING_LITERAL);
    EXPECT_EQ(tok3.line, 3);
}
