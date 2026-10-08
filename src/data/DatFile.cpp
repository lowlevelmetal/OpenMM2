#include "data/DatFile.h"

#include "data/CNumbers.h"

#include <algorithm>
#include <format>

namespace mm2::data {
namespace {

// datMultiTokenizer::GetReadTokenizer reads exactly this many header bytes.
constexpr std::string_view kAsciiHeader = "type: a";
constexpr std::string_view kBinaryHeader = "type: b";

// datParser::Read and datParser::Load read names into 64-byte buffers,
// datAsciiTokenizer::GetInt/GetFloat into 32-byte ones.
constexpr std::size_t kNameBuffer = 64;
constexpr std::size_t kNumberBuffer = 32;

// MM2 does not tell quoted tokens apart from others (a quoted "}" closes a
// block); `quoted` only tells an empty quoted token from the end of the file.
struct Token {
    std::string text;
    bool quoted = false;
    int line = 0;

    bool is(std::string_view s) const { return text == s; }
};

// A port of datBaseTokenizer / datAsciiTokenizer over the text after the
// header. The tokenizer keeps one character of look-ahead: a token ends on
// the separator that follows it, which stays as the current character, and
// SkipToEndOfLine reads on from the text behind that character.
class Tokenizer {
public:
    Tokenizer(std::string_view text, std::size_t pos) : m_text(text), m_pos(pos) {}

    bool atEnd() const { return m_cur == -1; }

    // datBaseTokenizer::GetToken; `capacity` is the caller's buffer size.
    Token getToken(std::size_t capacity) {
        Token t;
        while (true) {
            if (m_cur == ' ' || m_cur == '\t' || m_cur == '\r' || m_cur == 0) {
                advance();
            } else if (m_cur == '\n') {
                ++m_line;
                advance();
            } else {
                break;
            }
        }
        t.line = m_line;
        auto store = [&](int c) {
            if (t.text.size() + 1 < capacity)
                t.text.push_back(static_cast<char>(c));
        };
        if (m_cur == '"') {
            // A quoted token runs to the next quote, across lines.
            t.quoted = true;
            advance();
            while (m_cur != -1 && m_cur != '"') {
                store(m_cur);
                advance();
            }
            advance();
            return t;
        }
        while (true) {
            if (m_cur == -1 || isSeparator(m_cur))
                return t;
            if (m_cur == kComment) {
                // A comment before the token is skipped. One inside a token
                // ends it, but the line break that ends the comment is
                // stored in the token first.
                do {
                    while (m_cur != -1 && m_cur != '\n' && m_cur != '\r')
                        advance();
                    if (!t.text.empty())
                        break;
                    while (isSeparator(m_cur))
                        advance();
                } while (m_cur == kComment);
            }
            store(m_cur);
            advance();
        }
    }

    // datBaseTokenizer::SkipToEndOfLine: reads past the next line feed.
    void skipToEndOfLine() {
        while (m_pos < m_text.size())
            if (m_text[m_pos++] == '\n')
                return;
    }

    // datAsciiTokenizer::GetFloat: a token that does not start with a digit,
    // '-' or '.' is an error and reads as 0.
    float getFloat() {
        const Token t = getToken(kNumberBuffer);
        const char c = t.text.empty() ? '\0' : t.text[0];
        if (!((c >= '0' && c <= '9') || c == '-' || c == '.'))
            return 0.0f;
        return static_cast<float>(cAtof(t.text));
    }

    // datAsciiTokenizer::GetInt: a token that does not start with a digit or
    // '-' is an error and reads as 0.
    int getInt() {
        const Token t = getToken(kNumberBuffer);
        const char c = t.text.empty() ? '\0' : t.text[0];
        if (!((c >= '0' && c <= '9') || c == '-'))
            return 0;
        return cAtoi(t.text);
    }

    int line() const { return m_line; }

private:
    // datBaseTokenizer::CommentChar.
    static constexpr int kComment = ';';

    static bool isSeparator(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0; }

    void advance() {
        m_cur = m_pos < m_text.size() ? static_cast<unsigned char>(m_text[m_pos++]) : -1;
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_cur = ' '; // datBaseTokenizer::Init starts with a space
    int m_line = 1;
};

bool isNumberToken(const Token& t) {
    if (t.text.empty())
        return false;
    const char c = t.text[0];
    return (c >= '0' && c <= '9') || c == '-' || c == '.';
}

bool fail(std::string* error, int line, std::string msg) {
    if (error)
        *error = std::format("line {}: {}", line, msg);
    return false;
}

// Without a schema: every field and block the file has, for readers that
// look fields up by name. A field's values are the number tokens that follow
// it; other tokens on its own line are kept as strings (labels such as
// "Aero asAero :075abc8c {", the rest of a multi-word name). A field
// followed by '{' is a block.
class TreeParser {
public:
    TreeParser(std::string_view text, std::size_t start) : m_tok(text, start) { advance(); }

