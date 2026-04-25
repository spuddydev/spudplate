#include "spudplate/interpreter.h"

#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
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

using spudplate::AliasMap;
using spudplate::BinaryExpr;
using spudplate::BoolLiteralExpr;
using spudplate::Environment;
using spudplate::evaluate_expr;
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
using spudplate::run;
using spudplate::run_for_tests;
using spudplate::RuntimeError;
using spudplate::ScriptedPrompter;
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

// Per-test scratch directory. Created on construction with a randomised name
// under the system temp dir; the previous working directory is restored and
// the directory is wiped on destruction. Tests that touch the filesystem
// `chdir` into one of these so `mkdir foo` etc. resolve under it.
class TmpDir {
  public:
    TmpDir() {
        prev_ = std::filesystem::current_path();
        std::random_device rd;
        std::stringstream ss;
        ss << "spudplate-test-" << std::hex << rd() << rd();
        path_ = std::filesystem::temp_directory_path() / ss.str();
        std::filesystem::create_directories(path_);
        std::filesystem::current_path(path_);
    }
    TmpDir(const TmpDir&) = delete;
    TmpDir& operator=(const TmpDir&) = delete;
    ~TmpDir() {
        std::error_code ec;
        std::filesystem::current_path(prev_, ec);
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
    std::filesystem::path prev_;
};

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

TEST(AskTest, WhenSkipsAsk) {
    auto p = parse(R"(ask use_x "Use X?" bool
ask name "Name?" string when use_x
)");
    ScriptedPrompter prompter({"false"});
    Environment env = run_for_tests(p, prompter);
    EXPECT_FALSE(env.lookup("name").has_value());
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

TEST(ScriptedPrompterTest, ExhaustionThrowsLogicError) {
    auto p = parse(R"(ask name "Name?" string
)");
    ScriptedPrompter prompter({});
    EXPECT_THROW(run_for_tests(p, prompter), std::logic_error);
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

TEST(MkdirTest, FromIsNotYetSupported) {
    TmpDir td;
    auto p = parse(R"(mkdir foo from base
)");
    ScriptedPrompter prompter({});
    try {
        run(p, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("not yet supported"),
                  std::string::npos);
    }
    // The `from` rejection happens during execution, before flush — nothing
    // was written.
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo"));
}

TEST(MkdirTest, DeferredWriteAtomicityOnPreFlushFailure) {
    TmpDir td;
    // First mkdir queues; second throws on `from` before any flush runs.
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
    // is also skipped — neither directory is created and the alias is never
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

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

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
    auto p = parse(R"(file run.sh content "echo hi" mode 0755
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

TEST(FileTest, FromIsNotYetSupported) {
    TmpDir td;
    auto p = parse(R"(file foo.txt from src
)");
    ScriptedPrompter prompter({});
    try {
        run(p, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("not yet supported"),
                  std::string::npos);
    }
    EXPECT_FALSE(std::filesystem::exists(td.path() / "foo.txt"));
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
    // Each iteration must see only its own binding — no bleed across
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
    // Crucially, week_0/sub_1 and week_1/sub_0 must NOT exist — they would
    // if the alias from iteration 0 leaked into iteration 1 or vice versa.
    EXPECT_FALSE(std::filesystem::exists(td.path() / "week_0" / "sub_1"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "week_1" / "sub_0"));
}
