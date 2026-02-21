#include "Lexer.h"
#include "../support/SourceManager.h"
#include "../support/Diagnostics.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace holyc {

/**
 * @brief keyword text से उसके TokenKind तक का singleton map लौटाता है।
 *
 * @return static keyword lookup map का reference।
 */
static const std::unordered_map<std::string_view, TokenKind>& keywordMap() {
    static const std::unordered_map<std::string_view, TokenKind> map = {
        {"U0",  TokenKind::U0},  {"I0",  TokenKind::I0},
        {"U8",  TokenKind::U8},  {"I8",  TokenKind::I8},
        {"U16", TokenKind::U16}, {"I16", TokenKind::I16},
        {"U32", TokenKind::U32}, {"I32", TokenKind::I32},
        {"U64", TokenKind::U64}, {"I64", TokenKind::I64},
        {"F64", TokenKind::F64}, {"F32", TokenKind::F32}, {"Bool", TokenKind::Bool},
        // Intrinsic union types — ये built-in union keyword हैं
        {"U8i",  TokenKind::U8i},  {"I8i",  TokenKind::I8i},
        {"U16i", TokenKind::U16i}, {"I16i", TokenKind::I16i},
        {"U32i", TokenKind::U32i}, {"I32i", TokenKind::I32i},
        {"U64i", TokenKind::U64i}, {"I64i", TokenKind::I64i},
        {"F64i", TokenKind::F64i},
        {"if",        TokenKind::If},
        {"else",      TokenKind::Else},
        {"for",       TokenKind::For},
        {"while",     TokenKind::While},
        {"do",        TokenKind::Do},
        {"switch",    TokenKind::Switch},
        {"case",      TokenKind::Case},
        {"default",   TokenKind::Default},
        {"break",     TokenKind::Break},
        {"continue",  TokenKind::Continue},
        {"return",    TokenKind::Return},
        {"goto",      TokenKind::Goto},
        {"class",     TokenKind::Class},
        {"union",     TokenKind::Union},
        {"enum",      TokenKind::Enum},
        {"typedef",   TokenKind::Typedef},
        {"sizeof",    TokenKind::Sizeof},
        {"typeof",    TokenKind::Typeof},
        {"offset",    TokenKind::Offset},
        {"lastclass", TokenKind::Lastclass},
        {"try",       TokenKind::Try},
        {"catch",     TokenKind::Catch},
        {"throw",     TokenKind::Throw},
        {"asm",       TokenKind::Asm},
        {"exe",       TokenKind::Exe},
        {"extern",    TokenKind::Extern},
        {"_extern",   TokenKind::_Extern},
        {"_intern",   TokenKind::_Intern},
        {"import",    TokenKind::Import},
        {"_import",   TokenKind::_Import},
        {"public",    TokenKind::Public},
        {"static",    TokenKind::Static},
        {"reg",       TokenKind::Reg},
        {"noreg",     TokenKind::NoReg},
        {"no_warn",   TokenKind::NoWarn},
        {"interrupt", TokenKind::Interrupt},
        {"haserrcode",TokenKind::HasErrCode},
        {"argpop",    TokenKind::ArgPop},
        {"noargpop",  TokenKind::NoArgPop},
        {"lock",      TokenKind::Lock},
        {"TRUE",      TokenKind::True},
        {"FALSE",     TokenKind::False},
        {"auto",      TokenKind::Auto},
    };
    return map;
}

/**
 * @brief preprocessor directive name से उसके TokenKind तक का singleton map लौटाता है।
 *
 * @return static preprocessor directive lookup map का reference।
 */
static const std::unordered_map<std::string_view, TokenKind>& ppMap() {
    static const std::unordered_map<std::string_view, TokenKind> map = {
        {"include", TokenKind::PP_Include},
        {"define",  TokenKind::PP_Define},
        {"undef",   TokenKind::PP_Undef},
        {"ifdef",   TokenKind::PP_Ifdef},
        {"ifndef",  TokenKind::PP_Ifndef},
        {"if",      TokenKind::PP_If},
        {"elif",    TokenKind::PP_Elif},
        {"else",    TokenKind::PP_Else},
        {"endif",   TokenKind::PP_Endif},
        {"ifaot",   TokenKind::PP_Ifaot},
        {"ifjit",   TokenKind::PP_Ifjit},
        {"assert",  TokenKind::PP_Assert},
        {"exe",     TokenKind::PP_Exe},
    };
    return map;
}

