#include "spudplate/interpreter.h"

#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/token.h"
#include "test_helpers.h"

using spudplate::AliasMap;
using spudplate::BinaryExpr;
using spudplate::BoolLiteralExpr;
using spudplate::Environment;
using spudplate::dry_run;
using spudplate::evaluate_expr;
using spudplate::locale_is_utf8;
using spudplate::evaluate_path;
using spudplate::PathExpr;
using spudplate::PathInterp;
using spudplate::PathLiteral;
using spudplate::PathSegment;
using spudplate::PathVar;
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
using spudplate::PromptRequest;
using spudplate::run;
using spudplate::run_for_tests;
using spudplate::RuntimeError;
using spudplate::ScriptedPrompter;
using spudplate::StdinPrompter;
using spudplate::StringLiteralExpr;
using spudplate::TokenType;
using spudplate::UnaryExpr;
using spudplate::Value;
using spudplate::value_to_string;
using spudplate::VarType;

namespace {

// Test stub - never asked, never answered. Used in Part 1 where every
// statement throws "not yet supported" before the prompter is reached.
class NullPrompter : public Prompter {
  public:
    std::string prompt(const PromptRequest& /*req*/) override {
        throw std::logic_error("NullPrompter should not be called");
    }
    bool authorize(const std::string& /*summary*/) override {
        throw std::logic_error("NullPrompter::authorize should not be called");
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
    std::vector<ExprPtr> args;
    args.push_back(std::move(arg));
    return wrap(FunctionCallExpr{.name = name,
                                 .arguments = std::move(args),
                                 .line = 1,
                                 .column = 1});
}
ExprPtr fn3(const std::string& name, ExprPtr a, ExprPtr b, ExprPtr c) {
    std::vector<ExprPtr> args;
    args.push_back(std::move(a));
    args.push_back(std::move(b));
    args.push_back(std::move(c));
    return wrap(FunctionCallExpr{.name = name,
                                 .arguments = std::move(args),
                                 .line = 1,
                                 .column = 1});
}

// Path-segment builders.
PathSegment plit(const std::string& v) {
    return PathLiteral{.value = v, .line = 1, .column = 1};
}
PathSegment pvar(const std::string& name) {
    return PathVar{.name = name, .line = 1, .column = 1};
}
PathSegment pinterp(ExprPtr e) {
    return PathInterp{.expression = std::move(e), .line = 1, .column = 1};
}
PathExpr path(std::vector<PathSegment> segs) {
    return PathExpr{.segments = std::move(segs), .line = 1, .column = 1};
}

using spudplate::test::TmpDir;

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
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

// --- Statement types still pending an implementation ---

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
    // false and (1/0) - must not evaluate the right side
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

TEST(EvalExprTest, ReplaceBasic) {
    Environment env;
    auto e = fn3("replace", str_lit("Hello World"), str_lit(" "), str_lit("-"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "Hello-World");
}

TEST(EvalExprTest, ReplaceMultipleOccurrences) {
    Environment env;
    auto e = fn3("replace", str_lit("a.b.c.d"), str_lit("."), str_lit("/"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "a/b/c/d");
}

TEST(EvalExprTest, ReplaceNoMatch) {
    Environment env;
    auto e = fn3("replace", str_lit("hello"), str_lit("xyz"), str_lit("Q"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "hello");
}

TEST(EvalExprTest, ReplaceMultiCharNeedle) {
    Environment env;
    auto e = fn3("replace", str_lit("foobarfoo"), str_lit("foo"), str_lit("X"));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "XbarX");
}

TEST(EvalExprTest, ReplaceWithEmptyReplacement) {
    Environment env;
    auto e = fn3("replace", str_lit("hello world"), str_lit("o"), str_lit(""));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "hell wrld");
}

TEST(EvalExprTest, ReplaceEmptyNeedleErrors) {
    Environment env;
    auto e = fn3("replace", str_lit("x"), str_lit(""), str_lit("y"));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, ReplaceWrongArityErrors) {
    Environment env;
    auto e = fn("replace", str_lit("hello"));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, LowerWrongArityErrors) {
    Environment env;
    auto e = fn3("lower", str_lit("a"), str_lit("b"), str_lit("c"));
    EXPECT_THROW(evaluate_expr(*e, env), RuntimeError);
}

TEST(EvalExprTest, ReplaceComposesWithLower) {
    Environment env;
    env.declare("name", Value{std::string{"Hello World"}});
    auto e = fn("lower", fn3("replace", ident("name"), str_lit(" "), str_lit("-")));
    EXPECT_EQ(std::get<std::string>(evaluate_expr(*e, env)), "hello-world");
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

// --- Path evaluator ---

TEST(EvalPathTest, PureLiteralPath) {
    Environment env;
    AliasMap aliases;
    std::vector<PathSegment> segs;
    segs.push_back(plit("static/notes/README.md"));
    auto pe = path(std::move(segs));
    EXPECT_EQ(evaluate_path(pe, env, aliases), "static/notes/README.md");
}

TEST(EvalPathTest, PathVarFromAliasMap) {
    Environment env;
    AliasMap aliases{{"project", "my-project"}};
    std::vector<PathSegment> segs;
    segs.push_back(pvar("project"));
    segs.push_back(plit("/src"));
    auto pe = path(std::move(segs));
    EXPECT_EQ(evaluate_path(pe, env, aliases), "my-project/src");
}

TEST(EvalPathTest, PathInterpAgainstEnvString) {
    Environment env;
    env.declare("prefix", Value{std::string{"x"}});
    AliasMap aliases;
    std::vector<PathSegment> segs;
    segs.push_back(pinterp(ident("prefix")));
    segs.push_back(plit("/sub"));
    auto pe = path(std::move(segs));
    EXPECT_EQ(evaluate_path(pe, env, aliases), "x/sub");
}

TEST(EvalPathTest, PathInterpWithArithmetic) {
    Environment env;
    env.declare("n", Value{std::int64_t{3}});
    AliasMap aliases;
    std::vector<PathSegment> segs;
    segs.push_back(plit("week_"));
    segs.push_back(pinterp(binop(TokenType::PLUS, ident("n"), int_lit(1))));
    auto pe = path(std::move(segs));
    EXPECT_EQ(evaluate_path(pe, env, aliases), "week_4");
}

TEST(EvalPathTest, PathInterpStringifiesBool) {
    Environment env;
    env.declare("use_x", Value{true});
    AliasMap aliases;
    std::vector<PathSegment> segs;
    segs.push_back(plit("flag_"));
    segs.push_back(pinterp(ident("use_x")));
    auto pe = path(std::move(segs));
    EXPECT_EQ(evaluate_path(pe, env, aliases), "flag_true");
}

TEST(EvalPathTest, UnboundAliasThrows) {
    Environment env;
    AliasMap aliases;
    std::vector<PathSegment> segs;
    segs.push_back(PathVar{.name = "ghost", .line = 5, .column = 9});
    auto pe = path(std::move(segs));
    try {
        evaluate_path(pe, env, aliases);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& ex) {
        EXPECT_NE(std::string(ex.what()).find("ghost"), std::string::npos);
        EXPECT_EQ(ex.line(), 5);
        EXPECT_EQ(ex.column(), 9);
    }
}

// --- Ask + Let execution (Part 4) ---

TEST(AskTest, StringAnswerIsBound) {
    auto p = parse(R"(ask name "Project name?" string
)");
    ScriptedPrompter prompter({"my-project"});
    Environment env = run_for_tests(p, prompter);
    auto v = env.lookup("name");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::string>(*v), "my-project");
}

TEST(AskTest, BoolAnswerCaseInsensitive) {
    auto p = parse(R"(ask flag "Enable?" bool
)");
    ScriptedPrompter t({"true"});
    EXPECT_TRUE(std::get<bool>(*run_for_tests(p, t).lookup("flag")));
    ScriptedPrompter f({"FALSE"});
    EXPECT_FALSE(std::get<bool>(*run_for_tests(p, f).lookup("flag")));
}

TEST(AskTest, IntAnswer) {
    auto p = parse(R"(ask n "How many?" int
)");
    ScriptedPrompter prompter({"42"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("n")), 42);
}

TEST(AskTest, EmptyInputUsesDefault) {
    auto p = parse(R"(ask license "License?" string default "MIT"
)");
    ScriptedPrompter prompter({""});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("license")), "MIT");
}

TEST(AskTest, ComputedDefaultUsesPriorAnswer) {
    auto p = parse(R"(ask project_name "Project?" string
ask slug "Slug?" string default lower(trim(project_name))
)");
    ScriptedPrompter prompter({"  Hello World  ", ""});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("slug")), "hello world");
}

