#include "spudplate/bundler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "test_helpers.h"

using spudplate::BundleError;
using spudplate::BundleResult;
using spudplate::Lexer;
using spudplate::Parser;
using spudplate::Program;
using spudplate::SpudpackAsset;
using spudplate::bundle_assets;
using spudplate::test::TmpDir;

namespace fs = std::filesystem;

namespace {

Program parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(std::move(lexer));
    return parser.parse();
}

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << body;
}

const SpudpackAsset* find_asset(const BundleResult& r, const std::string& path) {
    auto it = std::find_if(r.assets.begin(), r.assets.end(),
                           [&](const SpudpackAsset& a) { return a.path == path; });
    return it == r.assets.end() ? nullptr : &*it;
}

}  // namespace

// --- happy path ------------------------------------------------------------

TEST(Bundler, FileFromRegularFile) {
    TmpDir tmp;
    write_file(tmp.path() / "tpl/main.cpp", "int main() {}\n");
    Program p = parse("file out/main.cpp from tpl/main.cpp\n");

    BundleResult r = bundle_assets(p, tmp.path());
    ASSERT_EQ(r.assets.size(), 1u);
    EXPECT_EQ(r.assets[0].path, "tpl/main.cpp");
    EXPECT_EQ(std::string(r.assets[0].data.begin(), r.assets[0].data.end()),
              "int main() {}\n");
}