    bool parseFile(DatFile& out, std::string* error) {
        out.root.isBlock = true;
        // datParser::Load: the first token is the class name, whatever it is.
        if (m_cur.text.empty() && m_tok.atEnd())
            return true;
        DatNode top;
        top.name = m_cur.text;
        top.isBlock = true;
        advance();
        if (!parseBody(top, error))
            return false;
        out.root.children.push_back(std::move(top));
        // MM2 stops reading at the class block's closing brace.
        return true;
    }

private:
    void advance() { m_cur = m_tok.getToken(kNameBuffer); }
    bool atEnd() const { return m_cur.text.empty() && !m_cur.quoted && m_tok.atEnd(); }

    bool parseBody(DatNode& node, std::string* error) {
        while (true) {
            if (atEnd())
                return fail(error, m_cur.line, std::format("unexpected end of file inside '{}'", node.name));
            if (m_cur.is("}")) {
                advance();
                return true;
            }
            if (m_cur.is("{")) {
                advance();
                continue;
            }
            DatNode child;
            const int nameLine = m_cur.line;
            child.name = m_cur.text;
            advance();
            while (!atEnd() && !isNumberToken(m_cur) && !m_cur.is("{") && !m_cur.is("}") &&
                   m_cur.line == nameLine) {
                child.strings.push_back(m_cur.text);
                advance();
            }
            if (m_cur.is("{")) {
                advance();
                child.isBlock = true;
                if (!parseBody(child, error))
                    return false;
            } else {
                while (isNumberToken(m_cur)) {
                    child.numbers.push_back(cAtof(m_cur.text));
                    child.numberTexts.push_back(m_cur.text);
                    advance();
                }
            }
            node.children.push_back(std::move(child));
        }
    }

    Tokenizer m_tok;
    Token m_cur;
};

// With a schema: datParser::Read itself. Only registered records are read,
// each taking exactly the tokens its type asks for; an unknown name is
// skipped together with the rest of its line, or with the block that follows
// it when the next token is '{'.
class SchemaParser {
public:
    SchemaParser(std::string_view text, std::size_t start) : m_tok(text, start) {}

    bool parseFile(const DatSchema& schema, DatFile& out, std::string* error) {
        out.root.isBlock = true;
        DatNode top;
        top.name = m_tok.getToken(kNameBuffer).text;
        top.isBlock = true;
        if (top.name.empty() && m_tok.atEnd())
            return true;
        if (!read(schema, top, error))
            return false;
        out.root.children.push_back(std::move(top));
        return true;
    }

private:
    bool read(const DatSchema& schema, DatNode& node, std::string* error) {
        while (true) {
            const Token t = m_tok.getToken(kNameBuffer);
            // MM2 keeps reading empty tokens at the end of the file and never
            // returns; report the file as broken instead.
            if (t.text.empty() && !t.quoted && m_tok.atEnd())
                return fail(error, t.line, std::format("unexpected end of file inside '{}'", node.name));
            if (t.text == "}")
                return true;
            if (t.text == "{")
                continue;
            const auto record = std::ranges::find(schema, t.text, &DatRecord::name);
            if (record != schema.end()) {
                DatNode child;
                child.name = record->name;
                if (!readRecord(*record, child, error))
                    return false;
                node.children.push_back(std::move(child));
                continue;
            }
            // "datParser::Read - Unrecognized token": skip the block that
            // follows, or the rest of the line.
            if (m_tok.getToken(kNameBuffer).text == "{") {
                for (int depth = 1; depth > 0;) {
                    const Token s = m_tok.getToken(kNameBuffer);
                    if (s.text.empty() && m_tok.atEnd())
                        return fail(error, s.line, std::format("unexpected end of file inside '{}'", t.text));
                    if (s.text == "{")
                        ++depth;
                    else if (s.text == "}")
                        --depth;
                }
            } else {
                m_tok.skipToEndOfLine();
            }
        }
    }

    void addNumber(DatNode& node, double value) {
        node.numbers.push_back(value);
        node.numberTexts.push_back(std::format("{}", value));
    }