TEST(AskTest, ComputedDefaultStillOverridable) {
    auto p = parse(R"(ask project_name "Project?" string
ask slug "Slug?" string default lower(project_name)
)");
    ScriptedPrompter prompter({"MyProj", "custom"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("slug")), "custom");
}

TEST(AskTest, ComputedDefaultStringConcat) {
    auto p = parse(R"(ask base "Base?" string
ask dir "Dir?" string default base + "-app"
)");
    ScriptedPrompter prompter({"foo", ""});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("dir")), "foo-app");
}

TEST(AskTest, ComputedDefaultIntArithmetic) {
    auto p = parse(R"(ask weeks "Weeks?" int
ask days "Days?" int default weeks * 7
)");
    ScriptedPrompter prompter({"3", ""});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("days")), 21);
}

TEST(AskTest, EmptyInputWithoutDefaultRePrompts) {
    auto p = parse(R"(ask name "Name?" string
)");
    ScriptedPrompter prompter({"", "MyProj"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("name")), "MyProj");
}

TEST(AskTest, StringOptionsRetryUntilValid) {
    auto p = parse(R"(ask format "Format?" string options "pdf" "html"
)");
    ScriptedPrompter prompter({"foo", "pdf"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("format")), "pdf");
}

TEST(AskTest, IntOptionsAcceptNumericMatch) {
    auto p = parse(R"(ask v "Version?" int options 15 16 17
)");
    ScriptedPrompter prompter({"15"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("v")), 15);
}

TEST(AskTest, WhenSkipsAskAndBindsDefault) {
    auto p = parse(R"(ask use_x "Use X?" bool
ask name "Name?" string default "anon" when use_x
)");
    ScriptedPrompter prompter({"false"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("name")), "anon");
}