Lexer::Lexer(SourceManager& sm, int fileId, Diagnostics& diag)
    : sm_(sm), diag_(diag), fileId_(fileId),
      buf_(sm.getBuffer(fileId)), pos_(0) {}

char Lexer::current() const {
    return atEnd() ? '\0' : buf_[pos_];
}

char Lexer::peekChar(int ahead) const {
    size_t idx = pos_ + ahead;
    return idx < buf_.size() ? buf_[idx] : '\0';
}

char Lexer::advance() {
    if (atEnd()) return '\0';
    return buf_[pos_++];
}

bool Lexer::atEnd() const {
    return pos_ >= buf_.size();
}

SourceLocation Lexer::location() const {
    return sm_.getLocation(fileId_, pos_);
}

Token Lexer::makeToken(TokenKind kind, size_t startOffset) const {
    std::string_view text(buf_.data() + startOffset, pos_ - startOffset);
    SourceLocation loc = sm_.getLocation(fileId_, startOffset);
    return Token(kind, text, loc);
}

Token Lexer::makeError(size_t startOffset, const std::string& msg) {
    SourceLocation loc = sm_.getLocation(fileId_, startOffset);
    diag_.error(loc, msg);
    std::string_view text(buf_.data() + startOffset, pos_ - startOffset);
    return Token(TokenKind::Error, text, loc);
}

void Lexer::skipWhitespaceAndComments() {
    while (!atEnd()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
            continue;
        }
        if (c == '/' && peekChar() == '/') {
            skipLineComment();
            continue;
        }
        if (c == '/' && peekChar() == '*') {
            skipBlockComment();
            continue;
        }
        break;
    }
}

void Lexer::skipLineComment() {
    advance();
    advance();
    while (!atEnd() && current() != '\n')
        advance();
}

void Lexer::skipBlockComment() {
    size_t startOffset = pos_;
    advance();
    advance();
    int depth = 1;
    while (!atEnd() && depth > 0) {
        if (current() == '/' && peekChar() == '*') {
            advance(); advance();
            diag_.warning(sm_.getLocation(fileId_, pos_ - 2),
                          "nested block comment");
            depth++;
            continue;
        }
        if (current() == '*' && peekChar() == '/') {
            advance(); advance();
            depth--;
            continue;
        }
        advance();
    }
    if (depth > 0) {
        diag_.error(sm_.getLocation(fileId_, startOffset),
                    "unterminated block comment");
    }
}

char Lexer::scanEscape() {
    advance();
    if (atEnd()) return '\0';
    char c = advance();
    switch (c) {
    case '0':  return '\0';
    case '\'': return '\'';
    case '"':  return '"';
    case '\\': return '\\';
    case 'n':  return '\n';
    case 'r':  return '\r';
    case 't':  return '\t';
    case 'd':  return '$';
    case 'x': {
        char hi = advance();
        char lo = advance();
        auto fromHex = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
            if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
            return -1;
        };
        int h = fromHex(hi), l = fromHex(lo);
        if (h < 0 || l < 0) return '?';
        return static_cast<char>((h << 4) | l);
    }
    default:
        diag_.warning(sm_.getLocation(fileId_, pos_ - 1),
                      std::string("unknown escape sequence '\\") + c + "'");
        return c;
    }
}

Token Lexer::lexIdentifierOrKeyword() {
    size_t start = pos_;
    while (!atEnd() && (std::isalnum(static_cast<unsigned char>(current())) ||
                        current() == '_'))
        advance();
    std::string_view word(buf_.data() + start, pos_ - start);
    auto& kw = keywordMap();
    auto it = kw.find(word);
    if (it != kw.end()) {
        return Token(it->second, word, sm_.getLocation(fileId_, start));
    }
    return Token(TokenKind::Identifier, word, sm_.getLocation(fileId_, start));
}

