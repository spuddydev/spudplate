#ifndef SPUDPLATE_TESTS_TEST_HELPERS_H
#define SPUDPLATE_TESTS_TEST_HELPERS_H

#include <filesystem>

#include "spudplate/ast.h"

namespace spudplate::test {

// Structural equality for AST nodes, used by round-trip assertions in the
// binary-serializer tests. Compares every field including line/column. The
// helpers are deliberately test-only - adding `operator==` to `ast.h` would
// pin equality semantics into the public surface, which prior attempts
// (PR #50, since reverted) showed to be more trouble than it is worth.

bool programs_equal(const Program& a, const Program& b);
bool stmts_equal(const Stmt& a, const Stmt& b);
bool exprs_equal(const Expr& a, const Expr& b);
bool path_exprs_equal(const PathExpr& a, const PathExpr& b);
bool file_sources_equal(const FileSource& a, const FileSource& b);

// Per-test scratch directory. Created on construction with a randomised name
// under the system temp dir; the previous working directory is restored and
// the directory is wiped on destruction. `chdir`-into-the-tempdir is opt-in
// - pass `chdir = false` for tests that should keep the caller's cwd.
class TmpDir {
  public:
    explicit TmpDir(bool chdir = true);
    TmpDir(const TmpDir&) = delete;
    TmpDir& operator=(const TmpDir&) = delete;
    ~TmpDir();

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
    std::filesystem::path prev_;
    bool chdir_;
};

}  // namespace spudplate::test

#endif  // SPUDPLATE_TESTS_TEST_HELPERS_H
