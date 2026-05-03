#include "spudplate/cli.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

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

void print_usage(std::ostream& out) {
    out << "usage: spudplate <command> [args...]\n"
        << "\n"
        << "  install <file.spud>      validate and store a template\n"
        << "  run <name|file>          run an installed template, or a .spud/.spp file\n"
        << "  validate <file.spud>     parse and validate without installing\n"
        << "  list                     list installed templates\n"
        << "  inspect <name>           print the source of an installed template\n"
        << "  uninstall <name>         remove an installed template\n"
        << "  version                  print the spudplate version\n"
        << "  update                   fetch and install the latest spudplate release\n"
        << "  completion <bash|zsh>    print a shell completion script\n"
        << "  self-uninstall           remove spudplate from this system\n"
        << "\n"
        << "Run 'spudplate <command> --help' for help on a specific command.\n"
        << "Use --help or -h at any level to see this guidance.\n";
}

bool is_help_flag(std::string_view arg) {
    return arg == "--help" || arg == "-h";
}

void print_help_install(std::ostream& out) {
    out << "usage: spudplate install [--yes] <file.spud>\n"
        << "\n"
        << "Validate a .spud template and store it under the install root\n"
        << "as <name>.spp. Prompts before overwriting an existing template.\n"
        << "\n"
        << "Options:\n"
        << "  --yes, -y       skip the overwrite confirmation\n";
}

void print_help_run(std::ostream& out) {
    out << "usage: spudplate run [--dry-run] [--yes] [--no-timeout] "
           "<name|file.spud|file.spp>\n"
        << "\n"
        << "Run an installed template by name, or run a .spud or .spp file\n"
        << "directly. The argument is treated as a path when it contains a\n"
        << "slash or ends with .spud or .spp; otherwise it is looked up under\n"
        << "the install root.\n"
        << "\n"
        << "Options:\n"
        << "  --dry-run       print the file tree the run would create,\n"
        << "                  without touching the filesystem\n"
        << "  --yes, -y       skip the authorisation prompt for run statements\n"
        << "  --no-timeout    disable per-statement timeouts\n";
}

void print_help_validate(std::ostream& out) {
    out << "usage: spudplate validate <file.spud>\n"
        << "\n"
        << "Parse and validate a .spud template without bundling or\n"
        << "installing. Prints 'ok' on success.\n";
}

void print_help_list(std::ostream& out) {
    out << "usage: spudplate list\n"
        << "\n"
        << "List installed templates by name, one per line.\n";
}

void print_help_inspect(std::ostream& out) {
    out << "usage: spudplate inspect <name>\n"
        << "\n"
        << "Print the original .spud source of an installed template.\n";
}

void print_help_uninstall(std::ostream& out) {
    out << "usage: spudplate uninstall <name>\n"
        << "\n"
        << "Remove an installed template from the install root.\n";
}

void print_help_version(std::ostream& out) {
    out << "usage: spudplate version\n"
        << "\n"
        << "Print the spudplate version and exit.\n";
}

void print_help_self_uninstall(std::ostream& out) {
    out << "usage: spudplate self-uninstall [--purge] [--yes]\n"
        << "\n"
        << "Remove the spudplate binary, its shell completion files, and\n"
        << "the completion block in ~/.zshrc added at install time.\n"
        << "\n"
        << "Options:\n"
        << "  --purge         also delete every installed template "
           "(.spp files)\n"
        << "  --yes, -y       skip the confirmation prompt\n";
}

void print_help_completion(std::ostream& out) {
    out << "usage: spudplate completion <bash|zsh>\n"
        << "\n"
        << "Print a shell completion script to stdout. Pipe the output\n"
        << "into your completion directory to enable tab completion of\n"
        << "subcommands, installed template names, and .spud files.\n"
        << "\n"
        << "Bash:  spudplate completion bash > "
           "~/.local/share/bash-completion/completions/spudplate\n"
        << "Zsh:   spudplate completion zsh  > ~/.zsh/completions/_spudplate\n";
}

