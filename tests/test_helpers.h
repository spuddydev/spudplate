#ifndef SPUDPLATE_TESTS_TEST_HELPERS_H
#define SPUDPLATE_TESTS_TEST_HELPERS_H

#include "spudplate/ast.h"

namespace spudplate::test {

// Structural equality for AST nodes, used by round-trip assertions in the
// binary-serializer tests. Compares every field including line/column. The
// helpers are deliberately test-only — adding `operator==` to `ast.h` would
// pin equality semantics into the public surface, which prior attempts
// (PR #50, since reverted) showed to be more trouble than it is worth.

bool programs_equal(const Program& a, const Program& b);
bool stmts_equal(const Stmt& a, const Stmt& b);
bool exprs_equal(const Expr& a, const Expr& b);
bool path_exprs_equal(const PathExpr& a, const PathExpr& b);
bool file_sources_equal(const FileSource& a, const FileSource& b);

}  // namespace spudplate::test

#endif  // SPUDPLATE_TESTS_TEST_HELPERS_H