TEST(AskTest, SkippedAskDefaultVisibleToLaterWhenClause) {
    // Reproduces the issue #59 pattern: a question gated on a prior bool, then
    // a later action gated on the skipped question. With the default applied,
    // the later when clause sees a real bound value rather than crashing.
    auto p = parse(R"(ask add_git "Init git?" bool
ask add_gitignore "Add gitignore?" bool default true when add_git
let gate = add_gitignore
)");
    ScriptedPrompter prompter({"false"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_TRUE(std::get<bool>(*env.lookup("add_gitignore")));
    EXPECT_TRUE(std::get<bool>(*env.lookup("gate")));
}

TEST(AskTest, SkippedAskInRepeatDeclaresPerIteration) {
    auto p = parse(R"(ask n "Count?" int
repeat n as i
  ask use_extra "Extra?" bool
  ask label "Label?" string default "" when use_extra
end
)");
    ScriptedPrompter prompter({"2", "false", "true", "real"});
    Environment env = run_for_tests(p, prompter);
    // The per-iteration `label` binding is local to the repeat body and not
    // visible at the outer scope. The run completes without "undefined
    // variable" errors, which is the bug-fix proof.
    EXPECT_FALSE(env.lookup("label").has_value());
}

TEST(AskTest, SkippedAskDefaultEvaluatesPriorBinding) {
    auto p = parse(R"(ask use_x "Use X?" bool
ask base "Base?" string
ask label "Label?" string default base when use_x
)");
    ScriptedPrompter prompter({"false", "fallback"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("label")), "fallback");
}

TEST(AskTest, SkippedAskMissingDefaultThrowsValidatorGap) {
    // Bypasses validate() via run_for_tests so we can exercise the defence-
    // in-depth path. The interpreter should raise a clear "validator gap"
    // error rather than silently leaving the variable unbound.
    auto p = parse(R"(ask use_x "Use X?" bool
ask name "Name?" string when use_x
)");
    ScriptedPrompter prompter({"false"});
    try {
        run_for_tests(p, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("when-gated ask 'name'"),
                  std::string::npos);
        EXPECT_NE(std::string(e.what()).find("validator should have rejected"),
                  std::string::npos);
    }
}

TEST(AskTest, BadBoolRetries) {
    auto p = parse(R"(ask flag "Enable?" bool
)");
    ScriptedPrompter prompter({"maybe", "true"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_TRUE(std::get<bool>(*env.lookup("flag")));
}

TEST(AskTest, BadIntRetries) {
    auto p = parse(R"(ask n "How many?" int
)");
    ScriptedPrompter prompter({"abc", "7"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("n")), 7);
}

TEST(AskTest, BoolAcceptsYesNo) {
    auto p = parse(R"(ask flag "Enable?" bool
)");
    ScriptedPrompter prompter({"yes"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_TRUE(std::get<bool>(*env.lookup("flag")));
}

TEST(AskTest, BoolAcceptsShortYN) {
    auto p = parse(R"(ask flag "Enable?" bool
)");
    ScriptedPrompter prompter({"n"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_FALSE(std::get<bool>(*env.lookup("flag")));
}

TEST(AskTest, NumberedOptionIndexResolves) {
    auto p = parse(R"(ask format "Format?" string options "pdf" "html" "latex"
)");
    ScriptedPrompter prompter({"2"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("format")), "html");
}

TEST(AskTest, NumberedIndexOnIntOptions) {
    auto p = parse(R"(ask v "PG?" int options 15 16 17
)");
    ScriptedPrompter prompter({"3"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("v")), 17);
}

TEST(AskTest, OutOfRangeIndexFallsThrough) {
    // "5" is outside the index range so it parses as the int 5, which is not
    // in the options list - re-prompt rather than crash.
    auto p = parse(R"(ask v "PG?" int options 15 16 17
)");
    ScriptedPrompter prompter({"5", "16"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("v")), 16);
}

TEST(AskTest, RejectionPropagatesPreviousError) {
    auto p = parse(R"(ask flag "Enable?" bool
)");
    ScriptedPrompter prompter({"maybe", "true"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request().has_value());
    ASSERT_TRUE(prompter.last_request()->previous_error.has_value());
    EXPECT_EQ(*prompter.last_request()->previous_error, "expected yes or no");
}

TEST(AskTest, EmptyOnRequiredQuestionPropagatesError) {
    auto p = parse(R"(ask name "Name?" string
)");
    ScriptedPrompter prompter({"", "x"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request()->previous_error.has_value());
    EXPECT_EQ(*prompter.last_request()->previous_error,
              "this question is required");
}

TEST(AskTest, AskInsideRepeatRunsPerIteration) {
    TmpDir td;
    auto p = parse(R"(ask n "Count?" int
repeat n as i
  ask name "Module name?" string
  mkdir {name}
end
)");
    ScriptedPrompter prompter({"3", "alpha", "beta", "gamma"});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "alpha"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "beta"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "gamma"));
}

TEST(AskTest, AskInsideRepeatKeepsStaticCounter) {
    // The static `(N/M)` counter is shown inside a repeat as a positional
    // anchor; M is the static-statement count and does not grow with
    // iterations. Iteration position is reported separately via
    // `iterations` (see B5 tests).
    auto p = parse(R"(ask n "Count?" int
repeat n as i
  ask name "Module?" string
end
)");
    ScriptedPrompter prompter({"1", "x"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request().has_value());
    EXPECT_EQ(prompter.last_request()->question_index, 1);
    EXPECT_EQ(prompter.last_request()->question_total, 1);
    EXPECT_EQ(prompter.last_request()->indent_level, 1);
}

TEST(AskTest, NestedAskGetsDoubleIndent) {
    auto p = parse(R"(ask n "Outer count?" int
ask m "Inner count?" int
repeat n as i
  repeat m as j
    ask name "Inner?" string
  end
end
)");
    ScriptedPrompter prompter({"1", "1", "leaf"});
    run_for_tests(p, prompter);
    EXPECT_EQ(prompter.last_request()->indent_level, 2);
}

TEST(AskTest, TopLevelCounterUnaffectedByRepeatAsks) {
    // Three top-level asks. The ask inside repeat must not increment the
    // running counter.
    auto p = parse(R"(ask a "A?" string
ask n "Count?" int
repeat n as i
  ask in_loop "In?" string
end
ask z "Z?" string
)");
    ScriptedPrompter prompter({"av", "1", "loopv", "zv"});
    run_for_tests(p, prompter);
    // Last presented prompt is `ask z` - top-level, should be (3/3).
    EXPECT_EQ(prompter.last_request()->question_index, 3);
    EXPECT_EQ(prompter.last_request()->question_total, 3);
}

TEST(AskTest, AskInsideRepeatReportsIteration) {
    auto p = parse(R"(ask n "Count?" int
repeat n as i
  ask name "Module?" string
end
)");
    // Four iterations; we want the second iteration's prompt captured. The
    // scripted prompter records `last_request` on every prompt, so we drive
    // through all four and rely on the last being iteration 4 of 4.
    ScriptedPrompter prompter({"4", "a", "b", "c", "d"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request().has_value());
    ASSERT_EQ(prompter.last_request()->iterations.size(), 1u);
    EXPECT_EQ(prompter.last_request()->iterations[0].first, 4);
    EXPECT_EQ(prompter.last_request()->iterations[0].second, 4);
}

TEST(AskTest, AskInsideNestedRepeatReportsStackedIterations) {
    auto p = parse(R"(ask n "Outer?" int
ask m "Inner?" int
repeat n as i
  repeat m as j
    ask name "Inner?" string
  end
end
)");
    // Outer count 2, inner count 3 - the final inner prompt is i=2, j=3.
    ScriptedPrompter prompter({"2", "3",
                               "a", "b", "c",
                               "d", "e", "f"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request().has_value());
    ASSERT_EQ(prompter.last_request()->iterations.size(), 2u);
    EXPECT_EQ(prompter.last_request()->iterations[0].first, 2);
    EXPECT_EQ(prompter.last_request()->iterations[0].second, 2);
    EXPECT_EQ(prompter.last_request()->iterations[1].first, 3);
    EXPECT_EQ(prompter.last_request()->iterations[1].second, 3);
}

TEST(AskTest, AskOutsideRepeatHasNoIterations) {
    auto p = parse(R"(ask name "Name?" string
)");
    ScriptedPrompter prompter({"x"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request().has_value());
    EXPECT_TRUE(prompter.last_request()->iterations.empty());
}

TEST(AskTest, ZeroIterationRepeatNeverPrompts) {
    auto p = parse(R"(ask n "Count?" int
repeat n as i
  ask name "Module?" string
end
)");
    ScriptedPrompter prompter({"0"});
    run_for_tests(p, prompter);
    // Only the outer `ask n` was prompted. The inner ask was never reached so
    // last_request() reflects the outer prompt with empty iterations.
    ASSERT_TRUE(prompter.last_request().has_value());
    EXPECT_EQ(prompter.last_request()->text, "Count?");
    EXPECT_TRUE(prompter.last_request()->iterations.empty());
}

TEST(AskTest, OffOptionPropagatesError) {
    auto p = parse(R"(ask format "Format?" string options "pdf" "html"
)");
    ScriptedPrompter prompter({"foo", "pdf"});
    run_for_tests(p, prompter);
    ASSERT_TRUE(prompter.last_request()->previous_error.has_value());
    EXPECT_EQ(*prompter.last_request()->previous_error,
              "not one of the listed options");
}

// --- StdinPrompter rendering ---

TEST(StdinPrompterTest, PlainStringPromptIsSingleLine) {
    std::stringstream in("MyProj\n");
    std::stringstream out;
    StdinPrompter p(in, out, /*use_colour=*/false);
    PromptRequest req{
        .text = "What is the project name?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
    };
    EXPECT_EQ(p.prompt(req), "MyProj");
    EXPECT_EQ(out.str(), "What is the project name?: ");
}

TEST(StdinPrompterTest, CounterPrefixWhenSet) {
    std::stringstream in("x\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Project name?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
        .question_index = 1,
        .question_total = 3,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(), "(1/3) Project name?: ");
}

TEST(StdinPrompterTest, IndentLevelShiftsPrompt) {
    std::stringstream in("x\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Module name?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
        .question_index = 0,
        .question_total = 0,
        .indent_level = 1,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(), "  Module name?: ");
}

TEST(StdinPrompterTest, NestedIndentDoublesShift) {
    std::stringstream in("x\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Inner?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
        .indent_level = 2,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(), "    Inner?: ");
}

TEST(StdinPrompterTest, IndentAppliesToOptions) {
    std::stringstream in("1\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Format?",
        .type = VarType::String,
        .options = {"pdf", "html"},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
        .indent_level = 1,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(),
              "  Format?\n"
              "    [1] pdf\n"
              "    [2] html\n"
              "  : ");
}

TEST(StdinPrompterTest, CounterSuppressedWhenZero) {
    std::stringstream in("x\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Name?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_EQ(out.str().find("("), std::string::npos);
}

TEST(StdinPrompterTest, BoolDefaultTrueShowsCapitalY) {
    std::stringstream in("\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Use git?",
        .type = VarType::Bool,
        .options = {},
        .default_value = "true",
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_NE(out.str().find("[Y/n]"), std::string::npos);
    EXPECT_EQ(out.str().find("[true]"), std::string::npos);
}

TEST(StdinPrompterTest, BoolDefaultFalseShowsCapitalN) {
    std::stringstream in("\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Use git?",
        .type = VarType::Bool,
        .options = {},
        .default_value = "false",
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_NE(out.str().find("[y/N]"), std::string::npos);
}

TEST(StdinPrompterTest, BoolNoDefaultShowsLowercaseHint) {
    std::stringstream in("yes\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Use git?",
        .type = VarType::Bool,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_NE(out.str().find("[y/n]"), std::string::npos);
}

TEST(StdinPrompterTest, OptionsRenderAsNumberedMenu) {
    std::stringstream in("1\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Format?",
        .type = VarType::String,
        .options = {"pdf", "html", "latex"},
        .default_value = "pdf",
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(),
              "Format?\n"
              "  [1] pdf\n"
              "  [2] html\n"
              "  [3] latex\n"
              "[pdf]: ");
}

TEST(StdinPrompterTest, BoolInlineHasColon) {
    std::stringstream in("\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Use git?",
        .type = VarType::Bool,
        .options = {},
        .default_value = "true",
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(), "Use git? [Y/n]: ");
}

TEST(StdinPrompterTest, StringWithDefaultIsSingleLine) {
    std::stringstream in("\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "License?",
        .type = VarType::String,
        .options = {},
        .default_value = "MIT",
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_EQ(out.str(), "License? [MIT]: ");
}

TEST(StdinPrompterTest, PreviousErrorPrintedAbovePrompt) {
    std::stringstream in("\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Format?",
        .type = VarType::String,
        .options = {"pdf"},
        .default_value = std::nullopt,
        .previous_error = "not one of the listed options",
    };
    p.prompt(req);
    std::string s = out.str();
    auto err_pos = s.find("not one of the listed options");
    auto prompt_pos = s.find("Format?");
    ASSERT_NE(err_pos, std::string::npos);
    ASSERT_NE(prompt_pos, std::string::npos);
    EXPECT_LT(err_pos, prompt_pos);
}

TEST(StdinPrompterTest, ColourEnabledEmitsAnsiCodes) {
    std::stringstream in("x\n");
    std::stringstream out;
    StdinPrompter p(in, out, /*use_colour=*/true);
    PromptRequest req{
        .text = "Name?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_NE(out.str().find("\x1b["), std::string::npos);
}

TEST(StdinPrompterTest, ColourDisabledEmitsNoAnsi) {
    std::stringstream in("x\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "Name?",
        .type = VarType::String,
        .options = {},
        .default_value = std::nullopt,
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_EQ(out.str().find("\x1b["), std::string::npos);
}

TEST(StdinPrompterTest, DefaultRenderedWithBrackets) {
    std::stringstream in("\n");
    std::stringstream out;
    StdinPrompter p(in, out, false);
    PromptRequest req{
        .text = "License?",
        .type = VarType::String,
        .options = {},
        .default_value = "MIT",
        .previous_error = std::nullopt,
    };
    p.prompt(req);
    EXPECT_NE(out.str().find("[MIT]"), std::string::npos);
}

TEST(LetTest, ArithmeticOverPriorAsk) {
    auto p = parse(R"(ask n "n?" int
let m = n + 1
)");
    ScriptedPrompter prompter({"10"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("m")), 11);
}

TEST(LetTest, StringFunctionsCompose) {
    auto p = parse(R"(ask project_name "Name?" string
let slug = lower(trim(project_name))
)");
    ScriptedPrompter prompter({"  My Project  "});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("slug")), "my project");
}

// --- Universal string-literal interpolation ---

TEST(TemplateStringTest, InterpolatesIdentifierInLet) {
    auto p = parse(R"(let name = "world"
let greet = "hello {name}!"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("greet")), "hello world!");
}

TEST(TemplateStringTest, StringifiesIntInInterpolation) {
    auto p = parse(R"(let n = 42
let s = "n={n}"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "n=42");
}

TEST(TemplateStringTest, StringifiesBoolInInterpolation) {
    auto p = parse(R"(let b = true
let s = "b={b}"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "b=true");
}

TEST(TemplateStringTest, ArithmeticInInterpolation) {
    auto p = parse(R"(let n = 5
let s = "next={n + 1}"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "next=6");
}

TEST(TemplateStringTest, FunctionCallInInterpolation) {
    auto p = parse(R"(let raw = "  Hi  "
let s = "[{trim(raw)}]"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "[Hi]");
}

TEST(TemplateStringTest, MultipleInterpolations) {
    auto p = parse(R"(let a = "x"
let b = 2
let s = "{a} times {b}"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "x times 2");
}

TEST(TemplateStringTest, StringifyOnly) {
    // `"{n}"` is a single-part template that stringifies n as the entire result.
    auto p = parse(R"(let n = 7
let s = "{n}"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "7");
}

TEST(TemplateStringTest, RunCommandPreviewKeepsBraceForm) {
    // The trust prompt shows the literal source - interpolations stay
    // visible as `{...}` so the user sees what flows into the command.
    TmpDir td;
    auto p = parse(R"(let label = "ok"
run "echo hi {label}"
)");
    ScriptedPrompter prompter({});
    prompter.set_authorize_response(false);
    run(p, prompter);
    ASSERT_TRUE(prompter.last_authorize_summary().has_value());
    EXPECT_NE(prompter.last_authorize_summary()->find("{label}"),
              std::string::npos);
}

TEST(ScriptedPrompterTest, ExhaustionThrowsLogicError) {
    auto p = parse(R"(ask name "Name?" string
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run_for_tests(p, prompter), std::logic_error);
}

// --- Reassignment ---

TEST(AssignTest, ReassignsLetBinding) {
    auto p = parse(R"(let n = 1
n = n + 2
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("n")), 3);
}

TEST(AssignTest, StringReassignment) {
    auto p = parse(R"(let s = "hi"
s = s + "!"
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::string>(*env.lookup("s")), "hi!");
}

TEST(AssignTest, RepeatAccumulator) {
    // Sum 0+1+2 over three iterations.
    auto p = parse(R"(let total = 0
let n = 3
repeat n as i
  total = total + i
end
)");
    ScriptedPrompter prompter({});
    Environment env = run_for_tests(p, prompter);
    EXPECT_EQ(std::get<std::int64_t>(*env.lookup("total")), 3);
}

TEST(AssignTest, MkdirCapturesValueAtStatementTime) {
    // Each mkdir resolves its path at statement time, so reassigning n
    // between two mkdirs creates two distinct directories.
    TmpDir td;
    auto p = parse(R"(let n = 1
mkdir dir_{n}
n = n + 1
mkdir dir_{n}
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "dir_1"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "dir_2"));
}

TEST(AssignTest, FileContentCapturesValueAtStatementTime) {
    TmpDir td;
    auto p = parse(R"(let label = "first"
file a.txt content "label=" + label
label = "second"
file b.txt content "label=" + label
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "a.txt"), "label=first");
    EXPECT_EQ(read_file(td.path() / "b.txt"), "label=second");
}

// --- Mkdir + flush + safety (Part 5) ---

TEST(MkdirTest, CreatesDirectory) {
    TmpDir td;
    auto p = parse(R"(mkdir my_project
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "my_project"));
}

TEST(MkdirTest, HyphenatedDirectoryName) {
    TmpDir td;
    auto p = parse(R"(mkdir my-project
mkdir my-project/pre-commit-hooks
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "my-project"));
    EXPECT_TRUE(std::filesystem::is_directory(
        td.path() / "my-project" / "pre-commit-hooks"));
}

TEST(MkdirTest, MkdirP) {
    TmpDir td;
    auto p = parse(R"(mkdir a/b/c
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "a" / "b" / "c"));
}

TEST(MkdirTest, WhenFalseSkips) {
    TmpDir td;
    auto p = parse(R"(ask use_foo "Use foo?" bool
mkdir foo when use_foo
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo"));
}

TEST(MkdirTest, WhenTrueCreates) {
    TmpDir td;
    auto p = parse(R"(ask use_foo "Use foo?" bool
mkdir foo when use_foo
)");
    ScriptedPrompter prompter({"true"});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "foo"));
}

TEST(MkdirTest, ModeApplied) {
    TmpDir td;
    auto p = parse(R"(mkdir bar mode 0700
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    auto perms = std::filesystem::status(td.path() / "bar").permissions();
    EXPECT_EQ(perms, static_cast<std::filesystem::perms>(0700));
}

TEST(MkdirTest, AliasResolvesInLaterPath) {
    TmpDir td;
    auto p = parse(R"(mkdir foo as project
mkdir project/sub
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "foo" / "sub"));
}

TEST(MkdirTest, RejectsPreExistingPath) {
    TmpDir td;
    std::filesystem::create_directory(td.path() / "exists");
    auto p = parse(R"(mkdir exists
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
}

TEST(MkdirTest, FromMissingSourceThrows) {
    TmpDir td;
    auto p = parse(R"(mkdir foo from base
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    // The `from` rejection happens during execution, before flush - nothing
    // was written.
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo"));
}

TEST(MkdirTest, FromCopiesSourceTree) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src" / "sub");
    {
        std::ofstream(td.path() / "src" / "top.txt") << "top";
    }
    {
        std::ofstream(td.path() / "src" / "sub" / "nested.txt") << "nested";
    }
    auto p = parse(R"(mkdir dst from src
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "dst"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "dst" / "sub"));
    EXPECT_EQ(read_file(td.path() / "dst" / "top.txt"), "top");
    EXPECT_EQ(read_file(td.path() / "dst" / "sub" / "nested.txt"), "nested");
}

TEST(MkdirTest, FromInterpolatesFileContents) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "readme.md") << "# {name}";
    }
    auto p = parse(R"(let name = "spudplate"
mkdir dst from src
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "readme.md"), "# spudplate");
}

TEST(MkdirTest, FromVerbatimSuppressesInterpolation) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "raw.txt") << "{name} stays";
    }
    auto p = parse(R"(let name = "ignored"
mkdir dst from src verbatim
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "raw.txt"), "{name} stays");
}

TEST(MkdirTest, FromBindsAlias) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "a.txt") << "x";
    }
    auto p = parse(R"(mkdir dst from src as out
file out/extra.txt content "extra"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "a.txt"), "x");
    EXPECT_EQ(read_file(td.path() / "dst" / "extra.txt"), "extra");
}