    bool readRecord(const DatRecord& record, DatNode& node, std::string* error) {
        using Type = DatRecord::Type;
        for (int i = 0; i < record.count; ++i) {
            switch (record.type) {
            case Type::String: node.strings.push_back(m_tok.getToken(kNameBuffer).text); break;
            case Type::Bool: addNumber(node, m_tok.getInt() != 0 ? 1.0 : 0.0); break;
            case Type::Byte: addNumber(node, static_cast<signed char>(m_tok.getInt())); break;
            case Type::Short: addNumber(node, static_cast<short>(m_tok.getInt())); break;
            case Type::Int: addNumber(node, m_tok.getInt()); break;
            case Type::Float: addNumber(node, m_tok.getFloat()); break;
            case Type::Vec2:
            case Type::Vec3:
            case Type::Vec4: {
                const int n = record.type == Type::Vec2 ? 2 : (record.type == Type::Vec3 ? 3 : 4);
                for (int c = 0; c < n; ++c)
                    addNumber(node, m_tok.getFloat());
                break;
            }
            case Type::Parser:
                node.isBlock = true;
                if (!read(record.records, node, error))
                    return false;
                break;
            }
        }
        return true;
    }

    Tokenizer m_tok;
};

std::optional<std::size_t> bodyStart(std::string_view text, std::string* error) {
    if (text.starts_with(kBinaryHeader)) {
        // MM2 reads these with datBinTokenizer; no retail file uses one.
        if (error)
            *error = "binary ('type: b') data files are not supported";
        return std::nullopt;
    }
    // GetReadTokenizer consumes the seven header bytes whatever they are; an
    // unknown header is logged and the rest is read as text.
    return std::min(text.size(), kAsciiHeader.size());
}

} // namespace

const DatNode* DatNode::child(std::string_view key) const {
    // datParser::Read assigns every occurrence in turn, so the last one wins.
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        if (it->name == key)
            return &*it;
    return nullptr;
}

std::optional<float> DatNode::getFloat(std::string_view key) const {
    const auto* c = child(key);
    if (!c || c->numbers.empty())
        return std::nullopt;
    return static_cast<float>(c->numbers[0]);
}

std::optional<int> DatNode::getInt(std::string_view key) const {
    const auto* c = child(key);
    if (!c || c->numberTexts.empty())
        return std::nullopt;
    // datAsciiTokenizer::GetInt: atoi of the token ("1.9" is 1, ".5" is 0).
    return cAtoi(c->numberTexts[0]);
}

std::optional<Vec2> DatNode::getVec2(std::string_view key) const {
    const auto* c = child(key);
    if (!c || c->numbers.size() < 2)
        return std::nullopt;
    return Vec2{static_cast<float>(c->numbers[0]), static_cast<float>(c->numbers[1])};
}

std::optional<Vec3> DatNode::getVec3(std::string_view key) const {
    const auto* c = child(key);
    if (!c || c->numbers.size() < 3)
        return std::nullopt;
    return Vec3{static_cast<float>(c->numbers[0]), static_cast<float>(c->numbers[1]),
                static_cast<float>(c->numbers[2])};
}

std::optional<std::string> DatNode::getString(std::string_view key) const {
    const auto* c = child(key);
    if (!c || c->strings.empty())
        return std::nullopt;
    return c->strings[0];
}

std::vector<float> DatNode::getFloats(std::string_view key) const {
    std::vector<float> out;
    if (const auto* c = child(key))
        for (double d : c->numbers)
            out.push_back(static_cast<float>(d));
    return out;
}

bool DatNode::read(std::string_view key, float& out) const {
    if (auto v = getFloat(key)) {
        out = *v;
        return true;
    }
    return false;
}

bool DatNode::read(std::string_view key, int& out) const {
    if (auto v = getInt(key)) {
        out = *v;
        return true;
    }
    return false;
}

bool DatNode::read(std::string_view key, Vec2& out) const {
    if (auto v = getVec2(key)) {
        out = *v;
        return true;
    }
    return false;
}

bool DatNode::read(std::string_view key, Vec3& out) const {
    if (auto v = getVec3(key)) {
        out = *v;
        return true;
    }
    return false;
}

std::optional<DatFile> parseDat(std::string_view text, std::string* error) {
    const auto start = bodyStart(text, error);
    if (!start)
        return std::nullopt;
    DatFile file;
    file.type = "a";
    TreeParser parser(text, *start);
    if (!parser.parseFile(file, error))
        return std::nullopt;
    return file;
}

std::optional<DatFile> parseDat(std::string_view text, const DatSchema& schema, std::string* error) {
    const auto start = bodyStart(text, error);
    if (!start)
        return std::nullopt;
    DatFile file;
    file.type = "a";
    SchemaParser parser(text, *start);
    if (!parser.parseFile(schema, file, error))
        return std::nullopt;
    return file;
}

} // namespace mm2::data
