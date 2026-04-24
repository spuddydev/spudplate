#include "spudplate/validator.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/token.h"

using spudplate::BinaryExpr;
using spudplate::BoolLiteralExpr;
using spudplate::clone_expr;
using spudplate::Expr;
using spudplate::ExprData;
using spudplate::ExprPtr;
using spudplate::exprs_equal;
using spudplate::FunctionCallExpr;
using spudplate::IdentifierExpr;
using spudplate::IntegerLiteralExpr;
using spudplate::Lexer;
using spudplate::normalize;
using spudplate::Parser;
using spudplate::Program;
using spudplate::SemanticError;
using spudplate::StringLiteralExpr;
using spudplate::TokenType;
using spudplate::TypeMap;
using spudplate::UnaryExpr;
using spudplate::validate;
using spudplate::VarType;

// AST builder helpers used by the normalization tests.
static ExprPtr wrap(ExprData data, int line = 1, int column = 1) {
    auto e = std::make_unique<Expr>();
    e->data = std::move(data);
    (void)line;
    (void)column;
    return e;
}

static ExprPtr id(const std::string& name, int line = 1, int column = 1) {
    return wrap(IdentifierExpr{.name = name, .line = line, .column = column});
}

static ExprPtr bool_lit(bool value, int line = 1, int column = 1) {
    return wrap(BoolLiteralExpr{.value = value, .line = line, .column = column});
}

static ExprPtr int_lit(int value, int line = 1, int column = 1) {
    return wrap(IntegerLiteralExpr{.value = value, .line = line, .column = column});
}

static ExprPtr str_lit(const std::string& value, int line = 1, int column = 1) {
    return wrap(StringLiteralExpr{.value = value, .line = line, .column = column});
}

static ExprPtr not_of(ExprPtr operand) {
    return wrap(UnaryExpr{.op = TokenType::NOT,
                          .operand = std::move(operand),
                          .line = 1,
                          .column = 1});
}

static ExprPtr bin(TokenType op, ExprPtr left, ExprPtr right) {
    return wrap(BinaryExpr{.op = op,
                           .left = std::move(left),
                           .right = std::move(right),
                           .line = 1,
                           .column = 1});
}

static ExprPtr call(const std::string& name, ExprPtr arg) {
    return wrap(FunctionCallExpr{
        .name = name, .argument = std::move(arg), .line = 1, .column = 1});
}

// Equivalent under the given type map iff normalized forms compare equal.
static bool equivalent(const Expr& a, const Expr& b, const TypeMap& tm) {
    auto na = normalize(a, tm);
    auto nb = normalize(b, tm);
    return exprs_equal(*na, *nb);
}

static Program parse(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parse();
}

// --- Validator skeleton tests ---

TEST(ValidatorTest, EmptyProgramValidatesCleanly) {
    Program empty;
    EXPECT_NO_THROW(validate(empty));
}

TEST(ValidatorTest, AskAtTopLevelThenRepeatValidates) {
    auto program = parse(
        "ask name \"Name?\" string\n"
        "repeat n as i\n"
        "  mkdir \"m_{i}\"\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, EmptyRepeatBodyValidates) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AskInsideSingleRepeatIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  ask use_extra \"Extra?\" bool\n"
        "end\n");
    try {
        validate(program);
        FAIL() << "expected SemanticError";
    } catch (const SemanticError& e) {
        // The offending ask is on line 3 (ask n ... is line 1, repeat is line 2).
        EXPECT_EQ(e.line(), 3);
    }
}

TEST(ValidatorTest, AskInsideNestedRepeatPointsAtAskLine) {
    auto program = parse(
        "ask n \"Count?\" int\n"       // line 1
        "ask m \"Count?\" int\n"       // line 2
        "repeat n as i\n"              // line 3 (outer)
        "  repeat m as j\n"            // line 4 (inner)
        "    ask bad \"bad?\" bool\n"  // line 5 — the offender
        "  end\n"
        "end\n");
    try {
        validate(program);
        FAIL() << "expected SemanticError";
    } catch (const SemanticError& e) {
        EXPECT_EQ(e.line(), 5);
    }
}

TEST(ValidatorTest, RepeatBodyWithNonAskStatementsValidates) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "mkdir base\n"
        "repeat n as i\n"
        "  mkdir \"m_{i}\"\n"
        "  file \"m_{i}/README.md\" content \"hi\"\n"
        "  let x = i + 1\n"
        "  repeat n as j\n"
        "    mkdir \"inner_{j}\"\n"
        "  end\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

// --- Scope stack tests ---

