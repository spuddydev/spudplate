#include "spudplate/binary_serializer.h"

#include <cstring>
#include <type_traits>
#include <variant>

namespace spudplate {

BinaryDeserializeError::BinaryDeserializeError(std::string message,
                                               std::size_t offset)
    : std::runtime_error(std::move(message)), offset_(offset) {}

namespace {

// Maximum bytes a LEB128-encoded uint64 can occupy. Anything longer
// indicates either a malicious payload or a bug in the encoder.
constexpr std::size_t kVarintMaxBytes = 10;

// Static asserts pin the count of each variant - adding a new arm fails
// the build until the encode/decode tables grow with it. Reordering
// existing arms is not caught by arity alone; the round-trip tests are
// the second line of defence there.
static_assert(std::variant_size_v<ExprData> == 8);
static_assert(std::variant_size_v<StmtData> == 10);
static_assert(std::variant_size_v<PathSegment> == 3);
static_assert(std::variant_size_v<FileSource> == 2);

// ----- Writer -------------------------------------------------------------

class Writer {
  public:
    void write_u8(std::uint8_t v) { buf_.push_back(v); }

    void write_varint(std::uint64_t v) {
        while (v >= 0x80U) {
            buf_.push_back(static_cast<std::uint8_t>(v | 0x80U));
            v >>= 7;
        }
        buf_.push_back(static_cast<std::uint8_t>(v));
    }

    void write_zigzag(std::int64_t v) {
        // Standard zigzag: maps -1,1,-2,2 → 1,2,3,4 so small magnitudes
        // share the cheap end of the varint range.
        const std::uint64_t z = (static_cast<std::uint64_t>(v) << 1) ^
                                static_cast<std::uint64_t>(v >> 63);
        write_varint(z);
    }

    void write_bool(bool v) { write_u8(v ? 1 : 0); }

    void write_string(const std::string& s) {
        write_varint(s.size());
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    std::vector<std::uint8_t> take() && { return std::move(buf_); }

  private:
    std::vector<std::uint8_t> buf_;
};

// ----- Reader -------------------------------------------------------------

class Reader {
  public:
    Reader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::size_t pos() const noexcept { return pos_; }
    std::size_t remaining() const noexcept { return size_ - pos_; }

    std::uint8_t read_u8() {
        require(1, "unexpected end of input");
        return data_[pos_++];
    }

    std::uint64_t read_varint() {
        std::uint64_t result = 0;
        std::size_t bytes = 0;
        const std::size_t start = pos_;
        while (true) {
            if (bytes == kVarintMaxBytes) {
                throw BinaryDeserializeError("varint exceeds 10-byte cap",
                                             start);
            }
            require(1, "truncated varint");
            const std::uint8_t b = data_[pos_++];
            result |= static_cast<std::uint64_t>(b & 0x7FU) << (7 * bytes);
            ++bytes;
            if ((b & 0x80U) == 0) break;
        }
        return result;
    }

    std::int64_t read_zigzag() {
        const std::uint64_t z = read_varint();
        return static_cast<std::int64_t>((z >> 1) ^ (~(z & 1U) + 1U));
    }

    bool read_bool() {
        const std::uint8_t v = read_u8();
        if (v > 1) {
            throw BinaryDeserializeError("invalid bool tag", pos_ - 1);
        }
        return v != 0;
    }