void print_help_update(std::ostream& out) {
    out << "usage: spudplate update [--yes] [--force]\n"
        << "\n"
        << "Fetch and install the latest spudplate release. Skips the\n"
        << "download when already up to date.\n"
        << "\n"
        << "Options:\n"
        << "  --yes, -y       skip the confirmation prompt\n"
        << "  --force         download and install even if already up to date\n";
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

void print_error(std::ostream& err, const std::string& file, const char* kind, int line,
                 int column, const char* message) {
    err << file << ":" << line << ":" << column << ": " << kind << ": " << message
        << "\n";
}

// Resolve a `run` argument to an on-disk file path. If the argument
// looks like a path it is returned as-is; otherwise it is treated as an
// installed template name and resolves to `<install_dir>/<name>.spp`.
// Returns the empty path and writes a diagnostic to `err` if the install
// dir cannot be located.
std::filesystem::path resolve_template_arg(const std::string& arg, std::ostream& err) {
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
bool legacy_install_exists(const std::filesystem::path& home, const std::string& name) {
    return std::filesystem::is_regular_file(home / name / "template.spud");
}

std::size_t levenshtein(std::string_view a, std::string_view b) {
    if (a.size() < b.size()) {
        std::swap(a, b);
    }
    if (b.empty()) {
        return a.size();
    }
    std::vector<std::size_t> prev(b.size() + 1);
    std::vector<std::size_t> curr(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        prev[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        curr[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(curr);
    }
    return prev[b.size()];
}

std::vector<std::string> list_installed_names(const std::filesystem::path& home) {
    std::vector<std::string> names;
    if (!std::filesystem::is_directory(home)) {
        return names;
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(home, ec)) {
        if (entry.is_regular_file() && ends_with_spp(entry.path())) {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

std::string strip_v_prefix(std::string s) {
    if (!s.empty() && s.front() == 'v') {
        s.erase(0, 1);
    }
    return s;
}

// Resolve the latest released version by following the redirect on
// `releases/latest`. Returns the version string without a leading `v` on
// success, or empty on failure.
//
// `SPUDPLATE_LATEST_VERSION` overrides the network call. The special
// value `fail` simulates a resolve failure for tests that exercise the
// fail-open path.
std::string resolve_latest_version() {
    if (const char* env = std::getenv("SPUDPLATE_LATEST_VERSION");
        env != nullptr && *env != '\0') {
        if (std::string{env} == "fail") {
            return {};
        }
        return strip_v_prefix(env);
    }
    FILE* pipe = popen(
        "curl -fsSLI -o /dev/null -w '%{url_effective}' "
        "https://github.com/spuddydev/spudplate/releases/latest 2>/dev/null",
        "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string url;
    char buf[256];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        url += buf;
    }
    int status = pclose(pipe);
    if (status != 0) {
        return {};
    }
    while (!url.empty() &&
           (url.back() == '\n' || url.back() == '\r' ||
            url.back() == ' ' || url.back() == '\t')) {
        url.pop_back();
    }
    auto pos = url.rfind('/');
    if (pos == std::string::npos || pos + 1 >= url.size()) {
        return {};
    }
    return strip_v_prefix(url.substr(pos + 1));
}

// Returns the closest installed name to `target` if it is a clear winner
// within a typo-distance threshold that scales with name length, or empty
// otherwise. A second-best at the same distance is treated as ambiguous
// and yields no suggestion.
std::string suggest_template_name(const std::string& target,
                                  const std::vector<std::string>& names) {
    if (names.empty()) {
        return {};
    }
    std::size_t threshold = std::max<std::size_t>(2, target.size() / 3);
    std::size_t best = std::numeric_limits<std::size_t>::max();
    std::size_t second = std::numeric_limits<std::size_t>::max();
    std::string winner;
    for (const auto& n : names) {
        std::size_t d = levenshtein(target, n);
        if (d < best) {
            second = best;
            best = d;
            winner = n;
        } else if (d < second) {
            second = d;
        }
    }
    if (best > threshold) {
        return {};
    }
    if (second <= best) {
        return {};
    }
    return winner;
}

// Parse and validate `source` so install rejects broken templates before
// they are copied. Returns true on success; on failure writes a `<file>:`
// diagnostic to `err` and sets `exit_code` to the appropriate error.
bool validate_template_source(const std::string& source, const std::string& file_label,
                              std::ostream& err, int& exit_code) {
    try {
        Lexer lexer(source);
        Parser parser(std::move(lexer));
        Program program = parser.parse();
        validate(program);
    } catch (const ParseError& e) {
        print_error(err, file_label, "parse error", e.line(), e.column(), e.what());
        exit_code = 2;
        return false;
    } catch (const SemanticError& e) {
        print_error(err, file_label, "semantic error", e.line(), e.column(), e.what());
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
        if (is_help_flag(arg)) {
            print_help_install(out);
            return 0;
        }
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
        err << source_path.string() << ": cannot open: " << std::strerror(errno) << "\n";
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
        print_error(err, source_path.string(), "parse error", e.line(), e.column(),
                    e.what());
        return 2;
    } catch (const SemanticError& e) {
        print_error(err, source_path.string(), "semantic error", e.line(), e.column(),
                    e.what());
        return 3;
    }

    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
               "XDG_DATA_HOME, or HOME\n";
        return 1;
    }

    std::vector<SpudpackAsset> assets;
    std::vector<spudplate::SpudpackDep> deps;
    try {
        BundleResult bundled =
            bundle_assets(program, source_path.parent_path(), home);
        assets = std::move(bundled.assets);
        deps = std::move(bundled.deps);
    } catch (const BundleError& e) {
        print_error(err, source_path.string(), "bundle error", e.line(), e.column(),
                    e.what());
        return 3;
    }

    std::vector<std::uint8_t> program_bytes;
    try {
        program_bytes = serialize_program(program);
    } catch (const BinarySerializeError& e) {
        err << source_path.string() << ": " << e.what() << "\n";
        return 3;
    }

    std::string name = source_path.stem().string();
    if (name.empty()) {
        err << source_path.string() << ": cannot derive template name\n";
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(home, ec);
    if (ec) {
        err << home.string() << ": cannot create install directory: " << ec.message()
            << "\n";
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
        if (!confirm_yes_no(out, "this will overwrite the existing '" + name +
                                     "' template. continue? [y/N] ")) {
            out << "aborted\n";
            return 0;
        }
    }

    Spudpack pack;
    pack.source = std::move(source);
    pack.program_bytes = std::move(program_bytes);
    pack.assets = std::move(assets);
    pack.deps = std::move(deps);

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
        err << final_path.string() << ": cannot finalise install: " << e.what() << "\n";
        return 1;
    }

    if (legacy_existed) {
        err << "replacing legacy install '" << name << "'\n";
        std::error_code rm_ec;
        std::filesystem::remove_all(home / name, rm_ec);
        if (rm_ec) {
            err << "warning: install succeeded but could not remove legacy '" << name
                << "': " << rm_ec.message() << "\n";
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

const char* bash_completion_script() {
    return R"BASH(_spudplate() {
    local cur cword
    cur="${COMP_WORDS[COMP_CWORD]}"
    cword=$COMP_CWORD

    local subcommands="install run validate list inspect uninstall version update completion --help -h"

    if [ "$cword" -eq 1 ]; then
        COMPREPLY=( $(compgen -W "$subcommands" -- "$cur") )
        return
    fi

    local subcommand="${COMP_WORDS[1]}"
    case "$subcommand" in
        run)
            local installed
            installed=$(spudplate list 2>/dev/null)
            COMPREPLY=( $(compgen -W "$installed" -- "$cur") )
            COMPREPLY+=( $(compgen -f -X '!*.spud' -- "$cur") )
            COMPREPLY+=( $(compgen -f -X '!*.spp' -- "$cur") )
            ;;
        inspect|uninstall)
            local installed
            installed=$(spudplate list 2>/dev/null)
            COMPREPLY=( $(compgen -W "$installed" -- "$cur") )
            ;;
        install|validate)
            COMPREPLY=( $(compgen -f -X '!*.spud' -- "$cur") )
            ;;
        completion)
            COMPREPLY=( $(compgen -W "bash zsh" -- "$cur") )
            ;;
    esac
}
complete -F _spudplate spudplate
)BASH";
}

const char* zsh_completion_script() {
    return R"ZSH(#compdef spudplate

_spudplate() {
    local -a subcommands
    subcommands=(
        'install:validate and store a template'
        'run:run an installed template by name or a .spud/.spp file'
        'validate:parse and validate a template without installing'
        'list:list installed templates'
        'inspect:print the source of an installed template'
        'uninstall:remove an installed template'
        'version:print the spudplate version'
        'update:fetch and install the latest spudplate'
        'completion:print a shell completion script'
    )

    if (( CURRENT == 2 )); then
        _describe 'subcommand' subcommands
        return
    fi

    case "$words[2]" in
        run)
            local -a names
            names=( ${(f)"$(spudplate list 2>/dev/null)"} )
            _alternative \
                "names:installed:($names)" \
                'files:.spud or .spp:_files -g "*.(spud|spp)"'
            ;;
        inspect|uninstall)
            local -a names
            names=( ${(f)"$(spudplate list 2>/dev/null)"} )
            _describe 'installed template' names
            ;;
        install|validate)
            _files -g '*.spud'
            ;;
        completion)
            _values 'shell' bash zsh
            ;;
    esac
}

