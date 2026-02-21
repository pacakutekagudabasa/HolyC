#pragma once

namespace holyc {

/**
 * @brief HolyC lexer के सभी token kinds — keywords, operators, और literals सहित।
 */
enum class TokenKind {
    // === Types === — सभी primitive type keywords
    U0, I0, U8, I8, U16, I16, U32, I32, U64, I64, F64, F32, Bool,

    // === Intrinsic union types === — ये built-in union keywords हैं
    U8i, I8i, U16i, I16i, U32i, I32i, U64i, I64i, F64i,

    // === Keywords === — सभी language keywords हैं
    If, Else, For, While, Do, Switch, Case, Default, Break, Continue,
    Return, Goto,
    Class, Union, Enum, Typedef, Sizeof, Typeof, Offset, Lastclass,
    Try, Catch, Throw,
    Asm, Exe,
    Extern, _Extern, _Intern, Import, _Import, Public, Static,
    Reg, NoReg, NoWarn,
    Interrupt, HasErrCode, ArgPop, NoArgPop,
    Lock,
    True, False,
    Auto,

    // === Preprocessor === — preprocessor directives के tokens
    PP_Ifdef, PP_Ifndef, PP_Ifaot, PP_Ifjit, PP_Endif, PP_Else, PP_Elif,
    PP_Define, PP_Undef, PP_Assert, PP_Include, PP_If, PP_Exe,
    PP_Hash,       // # macro body के अंदर (stringify operator)
    PP_HashHash,   // ## macro body के अंदर (token-paste operator)

    // === Literals === — literal value tokens (संख्या, string, character)
    IntLiteral, FloatLiteral, StringLiteral, CharLiteral,

    // === Single-char operators / punctuation === — एक-अक्षर के operators
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    Ampersand,  // &
    Pipe,       // |
    Caret,      // ^
    Tilde,      // ~
    Bang,       // !
    Less,       // <
    Greater,    // >
    Assign,     // =
    Dot,        // .
    Comma,      // ,
    Semicolon,  // ;
    Colon,      // :
    Question,   // ?
    LParen,     // (
    RParen,     // )
    LBrace,     // {
    RBrace,     // }
    LBracket,   // [
    RBracket,   // ]
    Backtick,   // ` (power operator — घात के लिए)

    // === Multi-char operators === — एक से ज़्यादा character के operators
    PlusPlus,       // ++
    MinusMinus,     // --
    PlusAssign,     // +=
    MinusAssign,    // -=
    StarAssign,     // *=
    SlashAssign,    // /=
    PercentAssign,  // %=
    AmpAssign,      // &=
    PipeAssign,     // |=
    CaretAssign,    // ^=
    ShlAssign,      // <<=
    ShrAssign,      // >>=
    PPAssign,       // ++=
    MMAssign,       // --=
    Shl,            // <<
    Shr,            // >>
    EqEq,           // ==
    BangEq,         // !=
    LessEq,         // <=
    GreaterEq,      // >=
    AmpAmp,         // &&
    PipePipe,       // ||
    CaretCaret,     // ^^
    Arrow,          // ->
    DotDot,         // ..
    DoubleColon,    // ::

