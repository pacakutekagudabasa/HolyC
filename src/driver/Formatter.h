#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lexer/Token.h"

namespace holyc {

/**
 * @brief Token-stream based HolyC code formatter।
 *
 * Indentation, spacing, और brace style को Terry Davis के canonical HolyC style
 * से match करने के लिए normalize करता है:
 *   - 2-space indentation (दो spaces का indent)
 *   - Opening { statement के same line पर
 *   - Binary operators के around spaces
 *   - Function name और ( के बीच space नहीं
 *   - * type name से attached (variable से नहीं)
 *
 * Optional raw source string supply करने पर (setSource के through), formatter यह भी:
 *   - Statement lines पर trailing // comments preserve करता है
 *   - #define continuation lines (\ से end होने वाली lines) preserve करता है
 *   - Top-level declarations के बीच blank lines preserve करता है (एक में collapse)
 *   - Block comments के interior lines verbatim preserve करता है
 */
class Formatter {
public:
    explicit Formatter(const std::vector<Token>& tokens);

    /**
     * @brief Raw source text optionally provide करो ताकि formatter comments,
     * blank lines, और #define continuations handle कर सके।
     *
     * @param src Format होने वाली file का raw source text।
     */
    void setSource(const std::string& src);

    /**
     * @brief Token stream format करो और formatted source string return करो।
     *
     * @return Trailing newline के साथ formatted HolyC source text।
     */
    std::string format();

private:
    const std::vector<Token>& toks_;
    size_t pos_;
    std::string out_;
    int indent_;
    TokenKind prev_kind_;
    bool at_line_start_;
    int paren_depth_;   // ( ) nesting track करो — for(;;) के अंदर ; के लिए

    // Comment/blank-line/define-continuation recovery के लिए raw source
    std::string src_;
    bool has_src_ = false;

    // src_ से split source lines
    std::vector<std::string> src_lines_;

    // Per-token: वह source line index (0-based) जिस पर हर token start होता है
    // (has_src_ true होने पर populate होता है)
    std::vector<uint32_t> tok_lines_; // toks_ के parallel

    // वह source line number (0-based) जो blank-line insertion के लिए
    // last "consumed" हुई।
    uint32_t last_src_line_ = 0;

    // क्या हम #define continuation block के अंदर हैं
    bool in_define_continuation_ = false;

    const Token& tok(int offset = 0) const;
    bool atEnd() const { return pos_ >= toks_.size(); }
    TokenKind nextKind(int offset = 1) const;

    void emitRaw(std::string_view s);
    void emitNewline();
    void emitIndent();
    void emitSpace();

    bool isBinaryOp(TokenKind k) const;
    bool isTypeStart(TokenKind k) const;
    bool isValueEnd(TokenKind k) const; // जिन tokens के बाद next token value है

    // Source-aware helpers — raw source text use करने वाले helper functions
    void splitSourceLines();
    void buildTokLines();

    /**
     * @brief 0-based index पर raw source line लौटाओ, या out of range हो तो ""।
     *
     * @param idx src_lines_ में 0-based line index।
     * @return Line का view, या idx out of range हो तो empty।
     */
    std::string_view srcLine(uint32_t idx) const;

    /**
     * @brief अगर line_idx पर source line में trailing // comment है (strings के बाहर)
     * तो comment text ("//", सहित) लौटाओ, नहीं तो ""।
     *
     * @param line_idx 0-based source line index।
     * @return Trailing comment text, या कोई न हो तो empty string।
     */
    std::string trailingComment(uint32_t line_idx) const;

    /**
     * @brief last_src_line_ और before_line (exclusive) के बीच source में आने वाली
     * blank lines emit करो। Consecutive blanks को एक में collapse करो।
     *
     * @param before_line अगले process होने वाले token line का 0-based index।
     */
    void emitBlankLinesBefore(uint32_t before_line);

    /**
     * @brief last_src_line_ (exclusive) और before_line (exclusive) के बीच source lines में
     * मिलने वाले block comments verbatim emit करो।
     *
     * @param before_line अगले process होने वाले token line का 0-based index।
     */
    void emitBlockCommentsBefore(uint32_t before_line);
};

} // namespace holyc