Token Lexer::lexNumber() {
    size_t start = pos_;
    char c0 = current();

    if (c0 == '0' && !atEnd()) {
        char c1 = peekChar();
        if (c1 == 'x' || c1 == 'X') {
            advance(); advance();
            uint64_t val = 0;
            while (!atEnd() && std::isxdigit(static_cast<unsigned char>(current()))) {
                char d = advance();
                int v = (d >= 'a') ? 10 + d - 'a' : (d >= 'A') ? 10 + d - 'A' : d - '0';
                if (val > (UINT64_MAX - static_cast<uint64_t>(v)) / 16) {
                    diag_.warning(sm_.getLocation(fileId_, start),
                                  "hex literal overflows U64");
                    while (!atEnd() && std::isxdigit(static_cast<unsigned char>(current())))
                        advance();
                    break;
                }
                val = val * 16 + v;
            }
            Token tok = makeToken(TokenKind::IntLiteral, start);
            tok.uintVal = val;
            return tok;
        }
        if (c1 == 'b' || c1 == 'B') {
            advance(); advance();
            uint64_t val = 0;
            int binDigits = 0;
            while (!atEnd() && (current() == '0' || current() == '1')) {
                if (++binDigits > 64) {
                    diag_.warning(sm_.getLocation(fileId_, start),
                                  "binary literal overflows U64");
                    while (!atEnd() && (current() == '0' || current() == '1')) advance();
                    break;
                }
                val = val * 2 + (advance() - '0');
            }
            Token tok = makeToken(TokenKind::IntLiteral, start);
            tok.uintVal = val;
            return tok;
        }
        if (c1 >= '0' && c1 <= '7') {
            advance();
            uint64_t val = 0;
            bool octalOverflow = false;
            while (!atEnd() && current() >= '0' && current() <= '7') {
                uint64_t d = advance() - '0';
                if (val > (UINT64_MAX - d) / 8) {
                    if (!octalOverflow)
                        diag_.warning(sm_.getLocation(fileId_, start),
                                      "octal literal overflows U64");
                    octalOverflow = true;
                    while (!atEnd() && current() >= '0' && current() <= '7') advance();
                    break;
                }
                val = val * 8 + d;
            }
            Token tok = makeToken(TokenKind::IntLiteral, start);
            tok.uintVal = val;
            return tok;
        }
    }

    bool isFloat = false;

    while (!atEnd() && std::isdigit(static_cast<unsigned char>(current())))
        advance();

    if (current() == '.' && peekChar() != '.') {
        isFloat = true;
        advance();
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(current())))
            advance();
    }

    if (current() == 'e' || current() == 'E') {
        isFloat = true;
        advance();
        if (current() == '+' || current() == '-') advance();
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(current())))
            advance();
    }

    std::string_view text(buf_.data() + start, pos_ - start);
    SourceLocation loc = sm_.getLocation(fileId_, start);

    if (isFloat) {
        Token tok(TokenKind::FloatLiteral, text, loc);
        errno = 0;
        {
            char buf[64];
            size_t n = std::min(text.size(), size_t(63));
            std::memcpy(buf, text.data(), n);
            buf[n] = '\0';
            tok.floatVal = std::strtod(buf, nullptr);
        }
        if (errno == ERANGE)
            diag_.warning(loc, "floating-point literal out of representable range");
        return tok;
    }

    Token tok(TokenKind::IntLiteral, text, loc);
    errno = 0;
    {
        char buf[64];
        size_t n = std::min(text.size(), size_t(63));
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
        tok.uintVal = std::strtoull(buf, nullptr, 10);
    }
    if (errno == ERANGE)
        diag_.warning(loc, "integer literal overflows U64");
    return tok;
}

