#include "spudplate/cli.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "spudplate/binary_serializer.h"
#include "spudplate/bundler.h"
#include "spudplate/cli_internal.h"
#include "spudplate/interpreter.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/spudpack.h"
#include "spudplate/validator.h"

namespace spudplate {

namespace cli_internal {

RenameFn& install_rename_fn() {
    static RenameFn fn = [](const std::filesystem::path& a,
                            const std::filesystem::path& b) {
        std::filesystem::rename(a, b);
    };
    return fn;
}

}  // namespace cli_internal

namespace {

void print_usage(std::ostream& err) {
    err << "usage: spudplate <command> [args...]\n"
        << "  install [--yes] <file.spud>     validate and store a template;\n"
        << "                                  prompts before overwriting an existing one\n"
        << "  run [--dry-run] [--yes] <name|file.spud>\n"
        << "                                  run an installed template by name,\n"
        << "                                  or run a .spud file directly\n"
        << "  validate <file.spud>            parse and validate without installing\n"
        << "  list                            list installed templates\n"
        << "  inspect <name>                  print the source of an installed template\n"
        << "  uninstall <name>                remove an installed template\n"
        << "  version                         print the spudplate version\n"
        << "  update [--yes]                  fetch and install the latest spudplate release\n";
}

// Resolve the directory templates are installed into. Honours, in order:
//   1. `SPUDPLATE_HOME` - explicit override (used by tests and dev setups).
//   2. `XDG_DATA_HOME/spudplate` - the XDG Base Directory standard.
//   3. `$HOME/.local/share/spudplate` - XDG default when the env var is unset.
// Returns an empty path if none of those sources is available so the caller
// can surface a usable error.
std::filesystem::path install_dir() {
    if (const char* env = std::getenv("SPUDPLATE_HOME"); env != nullptr && *env != '\0') {
        return env;
    }
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "spudplate";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / "spudplate";
    }
    return {};
}

// Heuristic: an argument that contains a slash or ends with `.spud`/`.spp`
// is treated as a path. Anything else is an installed-template name.
bool looks_like_path(const std::string& arg) {
    if (arg.find('/') != std::string::npos) {
        return true;
    }
    auto ends_with = [&](std::string_view ext) {
        return arg.size() >= ext.size() &&
               arg.compare(arg.size() - ext.size(), ext.size(), ext) == 0;
    };
    return ends_with(".spud") || ends_with(".spp");
}

bool ends_with_spp(const std::filesystem::path& p) {
    return p.extension() == ".spp";
}

bool ends_with_spud(const std::filesystem::path& p) {
    return p.extension() == ".spud";
}

void print_error(std::ostream& err, const std::string& file, const char* kind,
                 int line, int column, const char* message) {
    err << file << ":" << line << ":" << column << ": " << kind << ": "
        << message << "\n";
}

// Resolve a `run` argument to an on-disk file path. If the argument
// looks like a path it is returned as-is; otherwise it is treated as an
// installed template name and resolves to `<install_dir>/<name>.spp`.
// Returns the empty path and writes a diagnostic to `err` if the install
// dir cannot be located.
std::filesystem::path resolve_template_arg(const std::string& arg,
                                           std::ostream& err) {
    if (looks_like_path(arg)) {
        return arg;
    }
    auto dir = install_dir();
    if (dir.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return {};
    }
    return dir / (arg + ".spp");
}

// Diagnose the legacy directory-shaped install (pre-`.spp` layout) and
// emit a stderr warning. Returns true iff a legacy directory was found
// at `<install_dir>/<name>/template.spud`.
bool legacy_install_exists(const std::filesystem::path& home,
                           const std::string& name) {
    return std::filesystem::is_regular_file(home / name / "template.spud");
}

// Parse and validate `source` so install rejects broken templates before
// they are copied. Returns true on success; on failure writes a `<file>:`
// diagnostic to `err` and sets `exit_code` to the appropriate error.
bool validate_template_source(const std::string& source,
                              const std::string& file_label,
                              std::ostream& err, int& exit_code) {
    try {
        Lexer lexer(source);
        Parser parser(std::move(lexer));
        Program program = parser.parse();
        validate(program);
    } catch (const ParseError& e) {
        print_error(err, file_label, "parse error", e.line(), e.column(),
                    e.what());
        exit_code = 2;
        return false;
    } catch (const SemanticError& e) {
        print_error(err, file_label, "semantic error", e.line(), e.column(),
                    e.what());
        exit_code = 3;
        return false;
    }
    return true;
}

// Read a single line from stdin and return true if it parses as
// affirmative (`y`, `yes`, case-insensitive). Empty input returns false so
// the prompt's `[y/N]` default is "no".
bool confirm_yes_no(std::ostream& out, const std::string& prompt) {
    out << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }
    std::string lc;
    lc.reserve(line.size());
    for (char c : line) {
        lc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lc == "y" || lc == "yes";
}

int cmd_install(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    bool skip_confirm = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (arg == "--yes" || arg == "-y") {
            skip_confirm = true;
            ++positional_start;
        } else {
            break;
        }
    }
    if (argc - positional_start != 1) {
        print_usage(err);
        return 1;
    }

