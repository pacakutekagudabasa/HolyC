#pragma once

#include "Token.h"
#include "../support/SourceLocation.h"

#include <optional>
#include <string>

namespace holyc {

class SourceManager;
class Diagnostics;

/**
 * @brief एक source buffer को tokenise करता है।
 */
class Lexer {
public:
    /**
     * @brief दिए गए file ID वाले buffer पर lexer बनाओ।
     *
     * @param sm     Buffer का मालिक source manager।
     * @param fileId जिस file/buffer को lex करना है उसका identifier।
     * @param diag   Errors और warnings के लिए diagnostics sink।
     */
    Lexer(SourceManager& sm, int fileId, Diagnostics& diag);

    /**
     * @brief अगला token consume करके वापस दो।
     *
     * @return Source buffer से अगला token।
     */
    Token next();

    /**
     * @brief अगला token बिना consume किए वापस दो।
     *
     * @return Source buffer से अगला token, position नहीं बदलेगी।
     */
    Token peek();
    const SourceManager& sourceManager() const { return sm_; }

private:
    char current() const;

    /**
     * @brief Current position से आगे कितने bytes पर character है वो लौटाओ।
     *
     * @param ahead Current position से byte offset (default 1)।
     * @return उस position का character, या buffer खत्म हो तो '\\0'।
     */
    char peekChar(int ahead = 1) const;

    /**
     * @brief Current character consume करके position आगे बढ़ाओ।
     *
     * @return Advance करने से पहले current position का character।
     */
    char advance();
    bool atEnd() const;
    SourceLocation location() const;

    void skipWhitespaceAndComments();
    void skipLineComment();

    /**
     * @brief Block comment skip करो, nesting पर warning दो।
     */
    void skipBlockComment();

    /**
     * @brief Identifier या keyword token lex करो।
     *
     * @return Identifier token, या reserved word हो तो उचित keyword token।
     */
    Token lexIdentifierOrKeyword();

    /**
     * @brief Integer या floating-point literal lex करो (hex, octal, binary, या decimal)।
     *
     * @return IntLiteral या FloatLiteral token जिसमें parse की गई numeric value हो।
     */
    Token lexNumber();
    Token lexString();

    /**
     * @brief Character literal lex करो; multi-byte (max 8 bytes) forms support करता है।
     *
     * @return CharLiteral token जिसमें packed byte value हो।
     */
    Token lexChar();

    /**
     * @brief '#' से शुरू होने वाला preprocessor directive token lex करो।
     *
     * @return उचित PP_* token kind, या unknown directive हो तो PP_Hash।
     */
    Token lexPreprocessor();

    /**
     * @brief दिए गए start offset से current position तक का token बनाओ।
     *
     * @param kind        नए token को assign करने वाला kind।
     * @param startOffset Buffer में वो byte offset जहाँ यह token शुरू हुआ।
     * @return बना हुआ token।
     */
    Token makeToken(TokenKind kind, size_t startOffset) const;

    /**
     * @brief Diagnostic emit करो और दिए गए start offset से Error token लौटाओ।
     *
     * @param startOffset Buffer में वो byte offset जहाँ error शुरू हुई।
     * @param msg         Emit करने वाला error message।
     * @return Error token जो गलत source range cover करे।
     */
    Token makeError(size_t startOffset, const std::string& msg);

    /**
     * @brief Backslash escape sequence consume करके decoded character value लौटाओ।
     *
     * @return Decoded character, या unrecognised hex escape हो तो '?'।
     */
    char scanEscape();

    SourceManager& sm_;
    Diagnostics& diag_;
    int fileId_;
    const std::string& buf_;
    size_t pos_;
};

} // namespace holyc
