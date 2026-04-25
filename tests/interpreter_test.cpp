#include "spudplate/interpreter.h"

#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/token.h"

using spudplate::BinaryExpr;
using spudplate::BoolLiteralExpr;
using spudplate::Environment;
using spudplate::evaluate_expr;
using spudplate::Expr;
using spudplate::ExprData;
using spudplate::ExprPtr;
using spudplate::FunctionCallExpr;
using spudplate::IdentifierExpr;
using spudplate::IntegerLiteralExpr;
using spudplate::Lexer;
using spudplate::Parser;
using spudplate::Program;
using spudplate::Prompter;
using spudplate::run;
using spudplate::run_for_tests;
using spudplate::RuntimeError;
using spudplate::StringLiteralExpr;
using spudplate::TokenType;
using spudplate::UnaryExpr;
using spudplate::Value;
using spudplate::value_to_string;
using spudplate::VarType;

namespace {

// Test stub — never asked, never answered. Used in Part 1 where every
// statement throws "not yet supported" before the prompter is reached.
class NullPrompter : public Prompter {
  public:
    std::string prompt(const std::string& /*message*/,
                       VarType /*type*/) override {
        throw std::logic_error("NullPrompter should not be called");
    }
};

Program parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(std::move(lexer));
    return parser.parse();
}

// AST builder helpers for expression-level tests.
ExprPtr wrap(ExprData data) {
    auto e = std::make_unique<Expr>();
    e->data = std::move(data);
    return e;
}
ExprPtr str_lit(const std::string& v) {
    return wrap(StringLiteralExpr{.value = v, .line = 1, .column = 1});
}
ExprPtr int_lit(int v) {
    return wrap(IntegerLiteralExpr{.value = v, .line = 1, .column = 1});
}
ExprPtr bool_lit(bool v) {
    return wrap(BoolLiteralExpr{.value = v, .line = 1, .column = 1});
}
ExprPtr ident(const std::string& name) {
    return wrap(IdentifierExpr{.name = name, .line = 1, .column = 1});
}
ExprPtr unary_not(ExprPtr operand) {
    return wrap(UnaryExpr{.op = TokenType::NOT,
                          .operand = std::move(operand),
                          .line = 1,
                          .column = 1});
}
ExprPtr binop(TokenType op, ExprPtr left, ExprPtr right) {
    return wrap(BinaryExpr{.op = op,
                           .left = std::move(left),
                           .right = std::move(right),
                           .line = 1,
                           .column = 1});
}
ExprPtr fn(const std::string& name, ExprPtr arg) {
    return wrap(FunctionCallExpr{.name = name,
                                 .argument = std::move(arg),
                                 .line = 1,
                                 .column = 1});
}

}  // namespace

// --- RuntimeError shape ---

TEST(RuntimeErrorTest, RoundTripsMessageAndPosition) {
    RuntimeError e("boom", 3, 7);
    EXPECT_STREQ(e.what(), "boom");
    EXPECT_EQ(e.line(), 3);
    EXPECT_EQ(e.column(), 7);
}

// --- Environment primitives ---

TEST(EnvironmentTest, DeclareAndLookupString) {
    Environment env;
    env.declare("name", Value{std::string{"hello"}});
    auto v = env.lookup("name");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::string>(*v), "hello");
}

TEST(EnvironmentTest, DeclareAndLookupInt) {
    Environment env;
    env.declare("n", Value{std::int64_t{42}});
    auto v = env.lookup("n");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*v), 42);
}

TEST(EnvironmentTest, DeclareAndLookupBool) {
    Environment env;
    env.declare("flag", Value{true});
    auto v = env.lookup("flag");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<bool>(*v), true);
}

TEST(EnvironmentTest, LookupReturnsNulloptForUnknown) {
    Environment env;
    EXPECT_FALSE(env.lookup("missing").has_value());
}

TEST(EnvironmentTest, DeclareRejectsShadow) {
    Environment env;
    env.declare("x", Value{std::int64_t{1}});
    EXPECT_THROW(env.declare("x", Value{std::int64_t{2}}), std::logic_error);
}

TEST(EnvironmentTest, NestedFrameSeesOuterBinding) {
    Environment env;
    env.declare("outer", Value{std::int64_t{1}});
    env.push();
    auto v = env.lookup("outer");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*v), 1);
}