    std::string read_string() {
        const std::size_t start = pos_;
        const std::uint64_t len = read_varint();
        require_for_payload(len, start, "string length exceeds remaining input");
        std::string s(reinterpret_cast<const char*>(data_ + pos_),
                      static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return s;
    }

    // Read a varint that names a vector element count and bound it against
    // the bytes left in the input. Each AST element encodes to at least one
    // byte (a tag), so the count cannot legitimately exceed `remaining()`.
    // This stops a hand-crafted `n = 2^60` from triggering a multi-gigabyte
    // `vector::reserve` before the truncation error fires.
    std::size_t read_count() {
        const std::size_t start = pos_;
        const std::uint64_t n = read_varint();
        if (n > remaining()) {
            throw BinaryDeserializeError(
                "element count exceeds remaining input", start);
        }
        return static_cast<std::size_t>(n);
    }

    // Recursion-depth tracker for expression / statement decoding. Real
    // programs nest a handful of levels at most (a few binary operators
    // inside a `when` clause, a couple of nested `repeat` blocks). A
    // pathological `.spp` could chain thousands of `repeat` or binary-op
    // nodes and blow the call stack; the cap keeps the worst case bounded.
    class DepthGuard {
      public:
        explicit DepthGuard(Reader& r) : r_(r) {
            constexpr std::size_t kMaxDepth = 256;
            if (++r_.depth_ > kMaxDepth) {
                throw BinaryDeserializeError(
                    "recursion depth exceeds 256", r_.pos_);
            }
        }
        ~DepthGuard() { --r_.depth_; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;

      private:
        Reader& r_;
    };

  private:
    void require(std::size_t n, const char* msg) {
        if (pos_ + n > size_) {
            throw BinaryDeserializeError(msg, pos_);
        }
    }

    void require_for_payload(std::uint64_t len, std::size_t at,
                             const char* msg) {
        // Sum-overflow guard: detect SIZE_MAX-style hostile lengths
        // before we use them as an offset.
        if (len > size_ || pos_ + static_cast<std::size_t>(len) < pos_ ||
            pos_ + static_cast<std::size_t>(len) > size_) {
            throw BinaryDeserializeError(msg, at);
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
};

// ----- Token / VarType wire encoding --------------------------------------

bool is_legal_unary_op(TokenType t) { return t == TokenType::NOT; }

bool is_legal_binary_op(TokenType t) {
    switch (t) {
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::EQUALS:
        case TokenType::NOT_EQUALS:
        case TokenType::LESS:
        case TokenType::GREATER:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER_EQUAL:
        case TokenType::AND:
        case TokenType::OR:
            return true;
        default:
            return false;
    }
}

void write_var_type(Writer& w, VarType t) {
    w.write_u8(static_cast<std::uint8_t>(t));
}

VarType read_var_type(Reader& r) {
    const std::size_t at = r.pos();
    const std::uint8_t v = r.read_u8();
    switch (v) {
        case static_cast<std::uint8_t>(VarType::String):
            return VarType::String;
        case static_cast<std::uint8_t>(VarType::Bool):
            return VarType::Bool;
        case static_cast<std::uint8_t>(VarType::Int):
            return VarType::Int;
        default:
            throw BinaryDeserializeError("invalid VarType tag", at);
    }
}

// ----- Forward decls of the recursive encoders/decoders -------------------

void encode_expr(Writer& w, const Expr& e);
void encode_path_expr(Writer& w, const PathExpr& p);
void encode_path_segment(Writer& w, const PathSegment& s);
void encode_file_source(Writer& w, const FileSource& fs);
void encode_stmt(Writer& w, const Stmt& s);

ExprPtr decode_expr(Reader& r);
PathExpr decode_path_expr(Reader& r);
PathSegment decode_path_segment(Reader& r);
FileSource decode_file_source(Reader& r);
StmtPtr decode_stmt(Reader& r);

// ----- Optional / vector helpers ------------------------------------------

void encode_opt_expr(Writer& w, const std::optional<ExprPtr>& opt) {
    if (!opt.has_value()) {
        w.write_bool(false);
        return;
    }
    w.write_bool(true);
    encode_expr(w, **opt);
}

std::optional<ExprPtr> decode_opt_expr(Reader& r) {
    if (!r.read_bool()) return std::nullopt;
    return decode_expr(r);
}

void encode_opt_path_expr(Writer& w, const std::optional<PathExpr>& opt) {
    if (!opt.has_value()) {
        w.write_bool(false);
        return;
    }
    w.write_bool(true);
    encode_path_expr(w, *opt);
}

std::optional<PathExpr> decode_opt_path_expr(Reader& r) {
    if (!r.read_bool()) return std::nullopt;
    return decode_path_expr(r);
}

void encode_opt_string(Writer& w, const std::optional<std::string>& opt) {
    if (!opt.has_value()) {
        w.write_bool(false);
        return;
    }
    w.write_bool(true);
    w.write_string(*opt);
}

std::optional<std::string> decode_opt_string(Reader& r) {
    if (!r.read_bool()) return std::nullopt;
    return r.read_string();
}

void encode_opt_int(Writer& w, const std::optional<int>& opt) {
    if (!opt.has_value()) {
        w.write_bool(false);
        return;
    }
    w.write_bool(true);
    w.write_zigzag(*opt);
}

std::optional<int> decode_opt_int(Reader& r) {
    if (!r.read_bool()) return std::nullopt;
    const std::int64_t v = r.read_zigzag();
    return static_cast<int>(v);
}

void encode_expr_vector(Writer& w, const std::vector<ExprPtr>& v) {
    w.write_varint(v.size());
    for (const auto& e : v) {
        encode_expr(w, *e);
    }
}

std::vector<ExprPtr> decode_expr_vector(Reader& r) {
    const std::size_t n = r.read_count();
    std::vector<ExprPtr> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        v.push_back(decode_expr(r));
    }
    return v;
}

// ----- Path expressions ---------------------------------------------------

void encode_path_segment(Writer& w, const PathSegment& s) {
    w.write_u8(static_cast<std::uint8_t>(s.index()));
    std::visit(
        [&](const auto& seg) {
            using T = std::decay_t<decltype(seg)>;
            if constexpr (std::is_same_v<T, PathLiteral>) {
                w.write_string(seg.value);
            } else if constexpr (std::is_same_v<T, PathVar>) {
                w.write_string(seg.name);
            } else {
                static_assert(std::is_same_v<T, PathInterp>);
                encode_expr(w, *seg.expression);
            }
            w.write_zigzag(seg.line);
            w.write_zigzag(seg.column);
        },
        s);
}

PathSegment decode_path_segment(Reader& r) {
    const std::size_t at = r.pos();
    const std::uint8_t tag = r.read_u8();
    switch (static_cast<PathSegTag>(tag)) {
        case PathSegTag::Literal: {
            std::string value = r.read_string();
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return PathLiteral{
                .value = std::move(value), .line = line, .column = column};
        }
        case PathSegTag::Var: {
            std::string name = r.read_string();
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return PathVar{
                .name = std::move(name), .line = line, .column = column};
        }
        case PathSegTag::Interp: {
            ExprPtr expr = decode_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return PathInterp{
                .expression = std::move(expr), .line = line, .column = column};
        }
    }
    throw BinaryDeserializeError("invalid PathSegment tag", at);
}

void encode_path_expr(Writer& w, const PathExpr& p) {
    w.write_varint(p.segments.size());
    for (const auto& s : p.segments) {
        encode_path_segment(w, s);
    }
    w.write_zigzag(p.line);
    w.write_zigzag(p.column);
}

PathExpr decode_path_expr(Reader& r) {
    PathExpr p;
    const std::size_t n = r.read_count();
    p.segments.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        p.segments.push_back(decode_path_segment(r));
    }
    p.line = static_cast<int>(r.read_zigzag());
    p.column = static_cast<int>(r.read_zigzag());
    return p;
}

// ----- File sources -------------------------------------------------------

void encode_file_source(Writer& w, const FileSource& fs) {
    w.write_u8(static_cast<std::uint8_t>(fs.index()));
    std::visit(
        [&](const auto& src) {
            using T = std::decay_t<decltype(src)>;
            if constexpr (std::is_same_v<T, FileFromSource>) {
                encode_path_expr(w, src.path);
                w.write_bool(src.verbatim);
            } else {
                static_assert(std::is_same_v<T, FileContentSource>);
                encode_expr(w, *src.value);
            }
        },
        fs);
}

FileSource decode_file_source(Reader& r) {
    const std::size_t at = r.pos();
    const std::uint8_t tag = r.read_u8();
    switch (static_cast<FileSrcTag>(tag)) {
        case FileSrcTag::From: {
            PathExpr path = decode_path_expr(r);
            const bool verbatim = r.read_bool();
            return FileFromSource{.path = std::move(path), .verbatim = verbatim};
        }
        case FileSrcTag::Content: {
            return FileContentSource{.value = decode_expr(r)};
        }
    }
    throw BinaryDeserializeError("invalid FileSource tag", at);
}

// ----- Expressions --------------------------------------------------------

void encode_expr(Writer& w, const Expr& e) {
    w.write_u8(static_cast<std::uint8_t>(e.data.index()));
    std::visit(
        [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, StringLiteralExpr>) {
                w.write_string(node.value);
            } else if constexpr (std::is_same_v<T, IntegerLiteralExpr>) {
                w.write_zigzag(node.value);
            } else if constexpr (std::is_same_v<T, BoolLiteralExpr>) {
                w.write_bool(node.value);
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                w.write_string(node.name);
            } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                if (!is_legal_unary_op(node.op)) {
                    throw BinarySerializeError(
                        "invalid op tag for UnaryExpr");
                }
                w.write_u8(static_cast<std::uint8_t>(node.op));
                encode_expr(w, *node.operand);
            } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                if (!is_legal_binary_op(node.op)) {
                    throw BinarySerializeError(
                        "invalid op tag for BinaryExpr");
                }
                w.write_u8(static_cast<std::uint8_t>(node.op));
                encode_expr(w, *node.left);
                encode_expr(w, *node.right);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                w.write_string(node.name);
                encode_expr_vector(w, node.arguments);
            } else {
                static_assert(std::is_same_v<T, TemplateStringExpr>);
                w.write_varint(node.parts.size());
                for (const auto& part : node.parts) {
                    if (std::holds_alternative<std::string>(part)) {
                        w.write_u8(static_cast<std::uint8_t>(
                            TemplatePartTag::Literal));
                        w.write_string(std::get<std::string>(part));
                    } else {
                        w.write_u8(static_cast<std::uint8_t>(
                            TemplatePartTag::Expression));
                        encode_expr(w, *std::get<ExprPtr>(part));
                    }
                }
            }
            w.write_zigzag(node.line);
            w.write_zigzag(node.column);
        },
        e.data);
}

ExprPtr decode_expr(Reader& r) {
    Reader::DepthGuard guard(r);
    const std::size_t at = r.pos();
    const std::uint8_t tag = r.read_u8();
    auto wrap = [](auto&& data) {
        auto e = std::make_unique<Expr>();
        e->data = std::move(data);
        return e;
    };
    switch (static_cast<ExprTag>(tag)) {
        case ExprTag::StringLiteral: {
            std::string value = r.read_string();
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(StringLiteralExpr{
                .value = std::move(value), .line = line, .column = column});
        }
        case ExprTag::IntegerLiteral: {
            const std::int64_t value = r.read_zigzag();
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(IntegerLiteralExpr{.value = static_cast<int>(value),
                                           .line = line,
                                           .column = column});
        }
        case ExprTag::BoolLiteral: {
            const bool value = r.read_bool();
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(BoolLiteralExpr{
                .value = value, .line = line, .column = column});
        }
        case ExprTag::Identifier: {
            std::string name = r.read_string();
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(IdentifierExpr{
                .name = std::move(name), .line = line, .column = column});
        }
        case ExprTag::Unary: {
            const std::size_t op_at = r.pos();
            const auto op = static_cast<TokenType>(r.read_u8());
            if (!is_legal_unary_op(op)) {
                throw BinaryDeserializeError("invalid op tag for UnaryExpr",
                                             op_at);
            }
            ExprPtr operand = decode_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(UnaryExpr{.op = op,
                                  .operand = std::move(operand),
                                  .line = line,
                                  .column = column});
        }
        case ExprTag::Binary: {
            const std::size_t op_at = r.pos();
            const auto op = static_cast<TokenType>(r.read_u8());
            if (!is_legal_binary_op(op)) {
                throw BinaryDeserializeError("invalid op tag for BinaryExpr",
                                             op_at);
            }
            ExprPtr left = decode_expr(r);
            ExprPtr right = decode_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(BinaryExpr{.op = op,
                                   .left = std::move(left),
                                   .right = std::move(right),
                                   .line = line,
                                   .column = column});
        }
        case ExprTag::FunctionCall: {
            std::string name = r.read_string();
            std::vector<ExprPtr> args = decode_expr_vector(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(FunctionCallExpr{.name = std::move(name),
                                         .arguments = std::move(args),
                                         .line = line,
                                         .column = column});
        }
        case ExprTag::TemplateString: {
            const std::size_t n = r.read_count();
            std::vector<std::variant<std::string, ExprPtr>> parts;
            parts.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t sub_at = r.pos();
                const std::uint8_t sub = r.read_u8();
                switch (static_cast<TemplatePartTag>(sub)) {
                    case TemplatePartTag::Literal:
                        parts.emplace_back(r.read_string());
                        break;
                    case TemplatePartTag::Expression:
                        parts.emplace_back(decode_expr(r));
                        break;
                    default:
                        throw BinaryDeserializeError(
                            "invalid TemplateString sub-tag", sub_at);
                }
            }
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(TemplateStringExpr{.parts = std::move(parts),
                                           .line = line,
                                           .column = column});
        }
    }
    throw BinaryDeserializeError("invalid Expr tag", at);
}

// ----- Statements ---------------------------------------------------------

void encode_stmt(Writer& w, const Stmt& s) {
    w.write_u8(static_cast<std::uint8_t>(s.data.index()));
    std::visit(
        [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, AskStmt>) {
                w.write_string(node.name);
                w.write_string(node.prompt);
                write_var_type(w, node.var_type);
                encode_opt_expr(w, node.default_value);
                encode_expr_vector(w, node.options);
                encode_opt_expr(w, node.when_clause);
            } else if constexpr (std::is_same_v<T, LetStmt>) {
                w.write_string(node.name);
                encode_expr(w, *node.value);
            } else if constexpr (std::is_same_v<T, AssignStmt>) {
                w.write_string(node.name);
                encode_expr(w, *node.value);
            } else if constexpr (std::is_same_v<T, MkdirStmt>) {
                encode_path_expr(w, node.path);
                encode_opt_string(w, node.alias);
                w.write_bool(node.mkdir_p);
                encode_opt_path_expr(w, node.from_source);
                w.write_bool(node.verbatim);
                encode_opt_int(w, node.mode);
                encode_opt_expr(w, node.when_clause);
            } else if constexpr (std::is_same_v<T, FileStmt>) {
                encode_path_expr(w, node.path);
                encode_opt_string(w, node.alias);
                encode_file_source(w, node.source);
                w.write_bool(node.append);
                encode_opt_int(w, node.mode);
                encode_opt_expr(w, node.when_clause);
            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
                w.write_string(node.collection_var);
                w.write_string(node.iterator_var);
                w.write_varint(node.body.size());
                for (const auto& sp : node.body) {
                    encode_stmt(w, *sp);
                }
                encode_opt_expr(w, node.when_clause);
            } else if constexpr (std::is_same_v<T, CopyStmt>) {
                encode_path_expr(w, node.source);
                encode_path_expr(w, node.destination);
                w.write_bool(node.verbatim);
                encode_opt_expr(w, node.when_clause);
            } else if constexpr (std::is_same_v<T, IncludeStmt>) {
                w.write_string(node.name);
                encode_opt_expr(w, node.when_clause);
            } else if constexpr (std::is_same_v<T, RunStmt>) {
                encode_expr(w, *node.command);
                encode_opt_path_expr(w, node.cwd);
                encode_opt_expr(w, node.when_clause);
            } else {
                static_assert(std::is_same_v<T, IfStmt>);
                encode_expr(w, *node.condition);
                w.write_varint(node.body.size());
                for (const auto& sp : node.body) {
                    encode_stmt(w, *sp);
                }
            }
            w.write_zigzag(node.line);
            w.write_zigzag(node.column);
        },
        s.data);
}

StmtPtr decode_stmt(Reader& r) {
    Reader::DepthGuard guard(r);
    const std::size_t at = r.pos();
    const std::uint8_t tag = r.read_u8();
    auto wrap = [](auto&& data) {
        auto s = std::make_unique<Stmt>();
        s->data = std::move(data);
        return s;
    };
    switch (static_cast<StmtTag>(tag)) {
        case StmtTag::Ask: {
            std::string name = r.read_string();
            std::string prompt = r.read_string();
            VarType var_type = read_var_type(r);
            auto default_value = decode_opt_expr(r);
            auto options = decode_expr_vector(r);
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(AskStmt{.name = std::move(name),
                                .prompt = std::move(prompt),
                                .var_type = var_type,
                                .default_value = std::move(default_value),
                                .options = std::move(options),
                                .when_clause = std::move(when_clause),
                                .line = line,
                                .column = column});
        }
        case StmtTag::Let: {
            std::string name = r.read_string();
            ExprPtr value = decode_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(LetStmt{.name = std::move(name),
                                .value = std::move(value),
                                .line = line,
                                .column = column});
        }
        case StmtTag::Assign: {
            std::string name = r.read_string();
            ExprPtr value = decode_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(AssignStmt{.name = std::move(name),
                                   .value = std::move(value),
                                   .line = line,
                                   .column = column});
        }
        case StmtTag::Mkdir: {
            PathExpr path = decode_path_expr(r);
            auto alias = decode_opt_string(r);
            const bool mkdir_p = r.read_bool();
            auto from_source = decode_opt_path_expr(r);
            const bool verbatim = r.read_bool();
            auto mode = decode_opt_int(r);
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(MkdirStmt{.path = std::move(path),
                                  .alias = std::move(alias),
                                  .mkdir_p = mkdir_p,
                                  .from_source = std::move(from_source),
                                  .verbatim = verbatim,
                                  .mode = mode,
                                  .when_clause = std::move(when_clause),
                                  .line = line,
                                  .column = column});
        }
        case StmtTag::File: {
            PathExpr path = decode_path_expr(r);
            auto alias = decode_opt_string(r);
            FileSource source = decode_file_source(r);
            const bool append = r.read_bool();
            auto mode = decode_opt_int(r);
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(FileStmt{.path = std::move(path),
                                 .alias = std::move(alias),
                                 .source = std::move(source),
                                 .append = append,
                                 .mode = mode,
                                 .when_clause = std::move(when_clause),
                                 .line = line,
                                 .column = column});
        }
        case StmtTag::Repeat: {
            std::string collection_var = r.read_string();
            std::string iterator_var = r.read_string();
            const std::size_t n = r.read_count();
            std::vector<StmtPtr> body;
            body.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                body.push_back(decode_stmt(r));
            }
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(RepeatStmt{.collection_var = std::move(collection_var),
                                   .iterator_var = std::move(iterator_var),
                                   .body = std::move(body),
                                   .when_clause = std::move(when_clause),
                                   .line = line,
                                   .column = column});
        }
        case StmtTag::Copy: {
            PathExpr source = decode_path_expr(r);
            PathExpr destination = decode_path_expr(r);
            const bool verbatim = r.read_bool();
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(CopyStmt{.source = std::move(source),
                                 .destination = std::move(destination),
                                 .verbatim = verbatim,
                                 .when_clause = std::move(when_clause),
                                 .line = line,
                                 .column = column});
        }
        case StmtTag::Include: {
            std::string name = r.read_string();
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(IncludeStmt{.name = std::move(name),
                                    .when_clause = std::move(when_clause),
                                    .line = line,
                                    .column = column});
        }
        case StmtTag::Run: {
            ExprPtr command = decode_expr(r);
            auto cwd = decode_opt_path_expr(r);
            auto when_clause = decode_opt_expr(r);
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(RunStmt{.command = std::move(command),
                                .cwd = std::move(cwd),
                                .when_clause = std::move(when_clause),
                                .line = line,
                                .column = column});
        }
        case StmtTag::If: {
            ExprPtr condition = decode_expr(r);
            const std::size_t body_size = r.read_varint();
            std::vector<StmtPtr> body;
            body.reserve(body_size);
            for (std::size_t i = 0; i < body_size; ++i) {
                body.push_back(decode_stmt(r));
            }
            const int line = static_cast<int>(r.read_zigzag());
            const int column = static_cast<int>(r.read_zigzag());
            return wrap(IfStmt{.condition = std::move(condition),
                               .body = std::move(body),
                               .line = line,
                               .column = column});
        }
    }
    throw BinaryDeserializeError("invalid Stmt tag", at);
}

}  // namespace

std::vector<std::uint8_t> serialize_program(const Program& program) {
    Writer w;
    w.write_varint(program.statements.size());
    for (const auto& sp : program.statements) {
        encode_stmt(w, *sp);
    }
    return std::move(w).take();
}

Program deserialize_program(const std::uint8_t* data, std::size_t size) {
    Reader r(data, size);
    Program p;
    const std::size_t n = r.read_count();
    p.statements.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        p.statements.push_back(decode_stmt(r));
    }
    return p;
}

}  // namespace spudplate