TEST(ValidatorTest, LetInsideRepeatReferencedOutsideIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  let x = i\n"
        "end\n"
        "let y = x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, IteratorVarReferencedOutsideIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "end\n"
        "let y = i\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AliasBoundInsideRepeatReferencedOutsideIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  mkdir \"foo\" as bar\n"
        "end\n"
        "mkdir bar/sub\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, NestedRepeatsIsolateInnerScope) {
    // Inner-declared `x` must not be visible to the outer body after inner pops.
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  repeat n as j\n"
        "    let x = 1\n"
        "  end\n"
        "  let y = x\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, OuterNamesVisibleFromInsideNestedRepeat) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "let base = 5\n"
        "repeat n as i\n"
        "  repeat n as j\n"
        "    let x = base + i + j\n"
        "  end\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, SameScopeReferencesInsideRepeatAreValid) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  let x = i\n"
        "  let y = x\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ShadowingTopLevelLetFromInsideRepeatIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "let x = 1\n"
        "repeat n as i\n"
        "  let x = i\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, WhenClauseReferencingPoppedBindingIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  let x = 1\n"
        "end\n"
        "mkdir stuff when x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, PathInterpReferencingPoppedIteratorIsError) {
    // Unquoted path so `{i}` is parsed as a PathInterp segment.
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "end\n"
        "mkdir week_{i}\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, CollectionVarReferencingPoppedNameIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as k\n"
        "  let x = 1\n"
        "end\n"
        "repeat x as j\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

// --- Expression normalization tests ---

TEST(NormalizeTest, BoolIdEqualsTrueCollapsesToId) {
    TypeMap tm{{"x", VarType::Bool}};
    auto a = id("x");
    auto b = bin(TokenType::EQUALS, id("x"), bool_lit(true));
    EXPECT_TRUE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, NotBoolIdEqualsXEqualsFalse) {
    TypeMap tm{{"x", VarType::Bool}};
    auto a = not_of(id("x"));
    auto b = bin(TokenType::EQUALS, id("x"), bool_lit(false));
    EXPECT_TRUE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, NotBoolIdEqualsXNotEqualsTrue) {
    TypeMap tm{{"x", VarType::Bool}};
    auto a = not_of(id("x"));
    auto b = bin(TokenType::NOT_EQUALS, id("x"), bool_lit(true));
    EXPECT_TRUE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, NotNotBoolIdCollapsesToId) {
    TypeMap tm{{"x", VarType::Bool}};
    auto a = id("x");
    auto b = not_of(not_of(id("x")));
    EXPECT_TRUE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, ArbitraryDepthNotNormalizes) {
    TypeMap tm{{"x", VarType::Bool}};
    auto a = not_of(id("x"));  // canonical negative
    auto b = not_of(not_of(not_of(id("x"))));  // three nots collapses to one
    EXPECT_TRUE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, RecursesUnderFunctionAndComparison) {
    TypeMap tm{{"x", VarType::Bool}};
    // lower(not not x) == "y"   vs   lower(x) == "y"
    auto a = bin(TokenType::EQUALS,
                 call("lower", not_of(not_of(id("x")))), str_lit("y"));
    auto b = bin(TokenType::EQUALS, call("lower", id("x")), str_lit("y"));
    EXPECT_TRUE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, LineColumnIgnoredInEquality) {
    // Two identifiers with the same name at wildly different positions.
    auto a = id("x", 1, 1);
    auto b = id("x", 42, 7);
    EXPECT_TRUE(exprs_equal(*a, *b));
}

TEST(NormalizeTest, FunctionCallRecursiveEquality) {
    auto a = call("lower", id("a"));
    auto b = call("lower", id("a"));
    EXPECT_TRUE(exprs_equal(*a, *b));
}

TEST(NormalizeTest, DifferentIdentifiersNotEqual) {
    TypeMap tm{{"x", VarType::Bool}, {"y", VarType::Bool}};
    auto a = id("x");
    auto b = id("y");
    EXPECT_FALSE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, PositiveAndNegativeBoolNotEqual) {
    TypeMap tm{{"x", VarType::Bool}};
    auto a = id("x");
    auto b = not_of(id("x"));
    EXPECT_FALSE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, CommutativityNotApplied) {
    // `a and b` vs `b and a` — known limitation per TODO.
    TypeMap tm{{"a", VarType::Bool}, {"b", VarType::Bool}};
    auto left = bin(TokenType::AND, id("a"), id("b"));
    auto right = bin(TokenType::AND, id("b"), id("a"));
    EXPECT_FALSE(equivalent(*left, *right, tm));
}

TEST(NormalizeTest, UnknownIdentifierKeepsStructuralForm) {
    // `x` is NOT in the type map — bool simplification skipped.
    // `x == true` stays as the binary form; `x` stays as the identifier.
    TypeMap tm;  // empty
    auto a = id("x");
    auto b = bin(TokenType::EQUALS, id("x"), bool_lit(true));
    EXPECT_FALSE(equivalent(*a, *b, tm));
}

TEST(NormalizeTest, IntComparisonDiffersFromBoolComparison) {
    TypeMap tm{{"x", VarType::Bool}, {"n", VarType::Int}};
    auto a = bin(TokenType::EQUALS, id("n"), int_lit(1));
    auto b = bin(TokenType::EQUALS, id("x"), bool_lit(true));
    EXPECT_FALSE(equivalent(*a, *b, tm));
}
