#include "Formatter.h"

#include <cassert>
#include <cstring>
#include <sstream>
#include <string>

namespace holyc {

Formatter::Formatter(const std::vector<Token>& tokens)
    : toks_(tokens), pos_(0), indent_(0),
      prev_kind_(TokenKind::Eof), at_line_start_(true), paren_depth_(0) {}

void Formatter::setSource(const std::string& src) {
    src_ = src;
    has_src_ = true;
    splitSourceLines();
    buildTokLines();
}

// ============================================================================
// Source-line helpers — raw source lines से जुड़े helper functions
// ============================================================================

/**
 * @brief src_ को src_lines_ में '\n' boundaries पर split करो।
 */
void Formatter::splitSourceLines() {
    src_lines_.clear();
    std::string line;
    for (char c : src_) {
        if (c == '\n') {
            src_lines_.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty() || (!src_.empty() && src_.back() == '\n'))
        src_lines_.push_back(line);
}

/**
 * @brief tok_lines_ को हर token के लिए 0-based source line index से populate करो।
 */
void Formatter::buildTokLines() {
    // Token loc.line 1-based होता है
    tok_lines_.resize(toks_.size(), 0);
    for (size_t i = 0; i < toks_.size(); ++i) {
        uint32_t line = toks_[i].loc.line;
        tok_lines_[i] = (line > 0) ? (line - 1) : 0; // 0-based में convert करो
    }
}

std::string_view Formatter::srcLine(uint32_t idx) const {
    if (idx < src_lines_.size())
        return src_lines_[idx];
    return {};
}

/**
 * @brief Source line पर trailing // comment (string literals के बाहर) लौटाओ,
 * या कोई न हो तो empty string।
 *
 * @param line_idx 0-based source line index।
 * @return "//" से शुरू होने वाला trailing comment text, या कोई न हो तो empty string।
 */
std::string Formatter::trailingComment(uint32_t line_idx) const {
    if (line_idx >= src_lines_.size()) return {};
    const std::string& line = src_lines_[line_idx];

    bool in_str = false;
    bool in_char = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (in_char) {
            if (c == '\\') { ++i; continue; }
            if (c == '\'') in_char = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '\'') { in_char = true; continue; }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            // // comment मिल गया
            return line.substr(i);
        }
    }
    return {};
}

/**
 * @brief last_src_line_ (exclusive) और before_line (exclusive) के बीच
 * source lines में आने वाले block comments (/* ... *\/) verbatim emit करो।
 *
 * Block comments के interior lines verbatim emit होते हैं (opening /* के relative
 * original indentation preserve करते हुए)।
 * last_src_line_ को आखिरी emit हुई line पर update करता है।
 *
 * @param before_line अगले token के source line का 0-based index।
 */
void Formatter::emitBlockCommentsBefore(uint32_t before_line) {
    if (!has_src_) return;
    if (before_line <= last_src_line_ + 1) return;

    // Block comment content के लिए source lines scan करो।
    // /* ... */ comment के अंदर हैं या नहीं track करो।
    bool in_block = false;
    uint32_t block_start_line = 0;

    // First pass: [last_src_line_+1, before_line) में block comment ranges ढूंढो
    // और emit करो।
    for (uint32_t i = last_src_line_ + 1; i < before_line; ++i) {
        const std::string& line = src_lines_[i];

        if (!in_block) {
            // Check करो कि यह line block comment शुरू करती है
            size_t pos = 0;
            // /* ढूंढने के लिए leading whitespace skip करो
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            if (pos + 1 < line.size() && line[pos] == '/' && line[pos + 1] == '*') {
                in_block = true;
                block_start_line = i;
                // Emit से पहले line boundary पर हैं यह ensure करो
                if (!out_.empty() && out_.back() != '\n') {
                    out_ += '\n';
                    at_line_start_ = true;
                }
                // Opening line verbatim emit करो
                out_.append(line);
                out_ += '\n';
                at_line_start_ = true;
                last_src_line_ = i;
                // Check करो कि /* ... */ same line पर close होता है
                size_t close = line.find("*/", pos + 2);
                if (close != std::string::npos) {
                    in_block = false;
                }
            }
        } else {
            // Block comment के अंदर हैं: यह line verbatim emit करो
            out_.append(line);
            out_ += '\n';
            at_line_start_ = true;
            last_src_line_ = i;
            // Check करो कि block comment इस line पर close होता है
            if (line.find("*/") != std::string::npos) {
                in_block = false;
            }
        }
    }
    (void)block_start_line; // unused warning suppress करो
}

