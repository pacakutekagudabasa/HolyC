#include "../../src/lexer/Lexer.h"
#include "../../src/support/SourceManager.h"
#include "../../src/support/Diagnostics.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace holyc;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            ++g_passed;                                                        \
            std::cout << "  PASS: " << (msg) << "\n";                          \
        } else {                                                               \
            ++g_failed;                                                        \
            std::cout << "  FAIL: " << (msg) << "\n";                          \
        }                                                                      \
    } while (0)

static std::vector<Token> tokenize(const std::string& source) {
    SourceManager sm;
    int fid = sm.loadString("<test>", source);
    Diagnostics diag(&sm);
    Lexer lex(sm, fid, diag);

    std::vector<Token> tokens;
    while (true) {
        Token t = lex.next();
        if (t.kind == TokenKind::Eof)
            break;
        tokens.push_back(t);
    }
    return tokens;
}

static void test_keywords() {
    std::cout << "[test_keywords]\n";
    auto toks = tokenize("if else for while do switch return");
    CHECK(toks.size() == 7, "keyword count == 7");
    CHECK(toks[0].kind == TokenKind::If,     "if");
    CHECK(toks[1].kind == TokenKind::Else,   "else");
    CHECK(toks[2].kind == TokenKind::For,    "for");
    CHECK(toks[3].kind == TokenKind::While,  "while");
    CHECK(toks[4].kind == TokenKind::Do,     "do");
    CHECK(toks[5].kind == TokenKind::Switch, "switch");
    CHECK(toks[6].kind == TokenKind::Return, "return");
}

static void test_types() {
    std::cout << "[test_types]\n";
    auto toks = tokenize("U0 I8 U8 I16 U16 I32 U32 I64 U64 F64 Bool");
    CHECK(toks.size() == 11, "type count == 11");
    CHECK(toks[0].kind  == TokenKind::U0,   "U0");
    CHECK(toks[1].kind  == TokenKind::I8,   "I8");
    CHECK(toks[2].kind  == TokenKind::U8,   "U8");
    CHECK(toks[3].kind  == TokenKind::I16,  "I16");
    CHECK(toks[4].kind  == TokenKind::U16,  "U16");
    CHECK(toks[5].kind  == TokenKind::I32,  "I32");
    CHECK(toks[6].kind  == TokenKind::U32,  "U32");
    CHECK(toks[7].kind  == TokenKind::I64,  "I64");
    CHECK(toks[8].kind  == TokenKind::U64,  "U64");
    CHECK(toks[9].kind  == TokenKind::F64,  "F64");
    CHECK(toks[10].kind == TokenKind::Bool, "Bool");
}

static void test_int_literals() {
    std::cout << "[test_int_literals]\n";
    auto toks = tokenize("42 0xFF 0b1010 0755");
    CHECK(toks.size() == 4, "int literal count == 4");
    for (size_t i = 0; i < toks.size(); ++i)
        CHECK(toks[i].kind == TokenKind::IntLiteral,
              std::string("token ") + std::to_string(i) + " is IntLiteral");
    CHECK(toks[0].intVal == 42,  "42 value");
    CHECK(toks[1].intVal == 255, "0xFF value");
    CHECK(toks[2].intVal == 10,  "0b1010 value");
    CHECK(toks[3].intVal == 493, "0755 value");
}

static void test_float_literals() {
    std::cout << "[test_float_literals]\n";
    auto toks = tokenize("3.14 1.5e2 .5");
    CHECK(toks.size() == 3, "float literal count == 3");
    for (size_t i = 0; i < toks.size(); ++i)
        CHECK(toks[i].kind == TokenKind::FloatLiteral,
              std::string("token ") + std::to_string(i) + " is FloatLiteral");
    CHECK(std::abs(toks[0].floatVal - 3.14) < 1e-9,  "3.14 value");
    CHECK(std::abs(toks[1].floatVal - 150.0) < 1e-9, "1.5e2 value");
    CHECK(std::abs(toks[2].floatVal - 0.5) < 1e-9,   ".5 value");
}

static void test_string_literal() {
    std::cout << "[test_string_literal]\n";
    auto toks = tokenize("\"hello\\nworld\"");
    CHECK(toks.size() == 1, "string literal count == 1");
    CHECK(toks[0].kind == TokenKind::StringLiteral, "is StringLiteral");
}

static void test_char_literals() {
    std::cout << "[test_char_literals]\n";
    auto toks = tokenize("'A' 'AB' '\\n'");
    CHECK(toks.size() == 3, "char literal count == 3");
    for (size_t i = 0; i < toks.size(); ++i)
        CHECK(toks[i].kind == TokenKind::CharLiteral,
              std::string("token ") + std::to_string(i) + " is CharLiteral");
    CHECK(toks[0].intVal == 65,     "'A' value == 65");
    CHECK(toks[1].intVal == 0x4241, "'AB' value == 0x4241");
    CHECK(toks[2].intVal == 10,     "'\\n' value == 10");
}

