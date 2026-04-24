#include "spudplate/validator.h"

#include <variant>

namespace spudplate {

namespace {

// Forward declaration so the visitor below can recurse into nested repeat bodies.
void validate_stmt(const Stmt& stmt, bool inside_repeat);

struct StmtVisitor {
    bool inside_repeat;

    void operator()(const AskStmt& ask) const {
        if (inside_repeat) {
            throw SemanticError("'ask' not allowed inside 'repeat'", ask.line,
                                ask.column);
        }
    }

    void operator()(const LetStmt&) const {}
    void operator()(const MkdirStmt&) const {}
    void operator()(const FileStmt&) const {}
    void operator()(const CopyStmt&) const {}

    void operator()(const RepeatStmt& rep) const {
        for (const auto& inner : rep.body) {
            validate_stmt(*inner, true);
        }
    }
};

void validate_stmt(const Stmt& stmt, bool inside_repeat) {
    std::visit(StmtVisitor{inside_repeat}, stmt.data);
}

}  // namespace

void validate(const Program& program) {
    for (const auto& stmt : program.statements) {
        validate_stmt(*stmt, false);
    }
}

}  // namespace spudplate