/**
 * @brief last_src_line_ और before_line के बीच source की blank lines emit करो।
 *
 * Multiple consecutive blanks को एक में collapse करता है।
 * out_ already दो newlines से end होती हो तो blank emit नहीं करता।
 *
 * @param before_line अगले token के source line का 0-based index।
 */
void Formatter::emitBlankLinesBefore(uint32_t before_line) {
    if (!has_src_) return;
    // Source range (last_src_line_, before_line) में blank lines count करो
    bool has_blank_in_source = false;
    for (uint32_t i = last_src_line_ + 1; i < before_line; ++i) {
        std::string_view sl = srcLine(i);
        // Check करो कि line blank है (सिर्फ whitespace)
        bool blank = true;
        for (char c : sl) {
            if (c != ' ' && c != '\t' && c != '\r') { blank = false; break; }
        }
        if (blank) {
            has_blank_in_source = true;
            break; // blank emission trigger के लिए एक काफी है
        }
    }
    if (!has_blank_in_source) return;
    // out_ already दो newlines से end होती हो तो emit मत करो (double blanks avoid करो)
    if (out_.size() >= 2 && out_[out_.size()-1] == '\n' && out_[out_.size()-2] == '\n')
        return;
    // पहले line start पर हैं यह ensure करो
    if (!out_.empty() && out_.back() != '\n') {
        out_ += '\n';
        at_line_start_ = true;
    }
    out_ += '\n';
    at_line_start_ = true;
}

// ============================================================================
// Helpers — token navigation और output emission के helper functions
// ============================================================================

/**
 * @brief pos_ + offset पर token लौटाओ, या range से बाहर हो तो static EOF token।
 *
 * @param offset Current position से relative offset (default 0)।
 * @return pos_ + offset पर token का reference।
 */
const Token& Formatter::tok(int offset) const {
    size_t idx = pos_ + (size_t)offset;
    static Token eofTok{TokenKind::Eof, {}, {}};
    if (idx >= toks_.size()) return eofTok;
    return toks_[idx];
}

TokenKind Formatter::nextKind(int offset) const {
    return tok(offset).kind;
}

/**
 * @brief s को directly out_ पर append करो और at_line_start_ flag clear करो।
 *
 * @param s Verbatim append करने वाला text।
 */
void Formatter::emitRaw(std::string_view s) {
    out_.append(s.data(), s.size());
    at_line_start_ = false;
}

/**
 * @brief out_ से trailing spaces strip करो, '\n' append करो, और at_line_start_ set करो।
 */
void Formatter::emitNewline() {
    // Newline से पहले trailing spaces remove करो
    while (!out_.empty() && out_.back() == ' ') out_.pop_back();
    out_ += '\n';
    at_line_start_ = true;
}

/**
 * @brief out_ पर indent_ * 2 spaces append करो।
 */
void Formatter::emitIndent() {
    int n = indent_ * 2;
    for (int i = 0; i < n; i++) out_ += ' ';
    at_line_start_ = false;
}

/**
 * @brief out_ पर single space append करो अगर out_ already space या newline से end नहीं होती।
 */
void Formatter::emitSpace() {
    if (!out_.empty() && out_.back() != ' ' && out_.back() != '\n')
        out_ += ' ';
}

/**
 * @brief True लौटाओ अगर token binary operator के बाद आ सकता है (prefix/unary context नहीं)।
 *
 * @param k Test करने वाला token kind।
 * @return True अगर k value-ending token kind है।
 */
bool Formatter::isValueEnd(TokenKind k) const {
    return k == TokenKind::Identifier
        || k == TokenKind::IntLiteral
        || k == TokenKind::FloatLiteral
        || k == TokenKind::CharLiteral
        || k == TokenKind::RParen
        || k == TokenKind::RBracket
        || k == TokenKind::True
        || k == TokenKind::False
        || k == TokenKind::PlusPlus    // postfix ++ operator — value के बाद increment
        || k == TokenKind::MinusMinus; // postfix -- operator — value के बाद decrement
}

/**
 * @brief True लौटाओ अगर token binary operator है जिसके around spaces चाहिए।
 *
 * @param k Test करने वाला token kind।
 * @return True अगर k binary operator kind है।
 */