TEST(MkdirTest, DeferredWriteAtomicityOnPreFlushFailure) {
    TmpDir td;
    // First mkdir queues; second's `from` walk throws on missing source
    // before any flush runs, so neither directory ends up on disk.
    auto p = parse(R"(mkdir a
mkdir b from base
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "a"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "b"));
}

TEST(MkdirTest, SkippedConditionalAliasNotBound) {
    TmpDir td;
    // Producer is skipped (`use_x=false`); consumer guarded by the same when
    // is also skipped - neither directory is created and the alias is never
    // looked up. This proves the runtime does not bind aliases on skipped
    // statements.
    auto p = parse(R"(ask use_x "Use x?" bool
mkdir foo when use_x as project
mkdir project/sub when use_x
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "project"));
}

TEST(EvalPathTest, MixedSegments) {
    Environment env;
    env.declare("i", Value{std::int64_t{2}});
    AliasMap aliases{{"project", "my-app"}};
    std::vector<PathSegment> segs;
    segs.push_back(pvar("project"));
    segs.push_back(plit("/week_"));
    segs.push_back(pinterp(ident("i")));
    segs.push_back(plit("/notes.md"));
    auto pe = path(std::move(segs));
    EXPECT_EQ(evaluate_path(pe, env, aliases), "my-app/week_2/notes.md");
}

