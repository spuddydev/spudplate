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
    std::vector<ExprPtr> args;
    args.push_back(std::move(arg));
    return wrap(FunctionCallExpr{
        .name = name, .arguments = std::move(args), .line = 1, .column = 1});
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

TEST(ValidatorTest, AskInsideSingleRepeatValidates) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  ask use_extra \"Extra?\" bool\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AskInsideNestedRepeatValidates) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "ask m \"Count?\" int\n"
        "repeat n as i\n"
        "  repeat m as j\n"
        "    ask name \"name?\" string\n"
        "  end\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AskInRepeatShadowingOuterIsError) {
    auto program = parse(
        "ask name \"Name?\" string\n"
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  ask name \"Module name?\" string\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AskInRepeatShadowingIteratorIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  ask i \"i?\" int\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
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

// --- when-gated ask requires default ---

TEST(ValidatorTest, WhenGatedAskWithoutDefaultRejected) {
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "ask name \"Name?\" string when use_x\n");
    try {
        validate(program);
        FAIL() << "expected SemanticError";
    } catch (const SemanticError& e) {
        EXPECT_STREQ(e.what(),
                     "when-gated ask 'name' requires a default value "
                     "(add: default <value>)");
    }
}

TEST(ValidatorTest, WhenGatedAskWithDefaultPasses) {
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "ask name \"Name?\" string default \"\" when use_x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AskWithoutWhenOrDefaultStillPasses) {
    auto program = parse("ask name \"Name?\" string\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, StaticallyTrueWhenStillRequiresDefault) {
    auto program = parse(
        "ask name \"Name?\" string when true\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, DefaultExprReferencingPriorBindingPasses) {
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "ask base \"Base?\" string\n"
        "ask label \"Label?\" string default base when use_x\n");
    EXPECT_NO_THROW(validate(program));
}

// --- if block ---

TEST(ValidatorTest, IfBlockBodyValidates) {
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "if use_x\n"
        "  mkdir \"x\"\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, IfShadowingOuterIsError) {
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "let x = 1\n"
        "if use_x\n"
        "  let x = 2\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, LetInsideIfNotVisibleOutside) {
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "if use_x\n"
        "  let x = 1\n"
        "end\n"
        "let y = x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AskInsideIfWithoutDefaultPasses) {
    // The if block gates the ask structurally; without an inner when_clause
    // the rule from #59 does not fire.
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "if use_x\n"
        "  ask name \"Name?\" string\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AskInsideIfWithInnerWhenStillRequiresDefault) {
    // The inner when can be false while the outer if is true, so the bug
    // shape from #59 still applies. Section A's rule must still fire.
    auto program = parse(
        "ask use_x \"Use X?\" bool\n"
        "ask use_y \"Use Y?\" bool\n"
        "if use_x\n"
        "  ask name \"Name?\" string when use_y\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
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
    // `a and b` vs `b and a` - known limitation per TODO.
    TypeMap tm{{"a", VarType::Bool}, {"b", VarType::Bool}};
    auto left = bin(TokenType::AND, id("a"), id("b"));
    auto right = bin(TokenType::AND, id("b"), id("a"));
    EXPECT_FALSE(equivalent(*left, *right, tm));
}

TEST(NormalizeTest, UnknownIdentifierKeepsStructuralForm) {
    // `x` is NOT in the type map - bool simplification skipped.
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

// --- Conditional alias scoping tests ---

TEST(ValidatorTest, AliasBoundWhenXReferencedWhenXValid) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir bar/sub when x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AliasBoundWhenXReferencedWhenXEqualsTrueValid) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir bar/sub when x == true\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AliasBoundWhenXEqualsTrueReferencedWhenXValid) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x == true as bar\n"
        "mkdir bar/sub when x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AliasBoundWhenXReferencedWhenXEqualsFalseIsError) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir bar/sub when x == false\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AliasBoundWhenXReferencedWhenNotXIsError) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir bar/sub when not x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AliasBoundWhenAAndBReferencedWhenBAndAIsError) {
    // Commutativity is a known limitation of normalization.
    auto program = parse(
        "ask a \"a?\" bool\n"
        "ask b \"b?\" bool\n"
        "mkdir foo when a and b as bar\n"
        "mkdir bar/sub when b and a\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AliasBoundWhenXReferencedWithNoWhenIsError) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir bar/sub\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, UnconditionalAliasReferencedWhenXValid) {
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo as bar\n"
        "mkdir bar/sub when x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, UnconditionalAliasReferencedUnconditionallyValid) {
    auto program = parse(
        "mkdir foo as bar\n"
        "mkdir bar/sub\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ConditionalAliasReferenceInFilePath) {
    // Exercises the file.path reference site.
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "file bar/readme content \"hi\" when x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ConditionalAliasReferenceInCopySource) {
    // Exercises the copy.source reference site.
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir dest\n"
        "copy bar into dest when x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ConditionalAliasReferenceInCopyDestinationMismatch) {
    // Exercises copy.destination + mismatched condition.
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir src\n"
        "copy src into bar when not x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, ConditionalAliasReferenceInMkdirFromSource) {
    // Exercises mkdir.from_source reference site.
    auto program = parse(
        "ask x \"x?\" bool\n"
        "mkdir foo when x as bar\n"
        "mkdir new from bar when x\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, RepeatWhenNotFoldedIntoAliasCondition) {
    // The enclosing repeat's when clause is NOT silently combined with the
    // inner statement's condition. An inner statement without its own when
    // still mismatches a conditional alias, even if the repeat's own when
    // matches the alias's binding condition.
    auto program = parse(
        "ask x \"x?\" bool\n"
        "ask n \"n?\" int\n"
        "mkdir foo when x as bar\n"
        "repeat n as i when x\n"
        "  mkdir bar/dup_{i}\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, RunReferencesKnownVariable) {
    auto program = parse(
        "ask repo_url \"Repo url?\" string\n"
        "run \"git clone \" + repo_url\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, RunInsideRepeatSeesIterator) {
    auto program = parse(
        "let n = 1\n"
        "repeat n as i\n"
        "  run \"echo \" + i\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, RunReferencesIteratorOutsideRepeatIsError) {
    // The iterator goes out of scope when the repeat ends; using it after
    // is what walk_expr's check_reference catches.
    auto program = parse(
        "let n = 1\n"
        "repeat n as i\n"
        "end\n"
        "run \"echo \" + i\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, RunInClauseReferencesAlias) {
    auto program = parse(
        "mkdir myapp as proj\n"
        "run \"git init\" in proj\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, RunInClauseReferencesPoppedAliasIsError) {
    auto program = parse(
        "let n = 1\n"
        "repeat n as i\n"
        "  mkdir foo as bar\n"
        "end\n"
        "run \"echo hi\" in bar\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, RunWhenReferencesIteratorOutsideRepeatIsError) {
    auto program = parse(
        "let n = 1\n"
        "repeat n as i\n"
        "end\n"
        "run \"x\" when i > 0\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

// --- Reassignment tests ---

TEST(ValidatorTest, ReassignLetBindingValidates) {
    auto program = parse(
        "let n = 1\n"
        "n = n + 1\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ReassignUndeclaredIsError) {
    auto program = parse("n = 1\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, ReassignAskAnswerIsError) {
    auto program = parse(
        "ask name \"name?\" string\n"
        "name = \"other\"\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, ReassignPathAliasIsError) {
    auto program = parse(
        "mkdir foo as bar\n"
        "bar = \"baz\"\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, ReassignRepeatIteratorIsError) {
    auto program = parse(
        "let n = 3\n"
        "repeat n as i\n"
        "  i = 0\n"
        "end\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, ReassignOuterLetFromRepeatValidates) {
    auto program = parse(
        "let total = 0\n"
        "let n = 3\n"
        "repeat n as i\n"
        "  total = total + i\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ReassignOutOfScopeLetIsError) {
    auto program = parse(
        "let n = 3\n"
        "repeat n as i\n"
        "  let local = 0\n"
        "end\n"
        "local = 1\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

// --- String-literal template scoping ---

TEST(ValidatorTest, TemplateInLetReferencesDeclaredVar) {
    auto program = parse(
        "let n = 1\n"
        "let m = \"value={n}\"\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, TemplateReferencingUndeclaredOutOfScopeIsError) {
    auto program = parse(
        "let n = 3\n"
        "repeat n as i\n"
        "  let local = 0\n"
        "end\n"
        "let s = \"value={local}\"\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, TemplateInWhenInRunIsWalked) {
    auto program = parse(
        "let n = 1\n"
        "repeat n as i\n"
        "end\n"
        "run \"echo {i}\"\n");
    EXPECT_THROW(validate(program), spudplate::SemanticError);
}

TEST(ValidatorTest, ComposedRulesEndToEnd) {
    // Exercises Part A (repeat when), Part B (no ask-in-repeat - valid case),
    // Part C (nested let scoping), and Part E (bool-equivalent alias when).
    auto program = parse(
        "ask use_ci \"use ci?\" bool\n"
        "ask n \"n?\" int\n"
        "mkdir ci_dir when use_ci as ci_path\n"
        "repeat n as i when use_ci\n"
        "  let j = i + 1\n"
        "end\n"
        "mkdir ci_path/out when use_ci == true\n");
    EXPECT_NO_THROW(validate(program));
}