bool Formatter::isBinaryOp(TokenKind k) const {
    switch (k) {
    case TokenKind::Plus:       case TokenKind::Minus:
    case TokenKind::Slash:      case TokenKind::Percent:
    case TokenKind::Pipe:       case TokenKind::Caret:
    case TokenKind::Shl:        case TokenKind::Shr:
    case TokenKind::EqEq:       case TokenKind::BangEq:
    case TokenKind::Less:       case TokenKind::Greater:
    case TokenKind::LessEq:     case TokenKind::GreaterEq:
    case TokenKind::AmpAmp:     case TokenKind::PipePipe:
    case TokenKind::CaretCaret:
    case TokenKind::Assign:
    case TokenKind::PlusAssign: case TokenKind::MinusAssign:
    case TokenKind::StarAssign: case TokenKind::SlashAssign:
    case TokenKind::PercentAssign:
    case TokenKind::AmpAssign:  case TokenKind::PipeAssign:
    case TokenKind::CaretAssign:
    case TokenKind::ShlAssign:  case TokenKind::ShrAssign:
    case TokenKind::PPAssign:   case TokenKind::MMAssign:
    case TokenKind::Backtick:   // power operator — घात का operator
        return true;
    default:
        return false;
    }
}

/**
 * @brief True लौटाओ अगर token type specifier शुरू करता है (primitive keywords)।
 *
 * @param k Test करने वाला token kind।
 * @return True अगर k primitive type keyword है।
 */
bool Formatter::isTypeStart(TokenKind k) const {
    switch (k) {
    case TokenKind::U0: case TokenKind::I0:
    case TokenKind::U8: case TokenKind::I8:
    case TokenKind::U16: case TokenKind::I16:
    case TokenKind::U32: case TokenKind::I32:
    case TokenKind::U64: case TokenKind::I64:
    case TokenKind::F64: case TokenKind::Bool:
    case TokenKind::U8i: case TokenKind::I8i:
    case TokenKind::U16i: case TokenKind::I16i:
    case TokenKind::U32i: case TokenKind::I32i:
    case TokenKind::U64i: case TokenKind::I64i:
    case TokenKind::F64i:
        return true;
    default:
        return false;
    }
}

// ============================================================================
// format() — main formatting function जो सभी tokens process करती है
// ============================================================================

/**
 * @brief सभी tokens iterate करो और formatted output emit करो; complete result string लौटाओ।
 *
 * @return Newline से end होने वाला formatted HolyC source text।
 */
