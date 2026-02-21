#pragma once

#include "TokenKind.h"
#include "../support/SourceLocation.h"

#include <cstdint>
#include <string_view>

namespace holyc {

/**
 * @brief Lexer token जिसमें kind, source text, location, और optional numeric value हो।
 */
struct Token {
    TokenKind kind = TokenKind::Error; //!< इस token की category।
    std::string_view text;             //!< Source buffer में non-owning view।
    SourceLocation loc;                //!< Source में token कहाँ से शुरू होता है।

    union {
        int64_t intVal;
        uint64_t uintVal;
        double floatVal;
    };

    Token() : intVal(0) {}
    Token(TokenKind kind, std::string_view text, SourceLocation loc)
        : kind(kind), text(text), loc(loc), intVal(0) {}

    /**
     * @brief अगर यह token language keyword है तो true लौटाओ।
     *
     * @return True जब kind [If, Auto] range में हो।
     */
    bool isKeyword() const {
        return kind >= TokenKind::If && kind <= TokenKind::Auto;
    }

    /**
     * @brief अगर यह token primitive type keyword (U0..F64i) है तो true लौटाओ।
     *
     * @return True जब kind [U0, F64i] range में हो।
     */
    bool isType() const {
        return kind >= TokenKind::U0 && kind <= TokenKind::F64i;
    }

    /**
     * @brief अगर यह token operator या punctuation symbol है तो true लौटाओ।
     *
     * @return True जब kind [Plus, DoubleColon] range में हो।
     */
    bool isOperator() const {
        return kind >= TokenKind::Plus && kind <= TokenKind::DoubleColon;
    }

    /**
     * @brief अगर यह token integer, float, string, या char literal है तो true लौटाओ।
     *
     * @return True जब kind [IntLiteral, CharLiteral] range में हो।
     */
    bool isLiteral() const {
        return kind >= TokenKind::IntLiteral && kind <= TokenKind::CharLiteral;
    }

    bool is(TokenKind k) const { return kind == k; }    //!< kind == k हो तो true।
    bool isNot(TokenKind k) const { return kind != k; } //!< kind != k हो तो true।

    /**
     * @brief इस token के kind का human-readable नाम लौटाओ।
     *
     * @return C-string spelling जैसे "+" या "Identifier"।
     */
    const char* kindName() const { return tokenKindToString(kind); }
};

} // namespace holyc