TEST(Bundler, MkdirFromDirectoryWalksTree) {
    TmpDir tmp;
    write_file(tmp.path() / "src/main.cpp", "x\n");
    write_file(tmp.path() / "src/util/util.cpp", "y\n");
    Program p = parse("mkdir project from src\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_NE(find_asset(r, "src/main.cpp"), nullptr);
    EXPECT_NE(find_asset(r, "src/util/util.cpp"), nullptr);
}

TEST(Bundler, CopySourceMustBeDirectory) {
    TmpDir tmp;
    write_file(tmp.path() / "regular.txt", "x\n");
    fs::create_directories(tmp.path() / "dst");
    Program p = parse("copy regular.txt into dst\n");

    EXPECT_THROW(bundle_assets(p, tmp.path()), BundleError);
}

TEST(Bundler, CopyDirectoryWalks) {
    TmpDir tmp;
    write_file(tmp.path() / "snippets/a.txt", "a\n");
    write_file(tmp.path() / "snippets/b.txt", "b\n");
    fs::create_directories(tmp.path() / "dst");
    Program p = parse("copy snippets into dst\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_NE(find_asset(r, "snippets/a.txt"), nullptr);
    EXPECT_NE(find_asset(r, "snippets/b.txt"), nullptr);
}

TEST(Bundler, RepeatBodyIsWalked) {
    TmpDir tmp;
    write_file(tmp.path() / "wk/body.txt", "tpl\n");
    Program p = parse(
        "ask weeks \"How many?\" int default 1\n"
        "repeat weeks as i\n"
        "  file out_{i}/body.txt from wk/body.txt\n"
        "end\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_NE(find_asset(r, "wk/body.txt"), nullptr);
}

TEST(Bundler, NonAssetStatementsAreSkipped) {
    TmpDir tmp;
    Program p = parse(
        "ask name \"Name?\" string\n"
        "let upper_name = upper(name)\n"
        "include foo when name == \"x\"\n"
        "run \"echo hi\"\n"
        "file inline.txt content \"hi\"\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_TRUE(r.assets.empty());
}

// --- path classification ---------------------------------------------------

TEST(Bundler, RejectsLeadingDynamicSegment) {
    TmpDir tmp;
    Program p = parse(
        "ask name \"Name?\" string\n"
        "file out.txt from {name}/foo\n");
    try {
        bundle_assets(p, tmp.path());
        FAIL() << "expected throw";
    } catch (const BundleError& e) {
        EXPECT_NE(std::string(e.what()).find("dynamic"), std::string::npos);
        EXPECT_EQ(e.line(), 2);
    }
}

TEST(Bundler, RejectsDynamicSegmentMidFilename) {
    TmpDir tmp;
    fs::create_directories(tmp.path() / "tpl");
    Program p = parse(
        "ask n \"n?\" int default 1\n"
        "file out.txt from tpl/file_{n}\n");
    // tpl/file_{n} - the literal prefix "tpl/file_" does not end with /,
    // so this is the mid-filename-dynamic case.
    EXPECT_THROW(bundle_assets(p, tmp.path()), BundleError);
}

TEST(Bundler, AcceptsDynamicSuffixAfterTrailingSlash) {
    TmpDir tmp;
    write_file(tmp.path() / "tpl/a.txt", "a\n");
    write_file(tmp.path() / "tpl/b.txt", "b\n");
    Program p = parse(
        "ask choice \"choice?\" string default \"a.txt\"\n"
        "file out.txt from tpl/{choice}\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_NE(find_asset(r, "tpl/a.txt"), nullptr);
    EXPECT_NE(find_asset(r, "tpl/b.txt"), nullptr);
}

// --- empty-leaf and dedup --------------------------------------------------

TEST(Bundler, EmptyLeafDirRecorded) {
    TmpDir tmp;
    fs::create_directories(tmp.path() / "scaffold/logs");  // no files
    Program p = parse("mkdir project from scaffold\n");

    BundleResult r = bundle_assets(p, tmp.path());
    const SpudpackAsset* a = find_asset(r, "scaffold/logs/");
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->data.empty());
}

TEST(Bundler, NestedEmptyOnlyDeepestRecorded) {
    TmpDir tmp;
    fs::create_directories(tmp.path() / "scaffold/a/b/c");  // chain of empties
    Program p = parse("mkdir project from scaffold\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_NE(find_asset(r, "scaffold/a/b/c/"), nullptr);
    EXPECT_EQ(find_asset(r, "scaffold/a/"), nullptr);
    EXPECT_EQ(find_asset(r, "scaffold/a/b/"), nullptr);
}

TEST(Bundler, DuplicateAssetsCollapseWhenIdentical) {
    TmpDir tmp;
    write_file(tmp.path() / "shared/body.txt", "same\n");
    Program p = parse(
        "file out_a/body.txt from shared/body.txt\n"
        "file out_b/body.txt from shared/body.txt\n");

    BundleResult r = bundle_assets(p, tmp.path());
    EXPECT_EQ(r.assets.size(), 1u);
}

// --- escape, file types ----------------------------------------------------

TEST(Bundler, RejectsSourceOutsideRoot) {
    TmpDir tmp;
    fs::create_directories(tmp.path() / "child");
    write_file(tmp.path() / "sibling.txt", "x\n");
    Program p = parse("file out.txt from ../sibling.txt\n");

    EXPECT_THROW(bundle_assets(p, tmp.path() / "child"), BundleError);
}

TEST(Bundler, RejectsFifo) {
    TmpDir tmp;
    fs::create_directories(tmp.path() / "src");
    fs::path fifo = tmp.path() / "src" / "pipe";
    if (mkfifo(fifo.c_str(), 0644) != 0) {
        GTEST_SKIP() << "mkfifo failed; skipping fifo rejection test";
    }
    Program p = parse("mkdir project from src\n");
    EXPECT_THROW(bundle_assets(p, tmp.path()), BundleError);
}

// --- symlinks --------------------------------------------------------------

TEST(Bundler, SymlinkToFileFollowed) {
    TmpDir tmp;
    write_file(tmp.path() / "real/file.txt", "hello\n");
    fs::create_directories(tmp.path() / "tree");
    fs::create_symlink(tmp.path() / "real" / "file.txt",
                       tmp.path() / "tree" / "link.txt");

    Program p = parse("mkdir project from tree\n");
    BundleResult r = bundle_assets(p, tmp.path());
    const SpudpackAsset* a = find_asset(r, "tree/link.txt");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(std::string(a->data.begin(), a->data.end()), "hello\n");
}

TEST(Bundler, SymlinkLoopBroken) {
    TmpDir tmp;
    fs::create_directories(tmp.path() / "loop/a");
    fs::create_directory_symlink(tmp.path() / "loop", tmp.path() / "loop/a/back");

    Program p = parse("mkdir project from loop\n");
    // Should not infinite-recurse. The walker may produce zero assets (only
    // the dir loop) or report an error - either way it must terminate.
    try {
        bundle_assets(p, tmp.path());
    } catch (const BundleError&) {
        // Acceptable - escaping or unsupported types may surface here.
    }
}