    std::filesystem::path source_path{argv[positional_start]};
    if (ends_with_spp(source_path)) {
        err << source_path.string()
            << ": installing pre-built spudpacks is not supported in this "
               "version\n";
        return 1;
    }

    std::ifstream in(source_path, std::ios::binary);
    if (!in) {
        err << source_path.string() << ": cannot open: " << std::strerror(errno)
            << "\n";
        return 5;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    Program program;
    try {
        Lexer lexer(source);
        Parser parser(std::move(lexer));
        program = parser.parse();
        validate(program);
    } catch (const ParseError& e) {
        print_error(err, source_path.string(), "parse error", e.line(),
                    e.column(), e.what());
        return 2;
    } catch (const SemanticError& e) {
        print_error(err, source_path.string(), "semantic error", e.line(),
                    e.column(), e.what());
        return 3;
    }

    std::vector<SpudpackAsset> assets;
    try {
        BundleResult bundled =
            bundle_assets(program, source_path.parent_path());
        assets = std::move(bundled.assets);
    } catch (const BundleError& e) {
        print_error(err, source_path.string(), "bundle error", e.line(),
                    e.column(), e.what());
        return 3;
    }

    std::vector<std::uint8_t> program_bytes;
    try {
        program_bytes = serialize_program(program);
    } catch (const BinarySerializeError& e) {
        err << source_path.string() << ": " << e.what() << "\n";
        return 3;
    }

    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }

    std::string name = source_path.stem().string();
    if (name.empty()) {
        err << source_path.string() << ": cannot derive template name\n";
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(home, ec);
    if (ec) {
        err << home.string()
            << ": cannot create install directory: " << ec.message() << "\n";
        return 1;
    }

    std::filesystem::path final_path = home / (name + ".spp");
    std::filesystem::path tmp_path = home / (name + ".spp.tmp");

    // Step 7a - refuse to clobber a stray non-regular file/dir that
    // happens to sit at the destination. Surfacing this here gives a
    // friendlier diagnostic than letting the rename fail later.
    if (std::filesystem::exists(final_path) &&
        !std::filesystem::is_regular_file(final_path)) {
        err << "refusing to install: '" << final_path.filename().string()
            << "' exists but is not a regular file\n";
        return 1;
    }

    // Step 7b - overwrite prompt fires when either form (`<name>.spp` or
    // legacy `<name>/`) is present. Capture the legacy flag now so the
    // step-11 cleanup runs even when the prompt is suppressed by `--yes`.
    bool spp_existed = std::filesystem::exists(final_path);
    bool legacy_existed = legacy_install_exists(home, name);
    if ((spp_existed || legacy_existed) && !skip_confirm) {
        if (!confirm_yes_no(
                out, "this will overwrite the existing '" + name +
                         "' template. continue? [y/N] ")) {
            out << "aborted\n";
            return 0;
        }
    }

    Spudpack pack;
    pack.source = std::move(source);
    pack.program_bytes = std::move(program_bytes);
    pack.assets = std::move(assets);

    try {
        spudpack_write_file(tmp_path, pack);
    } catch (const std::exception& e) {
        std::filesystem::remove(tmp_path, ec);
        err << final_path.string() << ": cannot write: " << e.what() << "\n";
        return 1;
    }

    try {
        cli_internal::install_rename_fn()(tmp_path, final_path);
    } catch (const std::exception& e) {
        std::filesystem::remove(tmp_path, ec);
        err << final_path.string() << ": cannot finalise install: " << e.what()
            << "\n";
        return 1;
    }

    if (legacy_existed) {
        err << "replacing legacy install '" << name << "'\n";
        std::error_code rm_ec;
        std::filesystem::remove_all(home / name, rm_ec);
        if (rm_ec) {
            err << "warning: install succeeded but could not remove legacy '"
                << name << "': " << rm_ec.message() << "\n";
        }
    }

    out << (spp_existed ? "reinstalled " : "installed ") << name << " to "
        << final_path.string() << "\n";
    return 0;
}

int cmd_version(std::ostream& out) {
    out << "spudplate v" << SPUDPLATE_VERSION_STRING << "\n";
    return 0;
}

int cmd_update(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    bool skip_confirm = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (arg == "--yes" || arg == "-y") {
            skip_confirm = true;
            ++positional_start;
        } else {
            break;
        }
    }
    if (argc - positional_start != 0) {
        print_usage(err);
        return 1;
    }

    out << "current version: v" << SPUDPLATE_VERSION_STRING << "\n";
    if (!skip_confirm) {
        if (!confirm_yes_no(
                out, "this will download and install the latest spudplate "
                     "from github. continue? [y/N] ")) {
            out << "aborted\n";
            return 0;
        }
    }

    // The default command is the same one the README documents. Tests and
    // anyone with their own mirror can override it via the environment.
    const char* override_cmd = std::getenv("SPUDPLATE_UPDATE_COMMAND");
    std::string cmd =
        override_cmd != nullptr && *override_cmd != '\0'
            ? std::string{override_cmd}
            : std::string{
                  "curl -fsSL "
                  "https://raw.githubusercontent.com/spuddydev/spudplate/"
                  "main/install.sh | sh"};
    out << "running: " << cmd << "\n";
    int status = std::system(cmd.c_str());
    if (status != 0) {
        err << "update command failed (status " << status << ")\n";
        return 1;
    }
    out << "done. run 'spudplate version' to confirm.\n";
    return 0;
}

int cmd_validate(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc - 2 != 1) {
        print_usage(err);
        return 1;
    }
    std::filesystem::path source_path{argv[2]};
    if (ends_with_spp(source_path)) {
        err << source_path.string()
            << ": installing pre-built spudpacks is not supported in this "
               "version\n";
        return 1;
    }
    std::ifstream in(source_path);
    if (!in) {
        err << source_path.string() << ": cannot open: " << std::strerror(errno)
            << "\n";
        return 5;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    int exit_code = 0;
    if (!validate_template_source(source, source_path.string(), err,
                                  exit_code)) {
        return exit_code;
    }
    out << "ok\n";
    return 0;
}

int cmd_list(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc != 2) {
        print_usage(err);
        return 1;
    }
    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }
    if (!std::filesystem::is_directory(home)) {
        return 0;  // No installs yet - empty output, success.
    }
    std::vector<std::string> names;
    std::vector<std::string> shadowed_legacy;
    std::vector<std::string> only_legacy;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(home, ec)) {
        if (entry.is_regular_file() && ends_with_spp(entry.path())) {
            names.push_back(entry.path().stem().string());
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(home, ec)) {
        if (!entry.is_directory()) continue;
        std::string n = entry.path().filename().string();
        if (!std::filesystem::is_regular_file(entry.path() / "template.spud")) {
            continue;
        }
        if (std::find(names.begin(), names.end(), n) != names.end()) {
            shadowed_legacy.push_back(n);
        } else {
            only_legacy.push_back(n);
        }
    }
    std::sort(names.begin(), names.end());
    std::sort(shadowed_legacy.begin(), shadowed_legacy.end());
    std::sort(only_legacy.begin(), only_legacy.end());
    for (const auto& n : names) {
        out << n << "\n";
    }
    for (const auto& n : shadowed_legacy) {
        err << "warning: legacy install '" << n
            << "' is shadowed by '" << n << ".spp'\n";
    }
    for (const auto& n : only_legacy) {
        err << "warning: legacy install '" << n
            << "'; reinstall to upgrade\n";
    }
    return 0;
}