std::string Formatter::format() {
    out_.clear();
    pos_ = 0;
    indent_ = 0;
    prev_kind_ = TokenKind::Eof;
    at_line_start_ = true;
    paren_depth_ = 0;
    last_src_line_ = 0;
    in_define_continuation_ = false;

    while (!atEnd()) {
        const Token& t = tok();
        TokenKind k = t.kind;

        if (k == TokenKind::Eof) break;

        // Current token का source line (0-based)
        uint32_t cur_src_line = has_src_ ? tok_lines_[pos_] : 0;

        // --- Fix 3: Multi-line block comment preservation — multi-line /* */ comments सुरक्षित रखना ---
        // यह token emit करने से पहले, check करो कि last_src_line_ और
        // इस token की line के बीच source lines में /* ... */ block comments हैं।
        // Block comments lexer द्वारा skip होते हैं इसलिए उनके tokens नहीं होते।
        if (has_src_ && cur_src_line > last_src_line_ + 1) {
            emitBlockCommentsBefore(cur_src_line);
        }

        // --- Fix 4: Blank line preservation — blank lines सुरक्षित रखना ---
        // यह token emit करने से पहले, check करो कि last emitted source line
        // और इस token की source line के बीच blank source lines हैं।
        // यह सिर्फ top-level (indent_ == 0) और line start पर करो
        // (यानी top-level items के बीच), और सिर्फ तब जब यह token अपनी
        // source line पर first हो।
        if (has_src_ && at_line_start_ && indent_ == 0
            && cur_src_line > last_src_line_) {
            emitBlankLinesBefore(cur_src_line);
        }

        // --- Fix 1: #define continuation lines — #define की continuation lines सुरक्षित रखना ---
        // अगर current token किसी ऐसी source line पर है जो #define
        // continuation line है, और हम उसे code tokens के रूप में process
        // करने वाले हैं, तो उसे skip करो — वह पहले ही #define line के
        // हिस्से के रूप में output हो चुकी है।
        // Actually: continuation lines में macro body होती है। Lexer
        // body को normally tokenise करता है। हमें उन tokens को suppress
        // करना है और raw continuation line emit करनी है।
        // Switch detect करो: in_define_continuation_ true होने पर,
        // continuation end होने तक raw source lines emit करो।

        // Detect करो: current token PP_Define है
        if (k == TokenKind::PP_Define && has_src_) {
            // PP_Define token emit करो (#define keyword)
            if (!at_line_start_) emitNewline();
            emitRaw(std::string(t.text));

            // अब source line का बाकी हिस्सा verbatim emit करो (name, args, value)
            // क्योंकि lexer इन्हें कई tokens में split कर सकता है।
            std::string_view full_line = srcLine(cur_src_line);
            // Token column (1-based) use करो line में token का end ढूंढने के लिए,
            // फिर उसके बाद सब कुछ emit करो।
            std::string rest;
            if (t.loc.col > 0 && t.text.size() > 0) {
                // col 1-based है; token line में col-1 पर शुरू होता है
                size_t tok_end_in_line = (t.loc.col - 1) + t.text.size();
                if (tok_end_in_line <= full_line.size()) {
                    rest = std::string(full_line.substr(tok_end_in_line));
                }
            } else {
                // Fallback: line में token text ढूंढो
                size_t kw_end = full_line.find(t.text);
                if (kw_end != std::string::npos) {
                    rest = std::string(full_line.substr(kw_end + t.text.size()));
                }
            }
            emitRaw(rest);

            // Check करो कि यह line '\' से end होती है
            std::string trim_line(full_line);
            while (!trim_line.empty() &&
                   (trim_line.back() == ' ' || trim_line.back() == '\t'))
                trim_line.pop_back();
            bool cont = (!trim_line.empty() && trim_line.back() == '\\');

            // Continuation lines verbatim emit करो
            uint32_t cont_line = cur_src_line + 1;
            while (cont && cont_line < src_lines_.size()) {
                emitNewline();
                emitRaw(src_lines_[cont_line]);
                std::string tl = src_lines_[cont_line];
                while (!tl.empty() && (tl.back() == ' ' || tl.back() == '\t'))
                    tl.pop_back();
                cont = (!tl.empty() && tl.back() == '\\');
                cont_line++;
            }

            // अब सभी tokens skip करो जो इस #define line (और
            // continuation lines) से belong करते हैं। वे tokens skip करो
            // जिनकी source line <= last continuation line जो हमने emit की।
            uint32_t last_define_line = cont_line - 1;
            prev_kind_ = k;
            pos_++;
            while (!atEnd()) {
                uint32_t tl2 = tok_lines_[pos_];
                if (tl2 > last_define_line) break;
                pos_++;
            }
            last_src_line_ = last_define_line;
            emitNewline();
            continue;
        }

        // Tokens process करते वक्त last_src_line_ update करो
        if (cur_src_line > last_src_line_)
            last_src_line_ = cur_src_line;

        switch (k) {

        // ── Block open — { से block शुरू होता है ─────────────────────────
        case TokenKind::LBrace:
            if (at_line_start_) {
                emitIndent();
            } else {
                emitSpace();
            }
            emitRaw("{");
            indent_++;
            emitNewline();
            break;

        // ── Block close — } से block बंद होता है ─────────────────────────
        case TokenKind::RBrace: {
            indent_--;
            if (indent_ < 0) indent_ = 0;
            if (!at_line_start_) emitNewline();
            emitIndent();
            emitRaw("}");
            // } के बाद newline add करो जब तक ; , ) या दूसरा } follow न हो
            TokenKind nk = nextKind(1);
            if (nk != TokenKind::Semicolon
                && nk != TokenKind::Comma
                && nk != TokenKind::RParen
                && nk != TokenKind::RBrace
                && nk != TokenKind::Eof) {
                emitNewline();
                // Top-level declarations के बाद blank line।
                // Top-level items के बीच हमेशा एक blank line insert करो।
                // emitBlankLinesBefore (Fix 4) double-blanks avoid करेगा।
                if (indent_ == 0 && nk != TokenKind::Eof) {
                    out_ += '\n';
                }
            }
            break;
        }

        // ── Semicolon — statement terminator (statement खत्म करने वाला) ──
        case TokenKind::Semicolon:
            emitRaw(";");
            // for(...) के अंदर newline मत add करो
            if (paren_depth_ == 0) {
                // --- Fix 2: Trailing comment preservation — trailing // comments सुरक्षित रखना ---
                // ; के बाद, check करो कि source line पर trailing //
                // comment है और newline से पहले उसे same line पर emit करो।
                if (has_src_) {
                    std::string cmt = trailingComment(cur_src_line);
                    if (!cmt.empty()) {
                        emitSpace();
                        emitRaw(cmt);
                    }
                }
                TokenKind nk = nextKind(1);
                if (nk != TokenKind::Eof && nk != TokenKind::RBrace) {
                    emitNewline();
                }
            } else {
                emitSpace();
            }
            break;

        // ── Comma — arguments/elements separator (तर्क विभाजक) ──────────
        case TokenKind::Comma:
            emitRaw(",");
            emitSpace();
            break;

        // ── Paren open — ( खुलने वाला parenthesis ────────────────────────
        case TokenKind::LParen:
            paren_depth_++;
            // ( से पहले space नहीं जब previous ident/type/] हो (call या cast)
            if (!isValueEnd(prev_kind_) && !isTypeStart(prev_kind_)
                && prev_kind_ != TokenKind::Identifier
                && prev_kind_ != TokenKind::RBracket
                && prev_kind_ != TokenKind::Star) {
                emitSpace();
            }
            emitRaw("(");
            break;

        // ── Paren close — ) बंद होने वाला parenthesis ───────────────────
        case TokenKind::RParen:
            if (paren_depth_ > 0) paren_depth_--;
            emitRaw(")");
            break;

        // ── Bracket open — [ array indexing bracket (array का index access) ─
        case TokenKind::LBracket:
            // Indexing करते वक्त [ से पहले space नहीं (prev ident या ] है)
            if (prev_kind_ != TokenKind::Identifier
                && prev_kind_ != TokenKind::RParen
                && prev_kind_ != TokenKind::RBracket) {
                emitSpace();
            }
            emitRaw("[");
            break;

        // ── Bracket close — ] बंद होने वाला bracket ─────────────────────
        case TokenKind::RBracket:
            emitRaw("]");
            break;

        // ── Member access (कोई spaces नहीं) ──────────────────────────────
        case TokenKind::Dot:
        case TokenKind::Arrow:
        case TokenKind::DoubleColon:
            emitRaw(std::string(t.text));
            break;

        // ── Colon (case labels, bit fields, ternary) — विभिन्न contexts में : ──
        case TokenKind::Colon:
            emitRaw(":");
            emitSpace();
            break;

        // ── Ternary ? — ternary expression का ? operator ─────────────────
        case TokenKind::Question:
            emitSpace();
            emitRaw("?");
            emitSpace();
            break;

        // ── Star (pointer या multiply) ────────────────────────────────────
        case TokenKind::Star:
            // Star pointer declarator है जब:
            //   prev type keyword है, या prev दूसरा * है
            //   नहीं तो value-end होने पर binary multiply है
            if (isTypeStart(prev_kind_) || prev_kind_ == TokenKind::Star
                || prev_kind_ == TokenKind::Ampersand) {
                // Pointer declarator: type से attach, कोई spaces नहीं
                emitRaw("*");
            } else if (isValueEnd(prev_kind_)) {
                // Binary multiply: spaces — binary * के आसपास spaces चाहिए
                emitSpace();
                emitRaw("*");
                emitSpace();
            } else {
                // Prefix dereference: बाद में space नहीं, लेकिन पहले space अगर ज़रूरी हो
                if (prev_kind_ != TokenKind::LParen
                    && prev_kind_ != TokenKind::LBrace
                    && prev_kind_ != TokenKind::Comma
                    && prev_kind_ != TokenKind::Assign
                    && !isBinaryOp(prev_kind_)) {
                    emitSpace();
                }
                emitRaw("*");
            }
            break;

        // ── Ampersand (address-of या bitwise &) ───────────────────────────
        case TokenKind::Ampersand:
            if (isValueEnd(prev_kind_)) {
                emitSpace();
                emitRaw("&");
                emitSpace();
            } else {
                // Prefix address-of — unary & operator (address लेना)
                if (!at_line_start_ && prev_kind_ != TokenKind::LParen
                    && prev_kind_ != TokenKind::Comma
                    && !isBinaryOp(prev_kind_)) {
                    emitSpace();
                }
                emitRaw("&");
            }
            break;

        // ── Unary ! ~ — logical not और bitwise not operators ─────────────
        case TokenKind::Bang:
        case TokenKind::Tilde:
            if (!at_line_start_ && prev_kind_ != TokenKind::LParen
                && prev_kind_ != TokenKind::LBrace
                && prev_kind_ != TokenKind::Comma
                && !isBinaryOp(prev_kind_)) {
                emitSpace();
            }
            emitRaw(std::string(t.text));
            break;

        // ── ++ -- (prefix या postfix) ─────────────────────────────────────
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus:
            // दोनों तरफ कोई spaces नहीं
            emitRaw(std::string(t.text));
            break;

        // ── Binary operators (spaces चाहिए) ──────────────────────────────
        case TokenKind::Plus:
        case TokenKind::Minus:
            // Unary या binary हो सकता है
            if (isValueEnd(prev_kind_)) {
                emitSpace();
                emitRaw(std::string(t.text));
                emitSpace();
            } else {
                // Unary: बाद में space नहीं
                if (!at_line_start_ && prev_kind_ != TokenKind::LParen
                    && prev_kind_ != TokenKind::LBrace
                    && prev_kind_ != TokenKind::Comma
                    && !isBinaryOp(prev_kind_)) {
                    emitSpace();
                }
                emitRaw(std::string(t.text));
            }
            break;

        case TokenKind::Slash:
        case TokenKind::Percent:
        case TokenKind::Pipe:
        case TokenKind::Caret:
        case TokenKind::Shl:
        case TokenKind::Shr:
        case TokenKind::EqEq:
        case TokenKind::BangEq:
        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEq:
        case TokenKind::GreaterEq:
        case TokenKind::AmpAmp:
        case TokenKind::PipePipe:
        case TokenKind::CaretCaret:
        case TokenKind::Assign:
        case TokenKind::PlusAssign:
        case TokenKind::MinusAssign:
        case TokenKind::StarAssign:
        case TokenKind::SlashAssign:
        case TokenKind::PercentAssign:
        case TokenKind::AmpAssign:
        case TokenKind::PipeAssign:
        case TokenKind::CaretAssign:
        case TokenKind::ShlAssign:
        case TokenKind::ShrAssign:
        case TokenKind::PPAssign:
        case TokenKind::MMAssign:
        case TokenKind::Backtick:
            emitSpace();
            emitRaw(std::string(t.text));
            emitSpace();
            break;

        // ── Type keywords — primitive type specifiers (प्रकार के keywords) ─
        case TokenKind::U0:  case TokenKind::I0:
        case TokenKind::U8:  case TokenKind::I8:
        case TokenKind::U16: case TokenKind::I16:
        case TokenKind::U32: case TokenKind::I32:
        case TokenKind::U64: case TokenKind::I64:
        case TokenKind::F64: case TokenKind::Bool:
        case TokenKind::U8i: case TokenKind::I8i:
        case TokenKind::U16i: case TokenKind::I16i:
        case TokenKind::U32i: case TokenKind::I32i:
        case TokenKind::U64i: case TokenKind::I64i:
        case TokenKind::F64i: {
            // Type keywords: top level पर } या ; के बाद new line पर शुरू
            if (at_line_start_) {
                emitIndent();
            } else {
                emitSpace();
            }
            emitRaw(std::string(t.text));
            break;
        }

        // ── Control-flow keywords — if/for/while/switch आदि ──────────────
        case TokenKind::If:
        case TokenKind::Else:
        case TokenKind::For:
        case TokenKind::While:
        case TokenKind::Do:
        case TokenKind::Switch:
        case TokenKind::Return:
        case TokenKind::Break:
        case TokenKind::Continue:
        case TokenKind::Goto:
        case TokenKind::Case:
        case TokenKind::Default:
        case TokenKind::Try:
        case TokenKind::Catch:
        case TokenKind::Throw: {
            if (at_line_start_) {
                emitIndent();
            } else {
                emitSpace();
            }
            emitRaw(std::string(t.text));
            // Keyword के बाद space (expression या block से पहले)
            if (nextKind(1) != TokenKind::Semicolon
                && nextKind(1) != TokenKind::LBrace
                && nextKind(1) != TokenKind::Eof) {
                emitSpace();
            }
            break;
        }

        // ── Structure keywords — class/union/typedef/extern आदि ──────────
        case TokenKind::Class:
        case TokenKind::Union:
        case TokenKind::Typedef:
        case TokenKind::Extern:
        case TokenKind::Static:
        case TokenKind::Public:
        case TokenKind::Asm:
        case TokenKind::Exe: {
            if (at_line_start_) {
                emitIndent();
            } else {
                emitSpace();
            }
            emitRaw(std::string(t.text));
            emitSpace();
            break;
        }

        // ── String literal — quoted string values (उद्धृत string मान) ───
        case TokenKind::StringLiteral: {
            if (at_line_start_) {
                emitIndent();
            } else if (prev_kind_ != TokenKind::LParen
                       && prev_kind_ != TokenKind::Comma) {
                emitSpace();
            }
            // text में surrounding quotes already include हैं
            out_.append(t.text.data(), t.text.size());
            at_line_start_ = false;
            break;
        }

        // ── Identifier — variable/function names (पहचानकर्ता) ───────────
        case TokenKind::Identifier: {
            if (at_line_start_) {
                emitIndent();
            } else {
                // Ident से पहले space जब तक prev . -> :: ( [ या type* नहीं
                if (prev_kind_ != TokenKind::Dot
                    && prev_kind_ != TokenKind::Arrow
                    && prev_kind_ != TokenKind::DoubleColon
                    && prev_kind_ != TokenKind::LParen
                    && prev_kind_ != TokenKind::LBracket
                    && prev_kind_ != TokenKind::Star   // pointer: कोई space नहीं
                    && prev_kind_ != TokenKind::Ampersand  // addr-of — address-of operator (& से address लेना)
                    && prev_kind_ != TokenKind::Bang
                    && prev_kind_ != TokenKind::Tilde
                    && prev_kind_ != TokenKind::PlusPlus
                    && prev_kind_ != TokenKind::MinusMinus) {
                    emitSpace();
                }
            }
            emitRaw(t.text);
            break;
        }

        // ── Integer / Float / Char literals — numeric और character values ─
        case TokenKind::IntLiteral:
        case TokenKind::FloatLiteral:
        case TokenKind::CharLiteral: {
            if (at_line_start_) {
                emitIndent();
            } else if (prev_kind_ != TokenKind::LParen
                       && prev_kind_ != TokenKind::LBracket
                       && prev_kind_ != TokenKind::Bang
                       && prev_kind_ != TokenKind::Tilde
                       && prev_kind_ != TokenKind::Minus
                       && prev_kind_ != TokenKind::Plus) {
                emitSpace();
            }
            emitRaw(t.text);
            break;
        }

        // ── Preprocessor directives — #define/#include आदि directives ────
        // Note: PP_Define जब has_src_=true हो तो switch से पहले ऊपर handle होता है।
        // जब has_src_=false हो, PP_Define इस case में fall through करता है।
        case TokenKind::PP_Define:
        case TokenKind::PP_Include:
        case TokenKind::PP_Undef:
        case TokenKind::PP_Ifdef:
        case TokenKind::PP_Ifndef:
        case TokenKind::PP_Ifaot:
        case TokenKind::PP_Ifjit:
        case TokenKind::PP_Endif:
        case TokenKind::PP_Else:
        case TokenKind::PP_Elif:
        case TokenKind::PP_Assert:
        case TokenKind::PP_If:
        case TokenKind::PP_Exe: {
            // PP directives हमेशा अपनी अलग line पर शुरू होते हैं
            if (!at_line_start_) emitNewline();
            emitRaw(std::string(t.text));
            emitSpace();
            break;
        }

        // ── Sizeof / Offset — compile-time size और offset operators ──────
        case TokenKind::Sizeof:
        case TokenKind::Offset:
        case TokenKind::Lastclass: {
            if (at_line_start_) emitIndent();
            else emitSpace();
            emitRaw(std::string(t.text));
            break;
        }

        // ── Other tokens: appropriate spacing के साथ as-is emit करो ──────
        default: {
            if (at_line_start_) {
                emitIndent();
            } else if (!out_.empty() && out_.back() != ' '
                       && out_.back() != '\n') {
                emitSpace();
            }
            emitRaw(t.text.empty() ? "?" : std::string(t.text));
            break;
        }

        } // switch — token kind के अनुसार formatting complete

        prev_kind_ = k;
        pos_++;
    }

    // Trailing newline ensure करो
    if (!out_.empty() && out_.back() != '\n')
        out_ += '\n';

    return out_;
}

} // namespace holyc