// --- File content + flush (Part 6) ---

TEST(FileTest, ContentLiteral) {
    TmpDir td;
    auto p = parse(R"(file foo.txt content "hello"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "hello");
}

TEST(FileTest, ContentInterpolatedFromLet) {
    TmpDir td;
    auto p = parse(R"(let name = "world"
file foo.txt content "hello, " + name
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "hello, world");
}

TEST(FileTest, ContentBraceInterpolatesString) {
    TmpDir td;
    auto p = parse(R"(let name = "world"
file foo.txt content "hello, {name}!"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "hello, world!");
}

TEST(FileTest, ContentBraceInterpolatesInt) {
    TmpDir td;
    auto p = parse(R"(let n = 7
file foo.txt content "n={n}"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "n=7");
}

TEST(FileTest, ContentBraceInterpolatesMultipleVars) {
    TmpDir td;
    auto p = parse(R"(let a = "x"
let b = "y"
file foo.txt content "{a} and {b}"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "x and y");
}

TEST(FileTest, ContentBraceMissingVarErrors) {
    TmpDir td;
    auto p = parse(R"(file foo.txt content "{nope}"
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), spudplate::RuntimeError);
}

TEST(FileTest, ContentBraceUnclosedErrorsAtParse) {
    EXPECT_THROW(parse(R"(file foo.txt content "{name"
)"),
                 spudplate::ParseError);
}

TEST(FileTest, FileInsideQueuedDirectory) {
    TmpDir td;
    auto p = parse(R"(mkdir x
file x/foo.txt content "hi"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "x" / "foo.txt"), "hi");
}

TEST(FileTest, AppendAfterContent) {
    TmpDir td;
    auto p = parse(R"(file foo.txt content "first"
file foo.txt append content "second"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "firstsecond");
}

TEST(FileTest, SamePathTwiceWithoutAppendOverwrites) {
    TmpDir td;
    auto p = parse(R"(file foo.txt content "A"
file foo.txt content "B"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "B");
}

TEST(FileTest, EmptyContent) {
    TmpDir td;
    auto p = parse(R"(file foo.txt content ""
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "foo.txt"));
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "");
}

TEST(FileTest, IntContentCoercedToString) {
    TmpDir td;
    auto p = parse(R"(let n = 42
file count.txt content n
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "count.txt"), "42");
}

TEST(FileTest, BoolContentCoercedToString) {
    TmpDir td;
    auto p = parse(R"(file flag.txt content true
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "flag.txt"), "true");
}

TEST(FileTest, AppendViaAlias) {
    TmpDir td;
    auto p = parse(R"(file foo content "A" as f
file f append content "B"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo"), "AB");
}

TEST(FileTest, ModeApplied) {
    TmpDir td;
    // `run` is a reserved keyword; quote the path to use it as a filename.
    auto p = parse(R"(file "run.sh" content "echo hi" mode 0755
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    auto perms = std::filesystem::status(td.path() / "run.sh").permissions();
    EXPECT_EQ(perms, static_cast<std::filesystem::perms>(0755));
}

TEST(FileTest, RejectsPreExistingFile) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "exists.txt");
        out << "old";
    }
    auto p = parse(R"(file exists.txt content "new"
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
}

TEST(FileTest, FromCopiesSourceContents) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "src.txt");
        out << "from-source content";
    }
    auto p = parse(R"(file foo.txt from src.txt
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "from-source content");
}

TEST(FileTest, FromInterpolatesIdentifiers) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "src.txt");
        out << "hello, {who}!";
    }
    auto p = parse(R"(let who = "world"
file foo.txt from src.txt
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "hello, world!");
}

TEST(FileTest, FromVerbatimSkipsInterpolation) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "src.txt");
        out << "literal {braces}";
    }
    auto p = parse(R"(file foo.txt from src.txt verbatim
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "literal {braces}");
}

TEST(FileTest, FromUnknownIdentInterpolationThrows) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "src.txt");
        out << "{missing}";
    }
    auto p = parse(R"(file foo.txt from src.txt
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo.txt"));
}

TEST(FileTest, FromMissingSourceThrows) {
    TmpDir td;
    auto p = parse(R"(file foo.txt from nope.txt
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo.txt"));
}

TEST(FileTest, FromAppendsToFile) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "src.txt");
        out << "B";
    }
    auto p = parse(R"(file foo.txt content "A"
file foo.txt append from src.txt
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "foo.txt"), "AB");
}

TEST(FileTest, FromUnclosedBraceThrows) {
    TmpDir td;
    {
        std::ofstream out(td.path() / "src.txt");
        out << "oops {name";
    }
    auto p = parse(R"(file foo.txt from src.txt
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
}

TEST(FileTest, WhenFalseSkips) {
    TmpDir td;
    auto p = parse(R"(ask use_f "Use file?" bool
file foo.txt content "hi" when use_f
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo.txt"));
}

// --- Copy ---

TEST(CopyTest, MergesIntoExistingDir) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "a.txt") << "a";
    }
    auto p = parse(R"(mkdir dst
copy src into dst
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "a.txt"), "a");
}

TEST(CopyTest, MissingDestinationFailsAtFlush) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "a.txt") << "a";
    }
    auto p = parse(R"(copy src into dst
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "dst"));
}

TEST(CopyTest, MissingSourceThrows) {
    TmpDir td;
    auto p = parse(R"(mkdir dst
copy nope into dst
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "dst"));
}