TEST(EnvironmentTest, PoppedFrameLosesItsBindings) {
    Environment env;
    env.push();
    env.declare("inner", Value{std::int64_t{1}});
    env.pop();
    EXPECT_FALSE(env.lookup("inner").has_value());
}

// --- run() on empty program ---

TEST(InterpreterTest, EmptyProgramRunsCleanly) {
    Program empty;
    NullPrompter prompter;
    EXPECT_NO_THROW(run(empty, prompter));
}

// --- Every statement type throws "not yet supported" ---

TEST(InterpreterTest, AskThrowsNotYetSupported) {
    auto program = parse(R"(ask name "Project name?" string
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("ask"), std::string::npos);
    }
}

TEST(InterpreterTest, LetThrowsNotYetSupported) {
    auto program = parse(R"(let x = 1
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("let"), std::string::npos);
    }
}

TEST(InterpreterTest, MkdirThrowsNotYetSupported) {
    auto program = parse(R"(mkdir foo
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("mkdir"), std::string::npos);
    }
}

TEST(InterpreterTest, FileThrowsNotYetSupported) {
    auto program = parse(R"(file foo.txt content "hi"
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("file"), std::string::npos);
    }
}

TEST(InterpreterTest, RepeatThrowsNotYetSupported) {
    auto program = parse(R"(repeat n as i
end
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("repeat"), std::string::npos);
    }
}

TEST(InterpreterTest, CopyThrowsNotYetSupported) {
    auto program = parse(R"(copy src into dst
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("copy"), std::string::npos);
    }
}

TEST(InterpreterTest, IncludeThrowsNotYetSupported) {
    auto program = parse(R"(include claude_setup
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("include"), std::string::npos);
    }
}

// --- run_for_tests returns the interpreter's environment ---

TEST(InterpreterTest, RunForTestsReturnsEnvironmentOnEmptyProgram) {
    Program empty;
    NullPrompter prompter;
    Environment env = run_for_tests(empty, prompter);
    EXPECT_FALSE(env.lookup("anything").has_value());
}

// --- Expression evaluator: literals ---

TEST(EvalExprTest, StringLiteral) {
    Environment env;
    auto e = str_lit("hi");
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "hi");
}

TEST(EvalExprTest, IntegerLiteralCastsToInt64) {
    Environment env;
    auto e = int_lit(42);
    EXPECT_EQ(std::get<std::int64_t>(evaluate_expr(*e, env)), 42);
}

TEST(EvalExprTest, BoolLiteral) {
    Environment env;
    auto e = bool_lit(true);
    EXPECT_TRUE(std::get<bool>(evaluate_expr(*e, env)));
}

// --- Identifier resolution ---

TEST(EvalExprTest, IdentifierLooksUpEnv) {
    Environment env;
    env.declare("x", Value{std::int64_t{7}});
    auto e = ident("x");
    EXPECT_EQ(std::get<std::int64_t>(evaluate_expr(*e, env)), 7);
}

TEST(EvalExprTest, IdentifierMissingThrows) {
    Environment env;
    auto e = wrap(IdentifierExpr{.name = "missing", .line = 4, .column = 9});
    try {
        evaluate_expr(*e, env);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& ex) {
        EXPECT_EQ(ex.line(), 4);
        EXPECT_EQ(ex.column(), 9);
    }
}

// --- Unary NOT ---

