#include "data/DatFile.h"

#include "core/StringUtil.h"

#include <charconv>
#include <format>

namespace mm2::data {
namespace {

struct Token {
    enum class Kind { End, Ident, Number, String, Open, Close, Colon } kind = Kind::End;
    std::string_view text;
    double number = 0;
    int line = 0;
};

class Lexer {
public:
    explicit Lexer(std::string_view text) : m_text(text) {}

    Token next() {
        skipSpaceAndComments();
        Token t;
        t.line = m_line;
        if (m_pos >= m_text.size())
            return t;
        const char c = m_text[m_pos];
        if (c == '{' || c == '}' || c == ':') {
            t.kind = c == '{' ? Token::Kind::Open : (c == '}' ? Token::Kind::Close : Token::Kind::Colon);
            t.text = m_text.substr(m_pos++, 1);
            return t;
        }
        if (c == '"') {
            const std::size_t start = ++m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '"' && m_text[m_pos] != '\n')
                ++m_pos;
            t.kind = Token::Kind::String;
            t.text = m_text.substr(start, m_pos - start);
            if (m_pos < m_text.size() && m_text[m_pos] == '"')
                ++m_pos;
            return t;
        }
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && !isSpace(m_text[m_pos]) && m_text[m_pos] != '{' && m_text[m_pos] != '}' &&
               m_text[m_pos] != ':' && m_text[m_pos] != '"' && m_text[m_pos] != ';')
            ++m_pos;
        t.text = m_text.substr(start, m_pos - start);
        if (isNumberStart(c)) {
            std::string_view s = t.text;
            if (s.starts_with('+'))
                s.remove_prefix(1);
            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), t.number);
            if (ec == std::errc{} && ptr == s.data() + s.size()) {
                t.kind = Token::Kind::Number;
                return t;
            }
        }
        t.kind = Token::Kind::Ident;
        return t;
    }

private:
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
    static bool isNumberStart(char c) { return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.'; }

    void skipSpaceAndComments() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (isSpace(c)) {
                ++m_pos;
            } else if (c == ';') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n')
                    ++m_pos;
            } else {
                break;
            }
        }
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_line = 1;
};

class Parser {
public:
    explicit Parser(std::string_view text) : m_lex(text) { advance(); }

    bool parseFile(DatFile& out, std::string* error) {
        // Header "type: a".
        if (m_tok.kind == Token::Kind::Ident && m_tok.text == "type") {
            advance();
            if (m_tok.kind != Token::Kind::Colon)
                return fail(error, "expected ':' after 'type'");
            advance();
            if (m_tok.kind != Token::Kind::Ident)
                return fail(error, "expected data type after 'type:'");
            out.type = std::string(m_tok.text);
            advance();
            if (out.type != "a")
                return fail(error, std::format("unsupported data type '{}'", out.type));
        } else {
            out.type = "a";
        }
        out.root.isBlock = true;
        if (!parseBody(out.root, error, /*topLevel=*/true))
            return false;
        return true;
    }

private:
    void advance() { m_tok = m_lex.next(); }

    bool fail(std::string* error, std::string msg) const {
        if (error)
            *error = std::format("line {}: {}", m_tok.line, msg);
        return false;
    }

    bool parseBody(DatNode& node, std::string* error, bool topLevel) {
        while (true) {
            if (m_tok.kind == Token::Kind::End) {
                if (topLevel)
                    return true;
                return fail(error, std::format("unexpected end of file inside '{}'", node.name));
            }
            if (m_tok.kind == Token::Kind::Close) {
                if (topLevel)
                    return fail(error, "unmatched '}'");
                advance();
                return true;
            }
            if (m_tok.kind != Token::Kind::Ident)
                return fail(error, std::format("expected a field name, got '{}'", m_tok.text));

            // Field names may be several words, but only on one line.
            DatNode child;
            const int nameLine = m_tok.line;
            child.name = std::string(m_tok.text);
            advance();
            while (m_tok.kind == Token::Kind::Ident && m_tok.line == nameLine) {
                child.name += ' ';
                child.name += m_tok.text;
                advance();
            }

            // Optional label ("vehCarSim :0 {"), as written by the Angel serializer.
            if (m_tok.kind == Token::Kind::Colon) {
                advance();
                if (m_tok.kind == Token::Kind::Number || m_tok.kind == Token::Kind::Ident)
                    advance();
            }

            if (m_tok.kind == Token::Kind::Open) {
                advance();
                child.isBlock = true;
                if (!parseBody(child, error, false))
                    return false;
            } else {
                while (m_tok.kind == Token::Kind::Number || m_tok.kind == Token::Kind::String) {
                    if (m_tok.kind == Token::Kind::Number)
                        child.numbers.push_back(m_tok.number);
                    else
                        child.strings.emplace_back(m_tok.text);
                    advance();
                }
            }
            node.children.push_back(std::move(child));
        }
    }

    Lexer m_lex;
    Token m_tok;
};

} // namespace

const DatNode* DatNode::child(std::string_view key) const {
    for (const auto& c : children)
        if (c.name == key)
            return &c;
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
    if (!c || c->numbers.empty())
        return std::nullopt;
    return static_cast<int>(c->numbers[0]);
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
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    DatFile file;
    Parser parser(text);
    if (!parser.parseFile(file, error))
        return std::nullopt;
    return file;
}

} // namespace mm2::data