Token Lexer::lexString() {
    size_t start = pos_;
    advance();

    std::string value;
    while (!atEnd() && current() != '"') {
        if (current() == '\n') {
            return makeError(start, "unterminated string literal");
        }
        if (current() == '\\') {
            value += scanEscape();
        } else {
            value += advance();
        }
    }

    if (atEnd()) {
        return makeError(start, "unterminated string literal");
    }

    advance();
    return makeToken(TokenKind::StringLiteral, start);
}

Token Lexer::lexChar() {
    size_t start = pos_;
    advance();

    uint64_t val = 0;
    int byteCount = 0;

    while (!atEnd() && current() != '\'') {
        if (current() == '\n') {
            return makeError(start, "unterminated character literal");
        }
        char ch;
        if (current() == '\\') {
            ch = scanEscape();
        } else {
            ch = advance();
        }
        if (byteCount >= 8) {
            while (!atEnd() && current() != '\'' && current() != '\n')
                advance();
            diag_.error(sm_.getLocation(fileId_, start),
                        "character literal too long (max 8 bytes)");
            break;
        }
        val |= (static_cast<uint64_t>(static_cast<uint8_t>(ch)) << (byteCount * 8));
        byteCount++;
    }

    if (atEnd()) {
        return makeError(start, "unterminated character literal");
    }

    advance();

    if (byteCount == 0) {
        return makeError(start, "empty character literal");
    }

    Token tok = makeToken(TokenKind::CharLiteral, start);
    tok.uintVal = val;
    return tok;
}

Token Lexer::lexPreprocessor() {
    size_t start = pos_;
    advance();

    if (!atEnd() && current() == '#') {
        advance();
        return Token(TokenKind::PP_HashHash,
                     std::string_view(buf_.data() + start, pos_ - start),
                     sm_.getLocation(fileId_, start));
    }

    while (!atEnd() && (current() == ' ' || current() == '\t'))
        advance();

    size_t dirStart = pos_;
    while (!atEnd() && std::isalpha(static_cast<unsigned char>(current())))
        advance();

    std::string_view dirName(buf_.data() + dirStart, pos_ - dirStart);
    auto& pp = ppMap();
    auto it = pp.find(dirName);
    if (it != pp.end()) {
        return Token(it->second,
                     std::string_view(buf_.data() + start, pos_ - start),
                     sm_.getLocation(fileId_, start));
    }

    return Token(TokenKind::PP_Hash,
                 std::string_view(buf_.data() + start, pos_ - start),
                 sm_.getLocation(fileId_, start));
}

Token Lexer::peek() {
    size_t savedPos = pos_;
    Token tok = next();
    pos_ = savedPos;
    return tok;
}