TEST(EvalExprTest, NotTrueIsFalse) {
    Environment env;
    auto e = unary_not(bool_lit(true));
    EXPECT_FALSE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, NotNotTrueIsTrue) {
    Environment env;
    auto e = unary_not(unary_not(bool_lit(true)));
    EXPECT_TRUE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, NotIntThrows) {
    Environment env;
    auto e = unary_not(int_lit(1));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

// --- Arithmetic ---

TEST(EvalExprTest, Addition) {
    Environment env;
    auto e = binop(TokenType::PLUS, int_lit(2), int_lit(3));
    EXPECT_EQ(std::get<std::int64_t>(evaluate_expr(*e, env)), 5);
}

TEST(EvalExprTest, IntegerDivisionTruncates) {
    Environment env;
    auto e = binop(TokenType::SLASH, int_lit(10), int_lit(3));
    EXPECT_EQ(std::get<std::int64_t>(evaluate_expr(*e, env)), 3);
}

TEST(EvalExprTest, DivisionByZeroThrows) {
    Environment env;
    auto e = binop(TokenType::SLASH, int_lit(10), int_lit(0));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, IntMinDivByMinusOneThrows) {
    Environment env;
    env.declare("a", Value{std::int64_t{INT64_MIN}});
    env.declare("b", Value{std::int64_t{-1}});
    auto e = binop(TokenType::SLASH, ident("a"), ident("b"));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, AdditionOverflowWrapsAround) {
    Environment env;
    env.declare("a", Value{std::int64_t{INT64_MAX}});
    auto e = binop(TokenType::PLUS, ident("a"), int_lit(1));
    EXPECT_EQ(std::get<std::int64_t>(evaluate_expr(*e, env)), INT64_MIN);
}

// --- String concat ---

TEST(EvalExprTest, StringConcat) {
    Environment env;
    auto e = binop(TokenType::PLUS, str_lit("foo"), str_lit("bar"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "foobar");
}

TEST(EvalExprTest, StringPlusIntThrows) {
    Environment env;
    auto e = binop(TokenType::PLUS, str_lit("a"), int_lit(1));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

// --- Comparison ---

TEST(EvalExprTest, EqualityTrue) {
    Environment env;
    auto e = binop(TokenType::EQUALS, int_lit(1), int_lit(1));
    EXPECT_TRUE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, EqualityFalse) {
    Environment env;
    auto e = binop(TokenType::EQUALS, int_lit(1), int_lit(2));
    EXPECT_FALSE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, EqualityMixedVariantReturnsFalse) {
    Environment env;
    auto e = binop(TokenType::EQUALS, int_lit(1), str_lit("1"));
    EXPECT_FALSE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, OrderingOnStringsThrows) {
    Environment env;
    auto e = binop(TokenType::LESS, str_lit("a"), str_lit("b"));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, OrderingOnInts) {
    Environment env;
    auto e = binop(TokenType::LESS, int_lit(1), int_lit(2));
    EXPECT_TRUE(std::get<bool>(evaluate_expr(*e, env)));
}

// --- Logical and short-circuit ---

TEST(EvalExprTest, AndFalse) {
    Environment env;
    auto e = binop(TokenType::AND, bool_lit(true), bool_lit(false));
    EXPECT_FALSE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, AndShortCircuitsOnFalse) {
    Environment env;
    // false and (1/0) — must not evaluate the right side
    auto e = binop(TokenType::AND, bool_lit(false),
                   binop(TokenType::SLASH, int_lit(1), int_lit(0)));
    EXPECT_FALSE(std::get<bool>(evaluate_expr(*e, env)));
}

TEST(EvalExprTest, OrShortCircuitsOnTrue) {
    Environment env;
    auto e = binop(TokenType::OR, bool_lit(true),
                   binop(TokenType::SLASH, int_lit(1), int_lit(0)));
    EXPECT_TRUE(std::get<bool>(evaluate_expr(*e, env)));
}

// --- Functions ---

TEST(EvalExprTest, LowerString) {
    Environment env;
    auto e = fn("lower", str_lit("HI"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "hi");
}

TEST(EvalExprTest, UpperString) {
    Environment env;
    auto e = fn("upper", str_lit("hi"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "HI");
}

TEST(EvalExprTest, TrimString) {
    Environment env;
    auto e = fn("trim", str_lit("  x  "));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "x");
}

TEST(EvalExprTest, FunctionTypeMismatchThrows) {
    Environment env;
    auto e = fn("lower", int_lit(42));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, UnknownFunctionThrows) {
    Environment env;
    auto e = fn("frobnicate", str_lit("x"));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

// --- value_to_string ---

TEST(ValueToStringTest, StringPassesThrough) {
    EXPECT_EQ(value_to_string(Value{std::string{"hello"}}), "hello");
}

TEST(ValueToStringTest, IntZero) {
    EXPECT_EQ(value_to_string(Value{std::int64_t{0}}), "0");
}

TEST(ValueToStringTest, IntNegative) {
    EXPECT_EQ(value_to_string(Value{std::int64_t{-7}}), "-7");
}

TEST(ValueToStringTest, BoolTrue) {
    EXPECT_EQ(value_to_string(Value{true}), "true");
}

TEST(ValueToStringTest, BoolFalse) {
    EXPECT_EQ(value_to_string(Value{false}), "false");
}
