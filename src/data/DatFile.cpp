#include "data/DatFile.h"

#include "data/CNumbers.h"

#include <algorithm>
#include <format>

namespace mm2::data {
namespace {

// datMultiTokenizer::GetReadTokenizer reads exactly this many header bytes.
constexpr std::string_view kAsciiHeader = "type: a";
constexpr std::string_view kBinaryHeader = "type: b";

struct Token {
    enum class Kind { End, Word, Number, String, Open, Close } kind = Kind::End;
    std::string_view text;
    int line = 0;
};

// datBaseTokenizer::GetToken.
class Lexer {
public:
    Lexer(std::string_view text, std::size_t pos) : m_text(text), m_pos(pos) {}

    Token next() {
        skipSpaceAndComments();
        Token t;
        t.line = m_line;
        if (m_pos >= m_text.size())
            return t;
        if (m_text[m_pos] == '"') {
            // A quoted token runs to the next quote, across lines.
            const std::size_t start = ++m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '"') {
                if (m_text[m_pos] == '\n')
                    ++m_line;
                ++m_pos;
            }
            t.kind = Token::Kind::String;
            t.text = m_text.substr(start, m_pos - start);
            if (m_pos < m_text.size())
                ++m_pos;
            return t;
        }
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && !isSeparator(m_text[m_pos]) && m_text[m_pos] != kComment)
            ++m_pos;
        t.text = m_text.substr(start, m_pos - start);
        const char c = t.text.front();
        if (t.text == "{")
            t.kind = Token::Kind::Open;
        else if (t.text == "}")
            t.kind = Token::Kind::Close;
        else if ((c >= '0' && c <= '9') || c == '-' || c == '.')
            t.kind = Token::Kind::Number; // what datAsciiTokenizer::GetFloat accepts
        else
            t.kind = Token::Kind::Word;
        return t;
    }

private:
    // datBaseTokenizer::CommentChar.
    static constexpr char kComment = ';';

    static bool isSeparator(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0'; }

    void skipSpaceAndComments() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (isSeparator(c)) {
                ++m_pos;
            } else if (c == kComment) {
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
    Parser(std::string_view text, std::size_t start) : m_lex(text, start) { advance(); }

    bool parseFile(DatFile& out, std::string* error) {
        out.root.isBlock = true;
        // datParser::Load: the first token is the class name, whatever it is.
        if (m_tok.kind == Token::Kind::End)
            return true;
        DatNode top;
        top.name = std::string(m_tok.text);
        top.isBlock = true;
        advance();
        if (!parseBody(top, error))
            return false;
        out.root.children.push_back(std::move(top));
        // MM2 stops reading at the class block's closing brace.
        return true;
    }

private:
    void advance() { m_tok = m_lex.next(); }

    bool fail(std::string* error, std::string msg) const {
        if (error)
            *error = std::format("line {}: {}", m_tok.line, msg);
        return false;
    }

    // datParser::Read: fields up to the closing '}'. A stray '{' in field
    // position is skipped (that is how the block's own opening brace is
    // consumed).
    bool parseBody(DatNode& node, std::string* error) {
        while (true) {
            switch (m_tok.kind) {
            case Token::Kind::End:
                // MM2 keeps asking for tokens at the end of the file and never
                // returns; report the file as broken instead.
                return fail(error, std::format("unexpected end of file inside '{}'", node.name));
            case Token::Kind::Close: advance(); return true;
            case Token::Kind::Open: advance(); continue;
            default: break;
            }

            DatNode child;
            const int nameLine = m_tok.line;
            child.name = std::string(m_tok.text);
            advance();
            // Words and strings on the field's own line (labels, or the rest
            // of a multi-word name that MM2 cannot match).
            while ((m_tok.kind == Token::Kind::Word || m_tok.kind == Token::Kind::String) &&
                   m_tok.line == nameLine) {
                child.strings.emplace_back(m_tok.text);
                advance();
            }
            if (m_tok.kind == Token::Kind::Open) {
                advance();
                child.isBlock = true;
                if (!parseBody(child, error))
                    return false;
            } else {
                while (m_tok.kind == Token::Kind::Number) {
                    child.numbers.push_back(cAtof(m_tok.text));
                    child.numberTexts.emplace_back(m_tok.text);
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
    DatFile file;
    file.type = "a";
    if (text.starts_with(kBinaryHeader)) {
        // MM2 reads these with datBinTokenizer; no retail file uses one.
        if (error)
            *error = "binary ('type: b') data files are not supported";
        return std::nullopt;
    }
    // GetReadTokenizer consumes the seven header bytes whatever they are; an
    // unknown header is logged and the rest is read as text.
    Parser parser(text, std::min(text.size(), kAsciiHeader.size()));
    if (!parser.parseFile(file, error))
        return std::nullopt;
    return file;
}

} // namespace mm2::data