_spudplate "$@"
)ZSH";
}

// Resolve the absolute path of the running binary. `SPUDPLATE_SELF_BINARY`
// overrides for tests. On Linux we use `/proc/self/exe`; on macOS the
// dyld API. Returns an empty path on failure (in which case the caller
// reports and skips binary removal).
std::filesystem::path resolve_self_path() {
    if (const char* env = std::getenv("SPUDPLATE_SELF_BINARY");
        env != nullptr && *env != '\0') {
        return env;
    }
#ifdef __APPLE__
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return {};
    }
    std::error_code ec;
    auto canon = std::filesystem::canonical(buf, ec);
    return ec ? std::filesystem::path{buf} : canon;
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return {};
    }
    buf[n] = '\0';
    return buf;
#endif
}

std::filesystem::path home_dir() {
    if (const char* h = std::getenv("HOME"); h != nullptr && *h != '\0') {
        return h;
    }
    return {};
}

// Remove the spudplate completion block (delimited by the markers
// `setup_zshrc` writes in install.sh) from `zshrc`. Returns true if a
// block was found and removed. Does nothing and returns false if the
// file does not exist or contains no marker.
bool remove_zshrc_block(const std::filesystem::path& zshrc) {
    if (!std::filesystem::is_regular_file(zshrc)) {
        return false;
    }
    std::ifstream in(zshrc);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    const std::string start_marker = "# >>> spudplate completion >>>";
    const std::string end_marker = "# <<< spudplate completion <<<";
    auto start = content.find(start_marker);
    if (start == std::string::npos) {
        return false;
    }
    auto end = content.find(end_marker, start);
    if (end == std::string::npos) {
        return false;
    }
    end += end_marker.size();
    if (end < content.size() && content[end] == '\n') {
        ++end;
    }
    if (start > 0 && content[start - 1] == '\n') {
        --start;
    }
    content.erase(start, end - start);
    std::ofstream out(zshrc);
    if (!out) {
        return false;
    }
    out << content;
    return true;
}