Token Lexer::next() {
    skipWhitespaceAndComments();

    if (atEnd()) {
        return Token(TokenKind::Eof, std::string_view(), location());
    }

    char c = current();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return lexIdentifierOrKeyword();
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
        return lexNumber();
    }

    if (c == '.' && std::isdigit(static_cast<unsigned char>(peekChar()))) {
        return lexNumber();
    }

    if (c == '"') return lexString();
    if (c == '\'') return lexChar();
    if (c == '#') return lexPreprocessor();

    size_t start = pos_;

    switch (c) {
    case '+':
        advance();
        if (current() == '+') {
            advance();
            if (current() == '=') { advance(); return makeToken(TokenKind::PPAssign, start); }
            return makeToken(TokenKind::PlusPlus, start);
        }
        if (current() == '=') { advance(); return makeToken(TokenKind::PlusAssign, start); }
        return makeToken(TokenKind::Plus, start);

    case '-':
        advance();
        if (current() == '-') {
            advance();
            if (current() == '=') { advance(); return makeToken(TokenKind::MMAssign, start); }
            return makeToken(TokenKind::MinusMinus, start);
        }
        if (current() == '=') { advance(); return makeToken(TokenKind::MinusAssign, start); }
        if (current() == '>') { advance(); return makeToken(TokenKind::Arrow, start); }
        return makeToken(TokenKind::Minus, start);

    case '*':
        advance();
        if (current() == '=') { advance(); return makeToken(TokenKind::StarAssign, start); }
        return makeToken(TokenKind::Star, start);

    case '/':
        advance();
        if (current() == '=') { advance(); return makeToken(TokenKind::SlashAssign, start); }
        return makeToken(TokenKind::Slash, start);

    case '%':
        advance();
        if (current() == '=') { advance(); return makeToken(TokenKind::PercentAssign, start); }
        return makeToken(TokenKind::Percent, start);

    case '&':
        advance();
        if (current() == '&') { advance(); return makeToken(TokenKind::AmpAmp, start); }
        if (current() == '=') { advance(); return makeToken(TokenKind::AmpAssign, start); }
        return makeToken(TokenKind::Ampersand, start);

    case '|':
        advance();
        if (current() == '|') { advance(); return makeToken(TokenKind::PipePipe, start); }
        if (current() == '=') { advance(); return makeToken(TokenKind::PipeAssign, start); }
        return makeToken(TokenKind::Pipe, start);

    case '^':
        advance();
        if (current() == '^') { advance(); return makeToken(TokenKind::CaretCaret, start); }
        if (current() == '=') { advance(); return makeToken(TokenKind::CaretAssign, start); }
        return makeToken(TokenKind::Caret, start);

    case '~': advance(); return makeToken(TokenKind::Tilde, start);

    case '!':
        advance();
        if (current() == '=') { advance(); return makeToken(TokenKind::BangEq, start); }
        return makeToken(TokenKind::Bang, start);

    case '<':
        advance();
        if (current() == '<') {
            advance();
            if (current() == '=') { advance(); return makeToken(TokenKind::ShlAssign, start); }
            return makeToken(TokenKind::Shl, start);
        }
        if (current() == '=') { advance(); return makeToken(TokenKind::LessEq, start); }
        return makeToken(TokenKind::Less, start);

    case '>':
        advance();
        if (current() == '>') {
            advance();
            if (current() == '=') { advance(); return makeToken(TokenKind::ShrAssign, start); }
            return makeToken(TokenKind::Shr, start);
        }
        if (current() == '=') { advance(); return makeToken(TokenKind::GreaterEq, start); }
        return makeToken(TokenKind::Greater, start);

    case '=':
        advance();
        if (current() == '=') { advance(); return makeToken(TokenKind::EqEq, start); }
        return makeToken(TokenKind::Assign, start);

    case '.':
        advance();
        if (current() == '.') {
            advance();
                // तीसरा dot optional है (`...`) — हमेशा DotDot emit होता है
            if (current() == '.') { advance(); }
            return makeToken(TokenKind::DotDot, start);
        }
        return makeToken(TokenKind::Dot, start);

    case ',': advance(); return makeToken(TokenKind::Comma, start);
    case ';': advance(); return makeToken(TokenKind::Semicolon, start);

    case ':':
        advance();
        if (current() == ':') { advance(); return makeToken(TokenKind::DoubleColon, start); }
        return makeToken(TokenKind::Colon, start);

    case '?': advance(); return makeToken(TokenKind::Question, start);
    case '(': advance(); return makeToken(TokenKind::LParen, start);
    case ')': advance(); return makeToken(TokenKind::RParen, start);
    case '{': advance(); return makeToken(TokenKind::LBrace, start);
    case '}': advance(); return makeToken(TokenKind::RBrace, start);
    case '[': advance(); return makeToken(TokenKind::LBracket, start);
    case ']': advance(); return makeToken(TokenKind::RBracket, start);
    case '`': advance(); return makeToken(TokenKind::Backtick, start);

    case '\\':
        // line continuation है — Error emit करो ताकि collectLineTokens इसे हटा सके
        advance();
        if (!atEnd() && (current() == '\n' || current() == '\r')) {
            std::string_view text(buf_.data() + start, pos_ - start);
            SourceLocation loc = sm_.getLocation(fileId_, start);
            return Token(TokenKind::Error, text, loc);
        }
        return makeError(start, "unexpected backslash");

    default:
        advance();
        return makeError(start, std::string("unexpected character '") + c + "'");
    }
}

} // namespace holyc