int cmd_inspect(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc - 2 != 1) {
        print_usage(err);
        return 1;
    }
    std::string name{argv[2]};
    if (looks_like_path(name)) {
        err << "inspect takes an installed template name, not a path\n";
        return 1;
    }
    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }
    std::filesystem::path spp_path = home / (name + ".spp");
    if (std::filesystem::exists(spp_path) &&
        !std::filesystem::is_regular_file(spp_path)) {
        err << "refusing to read: '" << name
            << ".spp' exists but is not a regular file\n";
        return 1;
    }
    if (!std::filesystem::exists(spp_path)) {
        if (legacy_install_exists(home, name)) {
            err << "legacy install '" << name << "'; reinstall to upgrade\n";
            return 1;
        }
        err << name << ": not installed\n";
        return 5;
    }
    try {
        Spudpack pack = spudpack_read_file(spp_path);
        out.write(pack.source.data(),
                  static_cast<std::streamsize>(pack.source.size()));
    } catch (const SpudpackError& e) {
        err << name << ": " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmd_uninstall(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc - 2 != 1) {
        print_usage(err);
        return 1;
    }
    std::string name{argv[2]};
    if (looks_like_path(name)) {
        err << "uninstall takes an installed template name, not a path\n";
        return 1;
    }
    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }
    std::filesystem::path spp_path = home / (name + ".spp");
    std::filesystem::path legacy_dir = home / name;

    bool removed_anything = false;
    std::error_code ec;
    if (std::filesystem::exists(spp_path)) {
        std::filesystem::remove_all(spp_path, ec);
        if (ec) {
            err << name << ": cannot remove: " << ec.message() << "\n";
            return 1;
        }
        removed_anything = true;
    }
    if (legacy_install_exists(home, name)) {
        std::error_code rm_ec;
        std::filesystem::remove_all(legacy_dir, rm_ec);
        if (rm_ec) {
            err << name << ": cannot remove legacy: " << rm_ec.message()
                << "\n";
            return 1;
        }
        removed_anything = true;
    }
    if (!removed_anything) {
        err << name << ": not installed\n";
        return 5;
    }
    out << "uninstalled " << name << "\n";
    return 0;
}