static void test_operators() {
    std::cout << "[test_operators]\n";
    auto toks = tokenize("+ - * / ++ -- += -= == != <= >= && || ^^ ->");
    CHECK(toks.size() == 16, "operator count == 16");
    CHECK(toks[0].kind  == TokenKind::Plus,        "+");
    CHECK(toks[1].kind  == TokenKind::Minus,       "-");
    CHECK(toks[2].kind  == TokenKind::Star,        "*");
    CHECK(toks[3].kind  == TokenKind::Slash,       "/");
    CHECK(toks[4].kind  == TokenKind::PlusPlus,    "++");
    CHECK(toks[5].kind  == TokenKind::MinusMinus,  "--");
    CHECK(toks[6].kind  == TokenKind::PlusAssign,  "+=");
    CHECK(toks[7].kind  == TokenKind::MinusAssign, "-=");
    CHECK(toks[8].kind  == TokenKind::EqEq,        "==");
    CHECK(toks[9].kind  == TokenKind::BangEq,      "!=");
    CHECK(toks[10].kind == TokenKind::LessEq,      "<=");
    CHECK(toks[11].kind == TokenKind::GreaterEq,   ">=");
    CHECK(toks[12].kind == TokenKind::AmpAmp,      "&&");
    CHECK(toks[13].kind == TokenKind::PipePipe,    "||");
    CHECK(toks[14].kind == TokenKind::CaretCaret,  "^^");
    CHECK(toks[15].kind == TokenKind::Arrow,       "->");
}

static void test_special_operators() {
    std::cout << "[test_special_operators]\n";
    auto toks = tokenize("` ++= --= .. ::");
    CHECK(toks.size() == 5, "special operator count == 5");
    CHECK(toks[0].kind == TokenKind::Backtick,    "`");
    CHECK(toks[1].kind == TokenKind::PPAssign,    "++=");
    CHECK(toks[2].kind == TokenKind::MMAssign,    "--=");
    CHECK(toks[3].kind == TokenKind::DotDot,      "..");
    CHECK(toks[4].kind == TokenKind::DoubleColon, "::");
}

static void test_preprocessor() {
    std::cout << "[test_preprocessor]\n";
    auto toks = tokenize("#include #define #ifdef #ifndef #endif #ifaot #ifjit");
    CHECK(toks.size() == 7, "preprocessor count == 7");
    CHECK(toks[0].kind == TokenKind::PP_Include, "#include");
    CHECK(toks[1].kind == TokenKind::PP_Define,  "#define");
    CHECK(toks[2].kind == TokenKind::PP_Ifdef,   "#ifdef");
    CHECK(toks[3].kind == TokenKind::PP_Ifndef,  "#ifndef");
    CHECK(toks[4].kind == TokenKind::PP_Endif,   "#endif");
    CHECK(toks[5].kind == TokenKind::PP_Ifaot,   "#ifaot");
    CHECK(toks[6].kind == TokenKind::PP_Ifjit,   "#ifjit");
}

static void test_comments() {
    std::cout << "[test_comments]\n";
    auto toks = tokenize("42 // comment\n 43 /* block */ 44");
    CHECK(toks.size() == 3, "comment skipping: token count == 3");
    CHECK(toks[0].kind == TokenKind::IntLiteral && toks[0].intVal == 42,
          "first token is 42");
    CHECK(toks[1].kind == TokenKind::IntLiteral && toks[1].intVal == 43,
          "second token is 43");
    CHECK(toks[2].kind == TokenKind::IntLiteral && toks[2].intVal == 44,
          "third token is 44");
}

static void test_line_tracking() {
    std::cout << "[test_line_tracking]\n";
    auto toks = tokenize("a\nb\nc");
    CHECK(toks.size() == 3, "line tracking: token count == 3");
    CHECK(toks[0].loc.line == 1, "token 'a' on line 1");
    CHECK(toks[1].loc.line == 2, "token 'b' on line 2");
    CHECK(toks[2].loc.line == 3, "token 'c' on line 3");
}

static void test_mixed_program() {
    std::cout << "[test_mixed_program]\n";
    auto toks = tokenize("I64 x = 42;\nif (x > 0) { Print(\"yes\"); }");
    // Expected output: I64 x = 42 ; if ( x > 0 ) { Print ( "yes" ) ; } — यही tokens आने चाहिए
    CHECK(toks.size() == 18, "mixed program token count == 18");
    CHECK(toks[0].kind  == TokenKind::I64,           "I64");
    CHECK(toks[1].kind  == TokenKind::Identifier,    "x");
    CHECK(toks[2].kind  == TokenKind::Assign,        "=");
    CHECK(toks[3].kind  == TokenKind::IntLiteral,    "42");
    CHECK(toks[4].kind  == TokenKind::Semicolon,     ";");
    CHECK(toks[5].kind  == TokenKind::If,            "if");
    CHECK(toks[6].kind  == TokenKind::LParen,        "(");
    CHECK(toks[7].kind  == TokenKind::Identifier,    "x (2nd)");
    CHECK(toks[8].kind  == TokenKind::Greater,       ">");
    CHECK(toks[9].kind  == TokenKind::IntLiteral,    "0");
    CHECK(toks[10].kind == TokenKind::RParen,        ")");
    CHECK(toks[11].kind == TokenKind::LBrace,       "{");
    CHECK(toks[12].kind == TokenKind::Identifier,    "Print");
    CHECK(toks[13].kind == TokenKind::LParen,        "( for Print");
    CHECK(toks[14].kind == TokenKind::StringLiteral, "\"yes\"");
    CHECK(toks[15].kind == TokenKind::RParen,        ") for Print");
    CHECK(toks[16].kind == TokenKind::Semicolon,     "; after Print");
    CHECK(toks[17].kind == TokenKind::RBrace,        "}");
}

int main() {
    std::cout << "=== HolyC Lexer Tests ===\n\n";

    test_keywords();
    test_types();
    test_int_literals();
    test_float_literals();
    test_string_literal();
    test_char_literals();
    test_operators();
    test_special_operators();
    test_preprocessor();
    test_comments();
    test_line_tracking();
    test_mixed_program();

    int total = g_passed + g_failed;
    std::cout << "\n" << g_passed << "/" << total << " tests passed\n";

    return g_failed > 0 ? 1 : 0;
}