TEST(CopyTest, MergesMultipleSources) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src1");
    std::filesystem::create_directories(td.path() / "src2");
    {
        std::ofstream(td.path() / "src1" / "a.txt") << "a";
    }
    {
        std::ofstream(td.path() / "src2" / "b.txt") << "b";
    }
    auto p = parse(R"(mkdir dst
copy src1 into dst
copy src2 into dst
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "a.txt"), "a");
    EXPECT_EQ(read_file(td.path() / "dst" / "b.txt"), "b");
}

TEST(CopyTest, RecursesIntoSubdirectories) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src" / "deep");
    {
        std::ofstream(td.path() / "src" / "deep" / "x.txt") << "x";
    }
    auto p = parse(R"(mkdir dst
copy src into dst
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "dst" / "deep"));
    EXPECT_EQ(read_file(td.path() / "dst" / "deep" / "x.txt"), "x");
}

TEST(CopyTest, InterpolatesByDefault) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "f.txt") << "hello {who}";
    }
    auto p = parse(R"(let who = "you"
mkdir dst
copy src into dst
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "f.txt"), "hello you");
}

TEST(CopyTest, VerbatimSuppressesInterpolation) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "f.txt") << "{who}";
    }
    auto p = parse(R"(let who = "ignored"
mkdir dst
copy src into dst verbatim
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(read_file(td.path() / "dst" / "f.txt"), "{who}");
}

TEST(CopyTest, WhenFalseSkips) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "f.txt") << "x";
    }
    auto p = parse(R"(ask flag "Copy?" bool
mkdir dst
copy src into dst when flag
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "dst"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "dst" / "f.txt"));
}

// --- Repeat (Part 7) ---

TEST(RepeatTest, ZeroCountBodyNeverRuns) {
    TmpDir td;
    auto p = parse(R"(let n = 0
repeat n as i
    mkdir x_{i}
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_empty(td.path()));
}

TEST(RepeatTest, ThreeIterationsCreateThreeDirs) {
    TmpDir td;
    auto p = parse(R"(let n = 3
repeat n as i
    mkdir week_{i}
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "week_0"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "week_1"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "week_2"));
}

TEST(RepeatTest, WhenFalseSkipsLoop) {
    TmpDir td;
    auto p = parse(R"(ask use_loop "Use loop?" bool
let n = 3
repeat n as i when use_loop
    mkdir week_{i}
end
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "week_0"));
}

TEST(RepeatTest, NegativeCountIsZeroIterations) {
    TmpDir td;
    auto p = parse(R"(let n = 0 - 5
repeat n as i
    mkdir x_{i}
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_empty(td.path()));
}

TEST(RepeatTest, InnerLetReferencesIterator) {
    TmpDir td;
    // `let doubled = i + i` reads the iterator inside the body and binds a
    // new value; `mkdir d_{doubled}` proves it was both bound and visible.
    auto p = parse(R"(let n = 3
repeat n as i
    let doubled = i + i
    mkdir d_{doubled}
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "d_0"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "d_2"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "d_4"));
}

TEST(RepeatTest, NestedRepeatsCreateGrid) {
    TmpDir td;
    auto p = parse(R"(let n = 2
let m = 2
repeat n as i
    repeat m as j
        mkdir x_{i}_{j}
    end
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "x_0_0"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "x_0_1"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "x_1_0"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "x_1_1"));
}

TEST(RepeatTest, InnerAliasDoesNotLeak) {
    TmpDir td;
    // `wk` is rebound each iteration to that iteration's `week_{i}` path;
    // the inner `mkdir wk/sub_{i}` resolves through the per-iteration alias.
    // Each iteration must see only its own binding - no bleed across
    // iterations and no escape after the loop.
    auto p = parse(R"(let n = 2
repeat n as i
    mkdir week_{i} as wk
    mkdir wk/sub_{i}
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "week_0" / "sub_0"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "week_1" / "sub_1"));
    // Crucially, week_0/sub_1 and week_1/sub_0 must NOT exist - they would
    // if the alias from iteration 0 leaked into iteration 1 or vice versa.
    EXPECT_FALSE(std::filesystem::exists(td.path() / "week_0" / "sub_1"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "week_1" / "sub_0"));
}

// --- Run statement ---

TEST(RunTest, AuthorizedCommandExecutes) {
    TmpDir td;
    auto p = parse(R"(run "touch marker"
)");
    ScriptedPrompter prompter({});  // accept by default
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "marker"));
}

TEST(RunTest, DeclinedAbortsCleanly) {
    TmpDir td;
    auto p = parse(R"(mkdir foo
run "touch foo/marker"
)");
    ScriptedPrompter prompter({});
    prompter.set_authorize_response(false);
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo"));
}

TEST(RunTest, NonZeroExitAbortsRun) {
    TmpDir td;
    auto p = parse(R"(mkdir before
run "false"
mkdir after
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    // `before` was queued in pending_ before `run`, so the flush has
    // already created it by the time `false` errors.
    EXPECT_TRUE(std::filesystem::exists(td.path() / "before"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "after"));
}

TEST(RunTest, WhenFalseSkipsCommand) {
    TmpDir td;
    auto p = parse(R"(ask flag "Run?" bool default false
run "touch should_not_exist" when flag
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "should_not_exist"));
}

TEST(RunTest, InterpolatedCommandExecutes) {
    TmpDir td;
    auto p = parse(R"(let name = "marker.txt"
run "touch " + name
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "marker.txt"));
}

TEST(RunTest, NonStringCommandIsRuntimeError) {
    TmpDir td;
    auto p = parse(R"(run 42
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
}

TEST(RunTest, ProgramWithoutRunDoesNotInvokeAuthorize) {
    TmpDir td;
    auto p = parse(R"(mkdir foo
)");
    ScriptedPrompter prompter({});
    prompter.set_authorize_response(false);  // would abort if called
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "foo"));
    EXPECT_FALSE(prompter.last_authorize_summary().has_value());
}

TEST(RunTest, AuthorizeSummaryListsLiteralCommands) {
    TmpDir td;
    auto p = parse(R"(run "git init"
run "touch x"
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    ASSERT_TRUE(prompter.last_authorize_summary().has_value());
    const auto& s = *prompter.last_authorize_summary();
    EXPECT_NE(s.find("git init"), std::string::npos);
    EXPECT_NE(s.find("touch x"), std::string::npos);
}

TEST(RunTest, AuthorizeFlagsRepeatInternalCommands) {
    TmpDir td;
    auto p = parse(R"(let n = 1
repeat n as i
  run "echo loop"
end
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    ASSERT_TRUE(prompter.last_authorize_summary().has_value());
    EXPECT_NE(prompter.last_authorize_summary()->find("inside repeat"),
              std::string::npos);
}

TEST(RunTest, SkipAuthorizationBypassesPrompt) {
    TmpDir td;
    auto p = parse(R"(run "touch from_skip"
)");
    ScriptedPrompter prompter({});
    prompter.set_authorize_response(false);  // would abort if called
    run(p, prompter, /*skip_authorization=*/true);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "from_skip"));
    EXPECT_FALSE(prompter.last_authorize_summary().has_value());
}

TEST(RunTest, InClausePinsCwd) {
    TmpDir td;
    auto p = parse(R"(mkdir myapp
run "touch marker" in myapp
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "myapp" / "marker"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "marker"));
}

TEST(RunTest, InClauseRestoresCwdAfterRun) {
    TmpDir td;
    auto saved = std::filesystem::current_path();
    auto p = parse(R"(mkdir myapp
run "touch marker" in myapp
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_EQ(std::filesystem::current_path(), saved);
}

TEST(RunTest, InClauseAcceptsAlias) {
    TmpDir td;
    auto p = parse(R"(mkdir myapp as proj
run "touch marker" in proj
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "myapp" / "marker"));
}

TEST(RunTest, InClauseAcceptsInterpolation) {
    TmpDir td;
    auto p = parse(R"(let dir = "myapp"
mkdir {dir}
run "touch marker" in {dir}
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "myapp" / "marker"));
}

