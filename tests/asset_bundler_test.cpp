#include "spudplate/asset_bundler.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "spudplate/lexer.h"
#include "spudplate/parser.h"

using namespace spudplate;
namespace fs = std::filesystem;

namespace {

class TmpDir {
  public:
    TmpDir() {
        std::random_device rd;
        std::stringstream ss;
        ss << "spudplate-bundler-" << std::hex << rd() << rd();
        path_ = fs::temp_directory_path() / ss.str();
        fs::create_directories(path_);
    }
    TmpDir(const TmpDir&) = delete;
    TmpDir& operator=(const TmpDir&) = delete;
    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }

  private:
    fs::path path_;
};

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << content;
}

Program parse(const std::string& src) {
    Parser parser(Lexer{src});
    return parser.parse();
}

}  // namespace

TEST(BundlerTest, FullyStaticPathCopiesExactFile) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path() / "base" / "main.cpp", "int main(){}\n");

    auto p = parse("file project/main.cpp from base/main.cpp\n");
    auto report = bundle_assets(p, source_root.path(), dest.path());

    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "base" / "main.cpp"));
    EXPECT_EQ(report.copied_files.size(), 1u);
}

TEST(BundlerTest, StaticPrefixBundlesEntirePrefixDir) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path() / "base" / "a.cpp", "a\n");
    write_file(source_root.path() / "base" / "sub" / "b.cpp", "b\n");

    // base/{module}/main.cpp — prefix is `base`, bundle the whole dir.
    auto p = parse(
        "ask module \"?\" string default \"x\"\n"
        "file project/main.cpp from base/{module}/main.cpp\n");
    auto report = bundle_assets(p, source_root.path(), dest.path());

    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "base" / "a.cpp"));
    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "base" / "sub" / "b.cpp"));
}

TEST(BundlerTest, NoStaticPrefixThrows) {
    TmpDir source_root;
    TmpDir dest;
    auto p = parse(
        "ask root \"?\" string default \"a\"\n"
        "file project/main.cpp from {root}/main.cpp\n");
    EXPECT_THROW(bundle_assets(p, source_root.path(), dest.path()),
                 BundleError);
}

TEST(BundlerTest, DynamicMidFilenameWithoutSlashThrows) {
    TmpDir source_root;
    TmpDir dest;
    auto p = parse(
        "ask x \"?\" string default \"a\"\n"
        "file out.cpp from foo{x}.cpp\n");
    EXPECT_THROW(bundle_assets(p, source_root.path(), dest.path()),
                 BundleError);
}

TEST(BundlerTest, MissingSourceThrows) {
    TmpDir source_root;
    TmpDir dest;
    auto p = parse("file dst from missing.cpp\n");
    EXPECT_THROW(bundle_assets(p, source_root.path(), dest.path()),
                 BundleError);
}

TEST(BundlerTest, PathTraversalThrows) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path().parent_path() / "outside.cpp", "x\n");
    auto p = parse("file dst from ../outside.cpp\n");
    EXPECT_THROW(bundle_assets(p, source_root.path(), dest.path()),
                 BundleError);
}

TEST(BundlerTest, MkdirFromAndCopyAreBundled) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path() / "templates" / "x.cpp", "x\n");
    write_file(source_root.path() / "extras" / "y.cpp", "y\n");

    auto p = parse(
        "mkdir templates from templates as t\n"
        "copy extras into t\n");
    bundle_assets(p, source_root.path(), dest.path());

    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "templates" / "x.cpp"));
    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "extras" / "y.cpp"));
}

TEST(BundlerTest, RepeatBodyAssetsAreBundled) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path() / "base" / "a.cpp", "a\n");

    auto p = parse(
        "ask n \"?\" int default 1\n"
        "repeat n as i\n"
        "  file out_{i}.cpp from base/a.cpp\n"
        "end\n");
    bundle_assets(p, source_root.path(), dest.path());

    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "base" / "a.cpp"));
}

TEST(BundlerTest, DuplicatePathsAreDeduped) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path() / "base" / "a.cpp", "a\n");

    auto p = parse(
        "file out_a from base/a.cpp\n"
        "file out_b from base/a.cpp\n");
    auto report = bundle_assets(p, source_root.path(), dest.path());

    EXPECT_EQ(report.copied_files.size(), 1u);
}

TEST(BundlerTest, PrefixCoversNestedReferences) {
    TmpDir source_root;
    TmpDir dest;
    write_file(source_root.path() / "base" / "x" / "a.cpp", "a\n");
    write_file(source_root.path() / "base" / "y" / "b.cpp", "b\n");

    // First reference bundles the whole `base/` tree; second one is covered.
    auto p = parse(
        "ask m \"?\" string default \"x\"\n"
        "file out_a from base/{m}/a.cpp\n"
        "file out_b from base/y/b.cpp\n");
    auto report = bundle_assets(p, source_root.path(), dest.path());

    // Both files end up under assets/base/, but only the prefix walk runs.
    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "base" / "x" / "a.cpp"));
    EXPECT_TRUE(fs::exists(dest.path() / "assets" / "base" / "y" / "b.cpp"));
}