int cmd_run(int argc, char* argv[], std::ostream& out, std::ostream& err,
            Prompter& prompter) {
    bool dry_run_mode = false;
    bool skip_authorization = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (arg == "--dry-run") {
            dry_run_mode = true;
            ++positional_start;
        } else if (arg == "--yes" || arg == "-y") {
            skip_authorization = true;
            ++positional_start;
        } else {
            break;
        }
    }
    if (argc - positional_start != 1) {
        print_usage(err);
        return 1;
    }

    std::string raw_arg{argv[positional_start]};
    // Three legal shapes: bare name (resolves to a `.spp` under the install
    // root), path to a `.spud` source (cwd-relative assets), or path to a
    // pre-built `.spp` (bundled assets). The bare-name path additionally
    // detects a directory-shaped legacy install and surfaces a precise
    // diagnostic before any open is attempted.
    enum class RunShape { InstalledSpp, SpudFile, SpudpackFile };
    RunShape shape;
    if (!looks_like_path(raw_arg)) {
        shape = RunShape::InstalledSpp;
    } else if (ends_with_spud(std::filesystem::path(raw_arg))) {
        shape = RunShape::SpudFile;
    } else {
        shape = RunShape::SpudpackFile;
    }

    std::filesystem::path file_path;
    if (shape != RunShape::InstalledSpp) {
        file_path = raw_arg;
    } else {
        std::filesystem::path home = install_dir();
        if (home.empty()) {
            err << "cannot determine install directory: set SPUDPLATE_HOME, "
                   "XDG_DATA_HOME, or HOME\n";
            return 1;
        }
        file_path = home / (raw_arg + ".spp");

        if (!std::filesystem::exists(file_path) &&
            legacy_install_exists(home, raw_arg)) {
            err << "legacy install '" << raw_arg
                << "'; reinstall to upgrade\n";
            return 1;
        }
        if (std::filesystem::exists(file_path) &&
            !std::filesystem::is_regular_file(file_path)) {
            err << "refusing to read: '" << raw_arg
                << ".spp' exists but is not a regular file\n";
            return 1;
        }
    }

    Program program;
    Spudpack pack;  // outlives `provider` and the run() call
    std::string source;
    bool have_pack = false;

    if (shape == RunShape::SpudFile) {
        std::ifstream in(file_path);
        if (!in) {
            err << file_path.string() << ": cannot open: " << std::strerror(errno)
                << "\n";
            return 5;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        source = buffer.str();
        try {
            Lexer lexer(source);
            Parser parser(std::move(lexer));
            program = parser.parse();
        } catch (const ParseError& e) {
            print_error(err, file_path.string(), "parse error", e.line(),
                        e.column(), e.what());
            return 2;
        }
    } else {
        if (!std::filesystem::exists(file_path)) {
            err << file_path.string() << ": cannot open: "
                << std::strerror(ENOENT) << "\n";
            return 5;
        }
        try {
            pack = spudpack_read_file(file_path);
            program = deserialize_program(pack.program_bytes.data(),
                                          pack.program_bytes.size(),
                                          pack.version);
            have_pack = true;
        } catch (const SpudpackError& e) {
            err << file_path.string() << ": " << e.what() << "\n";
            return 1;
        } catch (const BinaryDeserializeError& e) {
            err << file_path.string() << ": " << e.what() << "\n";
            return 1;
        }
    }

    try {
        validate(program);
    } catch (const SemanticError& e) {
        print_error(err, file_path.string(), "semantic error", e.line(),
                    e.column(), e.what());
        return 3;
    }

    std::optional<AssetMapSourceProvider> provider;
    const SourceProvider* source_ptr = nullptr;
    if (have_pack) {
        provider.emplace(pack.assets);
        source_ptr = &*provider;
    }

    try {
        if (dry_run_mode) {
            dry_run(program, prompter, out, /*ascii_only=*/!locale_is_utf8(),
                    source_ptr);
        } else {
            run(program, prompter, skip_authorization, source_ptr);
        }
    } catch (const RuntimeError& e) {
        print_error(err, file_path.string(), "runtime error", e.line(),
                    e.column(), e.what());
        return 4;
    }

    return 0;
}

}  // namespace

int cli_main(int argc, char* argv[], std::ostream& out, std::ostream& err,
             Prompter& prompter) {
    if (argc < 2) {
        print_usage(err);
        return 1;
    }
    std::string subcommand{argv[1]};
    if (subcommand == "version" || subcommand == "--version") {
        return cmd_version(out);
    }
    if (subcommand == "update") {
        return cmd_update(argc, argv, out, err);
    }
    if (subcommand == "run") {
        return cmd_run(argc, argv, out, err, prompter);
    }
    if (subcommand == "install") {
        return cmd_install(argc, argv, out, err);
    }
    if (subcommand == "validate" || subcommand == "check") {
        return cmd_validate(argc, argv, out, err);
    }
    if (subcommand == "list") {
        return cmd_list(argc, argv, out, err);
    }
    if (subcommand == "inspect") {
        return cmd_inspect(argc, argv, out, err);
    }
    if (subcommand == "uninstall") {
        return cmd_uninstall(argc, argv, out, err);
    }
    print_usage(err);
    return 1;
}

}  // namespace spudplate