TEST(RunTest, InClauseMissingDirIsRuntimeError) {
    TmpDir td;
    auto p = parse(R"(run "echo hi" in nope
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
}

TEST(RunTest, InClauseRestoresCwdOnCommandFailure) {
    TmpDir td;
    auto saved = std::filesystem::current_path();
    auto p = parse(R"(mkdir myapp
run "false" in myapp
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run(p, prompter), RuntimeError);
    EXPECT_EQ(std::filesystem::current_path(), saved);
}

TEST(RunTest, AuthorizeSummaryIncludesInClause) {
    TmpDir td;
    auto p = parse(R"(mkdir myapp
run "git init" in myapp
)");
    ScriptedPrompter prompter({});
    run(p, prompter);
    ASSERT_TRUE(prompter.last_authorize_summary().has_value());
    EXPECT_NE(prompter.last_authorize_summary()->find("in myapp"),
              std::string::npos);
}

TEST(RunTest, DryRunShowsCommandsButDoesNotExecute) {
    TmpDir td;
    auto p = parse(R"(run "touch should_not_exist"
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    EXPECT_NE(out.str().find("Would execute:"), std::string::npos);
    EXPECT_NE(out.str().find("touch should_not_exist"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "should_not_exist"));
}

TEST(RunTest, ExplicitTimeoutFires) {
    TmpDir td;
    auto p = parse(R"(run "sleep 5" timeout 1
)");
    ScriptedPrompter prompter({});
    try {
        run(p, prompter, /*skip_authorization=*/true);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("timed out after 1s"),
                  std::string::npos);
    }
}

TEST(RunTest, NoTimeoutFlagOverridesPerStatementTimeout) {
    // Explicit `timeout 1` would normally kill `sleep 1` after 1s, but
    // --no-timeout disables timeouts for the whole invocation. A sleep of
    // 0 finishes immediately so this stays fast.
    TmpDir td;
    auto p = parse(R"(run "sleep 0" timeout 1
)");
    ScriptedPrompter prompter({});
    EXPECT_NO_THROW(run(p, prompter, /*skip_authorization=*/true,
                        /*source=*/nullptr,
                        /*timeouts_disabled=*/true));
}

TEST(RunTest, TimeoutKillsGrandchildViaProcessGroup) {
    // The shell forks a `sleep 5` into the background and waits. If the
    // interpreter only killed the shell, the grandchild would survive and
    // the wait would never complete, hanging the test. Killing the whole
    // process group ensures both die.
    TmpDir td;
    auto p = parse(R"(run "sleep 5 & wait" timeout 1
)");
    ScriptedPrompter prompter({});
    auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(run(p, prompter, /*skip_authorization=*/true), RuntimeError);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start);
    // Should die within the 1s timeout plus a small grace window.
    EXPECT_LT(elapsed.count(), 4);
}

// --- Dry run ---

TEST(DryRunTest, EmptyProgramRendersNothing) {
    TmpDir td;
    Program empty;
    NullPrompter prompter;
    std::stringstream out;
    dry_run(empty, prompter, out);
    EXPECT_EQ(out.str(), "Would create:\n  (nothing)\n");
}

TEST(DryRunTest, MkdirAndFileRender) {
    TmpDir td;
    auto p = parse(R"(mkdir foo
file foo/bar.txt content "hi"
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    EXPECT_EQ(out.str(),
              "Would create:\n"
              "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 foo/\n"
              "    \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 bar.txt\n");
}

TEST(DryRunTest, AppendGetsAnnotation) {
    TmpDir td;
    auto p = parse(R"(file log.txt content "first"
file log.txt append content "second"
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    // The file appears once (de-duplicated by path); since the second op is
    // append the leaf carries the annotation.
    EXPECT_NE(out.str().find("log.txt"), std::string::npos);
    EXPECT_NE(out.str().find("(append)"), std::string::npos);
}

TEST(DryRunTest, SiblingsAreSortedAlphabetically) {
    TmpDir td;
    auto p = parse(R"(mkdir zeta
mkdir alpha
mkdir mu
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    auto idx_a = out.str().find("alpha");
    auto idx_m = out.str().find("mu");
    auto idx_z = out.str().find("zeta");
    ASSERT_NE(idx_a, std::string::npos);
    ASSERT_NE(idx_m, std::string::npos);
    ASSERT_NE(idx_z, std::string::npos);
    EXPECT_LT(idx_a, idx_m);
    EXPECT_LT(idx_m, idx_z);
}

TEST(DryRunTest, MkdirFromExpandsTree) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src" / "sub");
    {
        std::ofstream(td.path() / "src" / "top.txt") << "x";
    }
    {
        std::ofstream(td.path() / "src" / "sub" / "deep.txt") << "y";
    }
    auto p = parse(R"(mkdir dst from src
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    EXPECT_NE(out.str().find("dst/"), std::string::npos);
    EXPECT_NE(out.str().find("sub/"), std::string::npos);
    EXPECT_NE(out.str().find("top.txt"), std::string::npos);
    EXPECT_NE(out.str().find("deep.txt"), std::string::npos);
}

TEST(DryRunTest, NoFilesystemSideEffects) {
    TmpDir td;
    auto p = parse(R"(mkdir foo
file foo/bar.txt content "hi"
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo" / "bar.txt"));
}

TEST(DryRunTest, CopyToMissingDestStillRendersTree) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "src");
    {
        std::ofstream(td.path() / "src" / "f.txt") << "x";
    }
    // `copy` into a non-existent `dst` would be a runtime error during a
    // real run; dry-run skips the existence check so the user still gets
    // a useful preview.
    auto p = parse(R"(copy src into dst
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    EXPECT_NO_THROW(dry_run(p, prompter, out));
    EXPECT_NE(out.str().find("dst/"), std::string::npos);
    EXPECT_NE(out.str().find("f.txt"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "dst"));
}

TEST(DryRunTest, MissingFromSourceStillThrows) {
    TmpDir td;
    auto p = parse(R"(mkdir dst from nope
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    EXPECT_THROW(dry_run(p, prompter, out), RuntimeError);
}

TEST(DryRunTest, AsciiGlyphsWhenRequested) {
    TmpDir td;
    // Two top-level siblings ensure the `|   ` continuation glyph shows
    // up under the non-last sibling.
    auto p = parse(R"(mkdir foo
file foo/bar.txt content "hi"
file foo/baz.txt content "hi"
mkdir last
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out, /*ascii_only=*/true);
    const std::string& s = out.str();
    EXPECT_NE(s.find("|-- "), std::string::npos);
    EXPECT_NE(s.find("\\-- "), std::string::npos);
    EXPECT_NE(s.find("|   "), std::string::npos);
    // No UTF-8 box-drawing bytes leaked through.
    EXPECT_EQ(s.find("\xe2\x94"), std::string::npos);
}

TEST(DryRunTest, Utf8GlyphsByDefault) {
    TmpDir td;
    auto p = parse(R"(mkdir foo
)");
    ScriptedPrompter prompter({});
    std::stringstream out;
    dry_run(p, prompter, out);
    EXPECT_NE(out.str().find("\xe2\x94\x94"), std::string::npos);
}

TEST(LocaleIsUtf8Test, EnUsUtf8Detected) {
    setenv("LC_ALL", "", 1);
    setenv("LC_CTYPE", "", 1);
    setenv("LANG", "en_US.UTF-8", 1);
    EXPECT_TRUE(locale_is_utf8());
}

TEST(LocaleIsUtf8Test, CLocaleNotDetected) {
    setenv("LC_ALL", "C", 1);
    setenv("LC_CTYPE", "", 1);
    setenv("LANG", "", 1);
    EXPECT_FALSE(locale_is_utf8());
}

TEST(LocaleIsUtf8Test, NoEnvVarsSetReturnsFalse) {
    unsetenv("LC_ALL");
    unsetenv("LC_CTYPE");
    unsetenv("LANG");
    EXPECT_FALSE(locale_is_utf8());
}

TEST(LocaleIsUtf8Test, FirstSetWinsOverLaterUtf8) {
    // POSIX precedence: LC_ALL overrides everything. A non-UTF-8 LC_ALL
    // means the user wants ASCII even if LANG happens to be UTF-8.
    setenv("LC_ALL", "POSIX", 1);
    setenv("LANG", "en_US.UTF-8", 1);
    EXPECT_FALSE(locale_is_utf8());
    unsetenv("LC_ALL");
}

TEST(DryRunTest, PromptsRunBeforeRendering) {
    TmpDir td;
    auto p = parse(R"(ask name "Project?" string default "demo"
mkdir {name}
)");
    ScriptedPrompter prompter({"hello"});
    std::stringstream out;
    dry_run(p, prompter, out);
    EXPECT_NE(out.str().find("hello/"), std::string::npos);
}

// ----- SourceProvider plumbing --------------------------------------------

namespace {

std::string read_text(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST(SourceProvider, AssetMapMaterialisesFileAndMode) {
    TmpDir td;
    std::vector<spudplate::SpudpackAsset> assets;
    assets.push_back(
        {"tpl/main.cpp", 0644, {'h', 'i', '\n'}});
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse(
        "mkdir out\n"
        "file out/main.cpp from tpl/main.cpp\n");
    ScriptedPrompter prompter({});
    spudplate::run(p, prompter, /*skip_authorization=*/true, &provider);
    EXPECT_EQ(read_text(td.path() / "out/main.cpp"), "hi\n");
}

TEST(SourceProvider, MkdirFromAssetMapNestedTree) {
    TmpDir td;
    std::vector<spudplate::SpudpackAsset> assets;
    assets.push_back({"src/a/b/c.txt", 0644, {'x'}});
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse(
        "mkdir tree from src\n"
        "file tree/a/extra.txt content \"more\"\n");
    ScriptedPrompter prompter({});
    spudplate::run(p, prompter, /*skip_authorization=*/true, &provider);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "tree/a/b/c.txt"));
    EXPECT_TRUE(std::filesystem::exists(td.path() / "tree/a/extra.txt"));
}

TEST(SourceProvider, MkdirFromEmptyLeafDir) {
    TmpDir td;
    std::vector<spudplate::SpudpackAsset> assets;
    assets.push_back({"scaffold/logs/", 0755, {}});
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse("mkdir project from scaffold\n");
    ScriptedPrompter prompter({});
    spudplate::run(p, prompter, /*skip_authorization=*/true, &provider);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "project/logs"));
}

TEST(SourceProvider, AssetMissSurfacesRuntimeError) {
    TmpDir td;
    std::vector<spudplate::SpudpackAsset> assets;
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse("file out.txt from missing.txt\n");
    ScriptedPrompter prompter({});
    try {
        spudplate::run(p, prompter, /*skip_authorization=*/true, &provider);
        FAIL() << "expected throw";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("was not bundled"),
                  std::string::npos);
        EXPECT_GT(e.line(), 0);
    }
}

TEST(SourceProvider, BinaryContentSkipsInterpolation) {
    TmpDir td;
    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
                                  0x00, 0x01, '{', 'x', '}'};
    std::vector<spudplate::SpudpackAsset> assets;
    assets.push_back({"logo.png", 0644, png});
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse(
        "mkdir out\n"
        "file out/logo.png from logo.png\n");
    ScriptedPrompter prompter({});
    spudplate::run(p, prompter, /*skip_authorization=*/true, &provider);
    auto materialised = read_text(td.path() / "out/logo.png");
    EXPECT_EQ(materialised.size(), png.size());
    EXPECT_EQ(std::memcmp(materialised.data(), png.data(), png.size()), 0);
}