std::vector<std::filesystem::path> list_installed_spp_paths(
    const std::filesystem::path& home) {
    std::vector<std::filesystem::path> paths;
    if (!std::filesystem::is_directory(home)) {
        return paths;
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(home, ec)) {
        if (entry.is_regular_file() && ends_with_spp(entry.path())) {
            paths.push_back(entry.path());
        }
    }
    return paths;
}

int cmd_self_uninstall(int argc, char* argv[], std::ostream& out,
                       std::ostream& err) {
    bool purge = false;
    bool skip_confirm = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (is_help_flag(arg)) {
            print_help_self_uninstall(out);
            return 0;
        }
        if (arg == "--purge") {
            purge = true;
            ++positional_start;
        } else if (arg == "--yes" || arg == "-y") {
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

    std::filesystem::path binary = resolve_self_path();
    std::filesystem::path home = home_dir();
    std::filesystem::path bash_completion;
    std::filesystem::path zsh_completion;
    std::filesystem::path zshrc;
    if (!home.empty()) {
        bash_completion = home / ".local" / "share" / "bash-completion" /
                          "completions" / "spudplate";
        zsh_completion = home / ".zsh" / "completions" / "_spudplate";
        zshrc = home / ".zshrc";
    }
    std::filesystem::path templates_root = install_dir();

    std::vector<std::filesystem::path> spp_paths;
    if (purge) {
        spp_paths = list_installed_spp_paths(templates_root);
    }

    out << "this will remove:\n";
    if (!binary.empty() && std::filesystem::exists(binary)) {
        out << "  binary:           " << binary.string() << "\n";
    }
    if (!bash_completion.empty() && std::filesystem::exists(bash_completion)) {
        out << "  bash completion:  " << bash_completion.string() << "\n";
    }
    if (!zsh_completion.empty() && std::filesystem::exists(zsh_completion)) {
        out << "  zsh completion:   " << zsh_completion.string() << "\n";
    }
    if (!zshrc.empty() && std::filesystem::is_regular_file(zshrc)) {
        std::ifstream in(zshrc);
        std::string contents((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        if (contents.find("# >>> spudplate completion >>>") !=
            std::string::npos) {
            out << "  zsh setup block:  in " << zshrc.string() << "\n";
        }
    }
    if (purge && !spp_paths.empty()) {
        out << "  installed templates: " << spp_paths.size() << " .spp file"
            << (spp_paths.size() == 1 ? "" : "s") << " in "
            << templates_root.string() << "\n";
    } else if (!purge && !templates_root.empty()) {
        out << "\ninstalled templates under " << templates_root.string()
            << " will be preserved (pass --purge to remove them).\n";
    }

    if (purge) {
        out << "\nwarning: --purge will permanently delete every installed "
               "template; this is irreversible\n";
    }
    if (!skip_confirm) {
        if (!confirm_yes_no(out, "continue? [y/N] ")) {
            out << "aborted\n";
            return 0;
        }
    }

    std::error_code ec;
    auto remove_if_exists = [&](const std::filesystem::path& p, const char* label) {
        if (p.empty() || !std::filesystem::exists(p)) {
            return;
        }
        if (std::filesystem::remove(p, ec)) {
            out << "  removed " << label << ": " << p.string() << "\n";
        } else {
            err << "  failed to remove " << label << ": " << p.string() << ": "
                << ec.message() << "\n";
        }
    };
    remove_if_exists(bash_completion, "bash completion");
    remove_if_exists(zsh_completion, "zsh completion");
    if (!zshrc.empty() && remove_zshrc_block(zshrc)) {
        out << "  removed zsh setup block from " << zshrc.string() << "\n";
    }
    if (purge) {
        for (const auto& p : spp_paths) {
            remove_if_exists(p, "template");
        }
    }
    // Remove the binary last so any earlier-step failures still leave the
    // user with a working binary they can re-invoke after fixing.
    remove_if_exists(binary, "binary");

    out << "done\n";
    return 0;
}

int cmd_completion(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc >= 3 && is_help_flag(argv[2])) {
        print_help_completion(out);
        return 0;
    }
    if (argc != 3) {
        print_help_completion(err);
        return 1;
    }
    std::string shell{argv[2]};
    if (shell == "bash") {
        out << bash_completion_script();
        return 0;
    }
    if (shell == "zsh") {
        out << zsh_completion_script();
        return 0;
    }
    err << "unknown shell '" << shell << "'; supported: bash, zsh\n";
    return 1;
}

int cmd_update(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    bool skip_confirm = false;
    bool force = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (is_help_flag(arg)) {
            print_help_update(out);
            return 0;
        }
        if (arg == "--yes" || arg == "-y") {
            skip_confirm = true;
            ++positional_start;
        } else if (arg == "--force") {
            force = true;
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

    if (!force) {
        std::string latest = resolve_latest_version();
        if (latest.empty()) {
            err << "warning: could not resolve latest version, continuing anyway\n";
        } else if (latest == SPUDPLATE_VERSION_STRING) {
            out << "already up to date (v" << SPUDPLATE_VERSION_STRING << ")\n";
            return 0;
        } else {
            out << "latest version:  v" << latest << "\n";
        }
    }

    if (!skip_confirm) {
        if (!confirm_yes_no(out,
                            "this will download and install the latest spudplate "
                            "from github. continue? [y/N] ")) {
            out << "aborted\n";
            return 0;
        }
    }

    // The default command is the same one the README documents. Tests and
    // anyone with their own mirror can override it via the environment.
    const char* override_cmd = std::getenv("SPUDPLATE_UPDATE_COMMAND");
    std::string cmd = override_cmd != nullptr && *override_cmd != '\0'
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
    if (argc >= 3 && is_help_flag(argv[2])) {
        print_help_validate(out);
        return 0;
    }
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
        err << source_path.string() << ": cannot open: " << std::strerror(errno) << "\n";
        return 5;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    int exit_code = 0;
    if (!validate_template_source(source, source_path.string(), err, exit_code)) {
        return exit_code;
    }
    out << "ok\n";
    return 0;
}

int cmd_list(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc >= 3 && is_help_flag(argv[2])) {
        print_help_list(out);
        return 0;
    }
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
        if (!entry.is_directory())
            continue;
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
        err << "warning: legacy install '" << n << "' is shadowed by '" << n << ".spp'\n";
    }
    for (const auto& n : only_legacy) {
        err << "warning: legacy install '" << n << "'; reinstall to upgrade\n";
    }
    return 0;
}

int cmd_inspect(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc >= 3 && is_help_flag(argv[2])) {
        print_help_inspect(out);
        return 0;
    }
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
        out.write(pack.source.data(), static_cast<std::streamsize>(pack.source.size()));
    } catch (const SpudpackError& e) {
        err << name << ": " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmd_uninstall(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc >= 3 && is_help_flag(argv[2])) {
        print_help_uninstall(out);
        return 0;
    }
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
            err << name << ": cannot remove legacy: " << rm_ec.message() << "\n";
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
    bool timeouts_disabled = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (is_help_flag(arg)) {
            print_help_run(out);
            return 0;
        }
        if (arg == "--dry-run") {
            dry_run_mode = true;
            ++positional_start;
        } else if (arg == "--yes" || arg == "-y") {
            skip_authorization = true;
            ++positional_start;
        } else if (arg == "--no-timeout") {
            timeouts_disabled = true;
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

        if (!std::filesystem::exists(file_path) && legacy_install_exists(home, raw_arg)) {
            err << "legacy install '" << raw_arg << "'; reinstall to upgrade\n";
            return 1;
        }
        if (!std::filesystem::exists(file_path)) {
            err << "'" << raw_arg << "' is not installed";
            std::string suggestion =
                suggest_template_name(raw_arg, list_installed_names(home));
            if (!suggestion.empty()) {
                err << ", did you mean '" << suggestion << "'?\n";
            } else {
                err << "; run 'spudplate list' to see available templates\n";
            }
            return 5;
        }
        if (!std::filesystem::is_regular_file(file_path)) {
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
            print_error(err, file_path.string(), "parse error", e.line(), e.column(),
                        e.what());
            return 2;
        }
    } else {
        if (!std::filesystem::exists(file_path)) {
            err << file_path.string() << ": cannot open: " << std::strerror(ENOENT)
                << "\n";
            return 5;
        }
        try {
            pack = spudpack_read_file(file_path);
            program = deserialize_program(pack.program_bytes.data(),
                                          pack.program_bytes.size(), pack.version);
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
        print_error(err, file_path.string(), "semantic error", e.line(), e.column(),
                    e.what());
        return 3;
    }

    std::optional<AssetMapSourceProvider> provider;
    const SourceProvider* source_ptr = nullptr;
    const std::vector<spudplate::SpudpackDep>* deps_ptr = nullptr;
    if (have_pack) {
        provider.emplace(pack.assets);
        source_ptr = &*provider;
        deps_ptr = &pack.deps;
    }

    try {
        if (dry_run_mode) {
            dry_run(program, prompter, out, /*ascii_only=*/!locale_is_utf8(),
                    source_ptr, deps_ptr);
        } else {
            run(program, prompter, skip_authorization, source_ptr,
                timeouts_disabled, deps_ptr);
        }
    } catch (const RuntimeError& e) {
        print_error(err, file_path.string(), "runtime error", e.line(), e.column(),
                    e.what());
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
    if (subcommand == "--help" || subcommand == "-h" || subcommand == "help") {
        print_usage(out);
        return 0;
    }
    if (subcommand == "version" || subcommand == "--version") {
        if (argc >= 3 && is_help_flag(argv[2])) {
            print_help_version(out);
            return 0;
        }
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
    if (subcommand == "completion") {
        return cmd_completion(argc, argv, out, err);
    }
    if (subcommand == "self-uninstall") {
        return cmd_self_uninstall(argc, argv, out, err);
    }
    print_usage(err);
    return 1;
}

}  // namespace spudplate
