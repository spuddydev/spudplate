#include "spudplate/interpreter.h"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "spudplate/ast.h"

namespace spudplate {

void Environment::declare(const std::string& name, Value value) {
    if (lookup(name).has_value()) {
        throw std::logic_error("environment already has a binding for '" + name +
                               "'");
    }
    frames_.back().emplace(name, std::move(value));
}

std::optional<Value> Environment::lookup(const std::string& name) const {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

namespace {

// Until Part 4 lands, the Prompter forward-declared in the header has no
// concrete implementation. The skeleton interpreter never reaches a code
// path that calls it (every statement throws "not yet supported" first).
void unsupported(const std::string& stmt_name, int line, int column) {
    throw RuntimeError("statement '" + stmt_name +
                           "' not yet supported in this build",
                       line, column);
}

class Interpreter {
  public:
    void execute(const Stmt& stmt) {
        std::visit(
            [&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, AskStmt>) {
                    unsupported("ask", s.line, s.column);
                } else if constexpr (std::is_same_v<T, LetStmt>) {
                    unsupported("let", s.line, s.column);
                } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                    unsupported("mkdir", s.line, s.column);
                } else if constexpr (std::is_same_v<T, FileStmt>) {
                    unsupported("file", s.line, s.column);
                } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                    unsupported("repeat", s.line, s.column);
                } else if constexpr (std::is_same_v<T, CopyStmt>) {
                    unsupported("copy", s.line, s.column);
                } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                    unsupported("include", s.line, s.column);
                }
            },
            stmt.data);
    }

    [[nodiscard]] Environment& env() { return env_; }

  private:
    Environment env_;
};

void run_program(const Program& program, Prompter& /*prompter*/,
                 Interpreter& interp) {
    for (const auto& stmt : program.statements) {
        interp.execute(*stmt);
    }
}

}  // namespace

void run(const Program& program, Prompter& prompter) {
    Interpreter interp;
    run_program(program, prompter, interp);
}

Environment run_for_tests(const Program& program, Prompter& prompter) {
    Interpreter interp;
    run_program(program, prompter, interp);
    return std::move(interp.env());
}

}  // namespace spudplate