TEST(SourceProvider, AppendOverReadonlyRestoresPriorMode) {
    TmpDir td;
    std::vector<spudplate::SpudpackAsset> assets;
    assets.push_back({"readonly.txt", 0444, {'b', 'a', 's', 'e', '\n'}});
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse(
        "file out.txt from readonly.txt\n"
        "file out.txt append content \"more\"\n");
    ScriptedPrompter prompter({});
    spudplate::run(p, prompter, /*skip_authorization=*/true, &provider);
    EXPECT_EQ(read_text(td.path() / "out.txt"), "base\nmore");
    auto perms = std::filesystem::status(td.path() / "out.txt").permissions();
    EXPECT_EQ(static_cast<unsigned>(perms) & 0777, 0444u);
}

TEST(SourceProvider, DiskBackedSkipsBrokenSymlinks) {
    TmpDir td;
    std::filesystem::create_directories(td.path() / "tree");
    std::ofstream(td.path() / "tree/regular.txt") << "hi\n";
    // Broken symlink - target does not exist. Today's behaviour skips
    // these silently rather than aborting the run; the provider
    // preserves that.
    std::filesystem::create_symlink(td.path() / "no/such/target",
                                    td.path() / "tree/dangling");
    auto p = parse("mkdir out from tree\n");
    ScriptedPrompter prompter({});
    spudplate::run(p, prompter, /*skip_authorization=*/true);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "out/regular.txt"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "out/dangling"));
}

TEST(SourceProvider, UnclosedBraceStillSurfacedOnTextWithHighBytes) {
    TmpDir td;
    // Latin-1 bytes (no NUL) plus a stray '{' should still hit the
    // interpolation path and report `unclosed '{'`.
    std::vector<std::uint8_t> body{0xC3, 0xA9, '{', 'a'};  // "é{a"
    std::vector<spudplate::SpudpackAsset> assets;
    assets.push_back({"latin.txt", 0644, body});
    spudplate::AssetMapSourceProvider provider(assets);
    auto p = parse("file out.txt from latin.txt\n");
    ScriptedPrompter prompter({});
    EXPECT_THROW(
        spudplate::run(p, prompter, /*skip_authorization=*/true, &provider),
        RuntimeError);
}

// --- if blocks ---

TEST(IfTest, BodyExecutesWhenConditionTrue) {
    TmpDir td;
    auto p = parse(R"(ask use_x "Use X?" bool
if use_x
  mkdir "x"
end
)");
    ScriptedPrompter prompter({"true"});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "x"));
}

TEST(IfTest, BodySkippedWhenConditionFalse) {
    TmpDir td;
    auto p = parse(R"(ask use_x "Use X?" bool
if use_x
  mkdir "x"
end
)");
    ScriptedPrompter prompter({"false"});
    run(p, prompter);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "x"));
}

TEST(IfTest, NonBoolConditionThrowsAtRuntime) {
    auto p = parse(R"(ask n "Count?" int
if n
  mkdir "x"
end
)");
    ScriptedPrompter prompter({"5"});
    try {
        run_for_tests(p, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("'if' condition must be bool"),
                  std::string::npos);
    }
}

TEST(IfTest, NestedIfInsideRepeat) {
    TmpDir td;
    auto p = parse(R"(ask n "Count?" int
ask use_extra "Extra?" bool
repeat n as i
  if use_extra
    mkdir m_{i}
  end
end
)");
    ScriptedPrompter prompter({"2", "true"});
    run(p, prompter);
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "m_0"));
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "m_1"));
}

TEST(IfTest, FalseIfDoesNotPromptInnerAsk) {
    auto p = parse(R"(ask use_x "Use X?" bool
if use_x
  ask name "Name?" string
end
)");
    ScriptedPrompter prompter({"false"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_FALSE(env.lookup("name").has_value());
}

TEST(IfTest, LetInsideIfNotVisibleAfterEnd) {
    auto p = parse(R"(ask use_x "Use X?" bool
if use_x
  let x = 1
end
)");
    ScriptedPrompter prompter({"true"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_FALSE(env.lookup("x").has_value());
}