    // === Special === — विशेष tokens
    Identifier,
    Eof,
    Error,
};

/**
 * @brief TokenKind की human-readable spelling लौटाओ (जैसे Plus के लिए "+")।
 *
 * @param kind Convert करने वाला token kind।
 * @return Kind की null-terminated string spelling।
 */
inline const char* tokenKindToString(TokenKind kind) {
    switch (kind) {
    // Types — primitive type keywords की list
    case TokenKind::U0:         return "U0";
    case TokenKind::I0:         return "I0";
    case TokenKind::U8:         return "U8";
    case TokenKind::I8:         return "I8";
    case TokenKind::U16:        return "U16";
    case TokenKind::I16:        return "I16";
    case TokenKind::U32:        return "U32";
    case TokenKind::I32:        return "I32";
    case TokenKind::U64:        return "U64";
    case TokenKind::I64:        return "I64";
    case TokenKind::F64:        return "F64";
    case TokenKind::F32:        return "F32";
    case TokenKind::Bool:       return "Bool";

    // Intrinsic union types — built-in union keywords की list
    case TokenKind::U8i:        return "U8i";
    case TokenKind::I8i:        return "I8i";
    case TokenKind::U16i:       return "U16i";
    case TokenKind::I16i:       return "I16i";
    case TokenKind::U32i:       return "U32i";
    case TokenKind::I32i:       return "I32i";
    case TokenKind::U64i:       return "U64i";
    case TokenKind::I64i:       return "I64i";
    case TokenKind::F64i:       return "F64i";

    // Keywords — सभी language keywords की list
    case TokenKind::If:         return "If";
    case TokenKind::Else:       return "Else";
    case TokenKind::For:        return "For";
    case TokenKind::While:      return "While";
    case TokenKind::Do:         return "Do";
    case TokenKind::Switch:     return "Switch";
    case TokenKind::Case:       return "Case";
    case TokenKind::Default:    return "Default";
    case TokenKind::Break:      return "Break";
    case TokenKind::Continue:   return "Continue";
    case TokenKind::Return:     return "Return";
    case TokenKind::Goto:       return "Goto";
    case TokenKind::Class:      return "Class";
    case TokenKind::Union:      return "Union";
    case TokenKind::Enum:       return "enum";
    case TokenKind::Typedef:    return "Typedef";
    case TokenKind::Sizeof:     return "Sizeof";
    case TokenKind::Typeof:     return "Typeof";
    case TokenKind::Offset:     return "Offset";
    case TokenKind::Lastclass:  return "Lastclass";
    case TokenKind::Try:        return "Try";
    case TokenKind::Catch:      return "Catch";
    case TokenKind::Throw:      return "Throw";
    case TokenKind::Asm:        return "Asm";
    case TokenKind::Exe:        return "Exe";
    case TokenKind::Extern:     return "Extern";
    case TokenKind::_Extern:    return "_Extern";
    case TokenKind::_Intern:    return "_Intern";
    case TokenKind::Import:     return "Import";
    case TokenKind::_Import:    return "_Import";
    case TokenKind::Public:     return "Public";
    case TokenKind::Static:     return "Static";
    case TokenKind::Reg:        return "Reg";
    case TokenKind::NoReg:      return "NoReg";
    case TokenKind::NoWarn:     return "NoWarn";
    case TokenKind::Interrupt:  return "Interrupt";
    case TokenKind::HasErrCode: return "HasErrCode";
    case TokenKind::ArgPop:     return "ArgPop";
    case TokenKind::NoArgPop:   return "NoArgPop";
    case TokenKind::Lock:       return "Lock";
    case TokenKind::True:       return "True";
    case TokenKind::False:      return "False";
    case TokenKind::Auto:       return "Auto";

    // Preprocessor — preprocessor directive tokens की list
    case TokenKind::PP_Ifdef:   return "#ifdef";
    case TokenKind::PP_Ifndef:  return "#ifndef";
    case TokenKind::PP_Ifaot:   return "#ifaot";
    case TokenKind::PP_Ifjit:   return "#ifjit";
    case TokenKind::PP_Endif:   return "#endif";
    case TokenKind::PP_Else:    return "#else";
    case TokenKind::PP_Elif:    return "#elif";
    case TokenKind::PP_Define:  return "#define";
    case TokenKind::PP_Undef:   return "#undef";
    case TokenKind::PP_Assert:  return "#assert";
    case TokenKind::PP_Include: return "#include";
    case TokenKind::PP_If:      return "#if";
    case TokenKind::PP_Exe:      return "#exe";
    case TokenKind::PP_Hash:     return "#";
    case TokenKind::PP_HashHash: return "##";

    // Literals — literal value tokens की list
    case TokenKind::IntLiteral:    return "IntLiteral";
    case TokenKind::FloatLiteral:  return "FloatLiteral";
    case TokenKind::StringLiteral: return "StringLiteral";
    case TokenKind::CharLiteral:   return "CharLiteral";

    // Single-char operators — एक character के operators
    case TokenKind::Plus:       return "+";
    case TokenKind::Minus:      return "-";
    case TokenKind::Star:       return "*";
    case TokenKind::Slash:      return "/";
    case TokenKind::Percent:    return "%";
    case TokenKind::Ampersand:  return "&";
    case TokenKind::Pipe:       return "|";
    case TokenKind::Caret:      return "^";
    case TokenKind::Tilde:      return "~";
    case TokenKind::Bang:       return "!";
    case TokenKind::Less:       return "<";
    case TokenKind::Greater:    return ">";
    case TokenKind::Assign:     return "=";
    case TokenKind::Dot:        return ".";
    case TokenKind::Comma:      return ",";
    case TokenKind::Semicolon:  return ";";
    case TokenKind::Colon:      return ":";
    case TokenKind::Question:   return "?";
    case TokenKind::LParen:     return "(";
    case TokenKind::RParen:     return ")";
    case TokenKind::LBrace:     return "{";
    case TokenKind::RBrace:     return "}";
    case TokenKind::LBracket:   return "[";
    case TokenKind::RBracket:   return "]";
    case TokenKind::Backtick:   return "`";

    // Multi-char operators — एक से ज़्यादा character के operators
    case TokenKind::PlusPlus:      return "++";
    case TokenKind::MinusMinus:    return "--";
    case TokenKind::PlusAssign:    return "+=";
    case TokenKind::MinusAssign:   return "-=";
    case TokenKind::StarAssign:    return "*=";
    case TokenKind::SlashAssign:   return "/=";
    case TokenKind::PercentAssign: return "%=";
    case TokenKind::AmpAssign:     return "&=";
    case TokenKind::PipeAssign:    return "|=";
    case TokenKind::CaretAssign:   return "^=";
    case TokenKind::ShlAssign:     return "<<=";
    case TokenKind::ShrAssign:     return ">>=";
    case TokenKind::PPAssign:      return "++=";
    case TokenKind::MMAssign:      return "--=";
    case TokenKind::Shl:           return "<<";
    case TokenKind::Shr:           return ">>";
    case TokenKind::EqEq:          return "==";
    case TokenKind::BangEq:        return "!=";
    case TokenKind::LessEq:        return "<=";
    case TokenKind::GreaterEq:     return ">=";
    case TokenKind::AmpAmp:        return "&&";
    case TokenKind::PipePipe:      return "||";
    case TokenKind::CaretCaret:    return "^^";
    case TokenKind::Arrow:         return "->";
    case TokenKind::DotDot:        return "..";
    case TokenKind::DoubleColon:   return "::";

    // Special — विशेष tokens
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::Eof:        return "Eof";
    case TokenKind::Error:      return "Error";
    }
    return "<unknown>";
}

} // namespace holyc
