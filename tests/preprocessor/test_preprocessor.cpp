#include "support/SourceManager.h"
#include "support/Diagnostics.h"
#include "preprocessor/Preprocessor.h"
#include "lexer/TokenKind.h"
#include "lexer/Token.h"

#include <cstdio>
#include <vector>
#include <string>
#include <cmath>

using namespace holyc;

static int total = 0, passed = 0;

#define CHECK(cond, msg) do { \
    total++; \
    if (cond) { passed++; std::printf("  PASS: %s\n", msg); } \
    else { std::printf("  FAIL: %s\n", msg); } \
} while(0)

static std::vector<Token> preprocess(const std::string& src) {
    SourceManager sm;
    Diagnostics diag(&sm);
    int fid = sm.loadString("<test>", src);
    Preprocessor pp(sm, diag, fid);
    std::vector<Token> tokens;
    while (true) {
        Token t = pp.next();
        tokens.push_back(t);
        if (t.kind == TokenKind::Eof) break;
    }
    return tokens;
}

static void test_object_macro() {
    std::printf("[test_object_macro]\n");
    auto tokens = preprocess("#define FOO 42\nFOO");
    // IntLiteral(42) और फिर Eof मिलना चाहिए
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].kind == TokenKind::IntLiteral, "expanded to IntLiteral");
    CHECK(tokens[0].intVal == 42, "value == 42");
}

static void test_undef() {
    std::printf("[test_undef]\n");
    auto tokens = preprocess("#define FOO 42\n#undef FOO\nFOO");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].kind == TokenKind::Identifier, "FOO is now Identifier (undefined)");
}

static void test_ifdef_true() {
    std::printf("[test_ifdef_true]\n");
    auto tokens = preprocess("#define X\n#ifdef X\n10\n#endif");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].kind == TokenKind::IntLiteral, "10 is included");
}

static void test_ifdef_false() {
    std::printf("[test_ifdef_false]\n");
    auto tokens = preprocess("#ifdef UNDEF\n10\n#endif");
    CHECK(tokens.size() == 1, "token count == 1 (only Eof)");
    CHECK(tokens[0].kind == TokenKind::Eof, "only Eof");
}

static void test_ifndef() {
    std::printf("[test_ifndef]\n");
    auto tokens = preprocess("#ifndef UNDEF\n20\n#endif");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].kind == TokenKind::IntLiteral, "20 is included");
}

static void test_ifdef_else() {
    std::printf("[test_ifdef_else]\n");
    auto tokens = preprocess("#ifdef UNDEF\n10\n#else\n20\n#endif");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].kind == TokenKind::IntLiteral, "got IntLiteral");
    CHECK(tokens[0].intVal == 20, "value == 20 (else branch)");
}

static void test_if_const_expr() {
    std::printf("[test_if_const_expr]\n");
    auto tokens = preprocess("#if 1 + 1 == 2\n99\n#endif");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].intVal == 99, "value == 99");
}

static void test_if_defined() {
    std::printf("[test_if_defined]\n");
    auto tokens = preprocess("#define X\n#if defined(X)\n77\n#endif");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].intVal == 77, "value == 77");
}

static void test_ifaot_ifjit() {
    std::printf("[test_ifaot_ifjit]\n");
    // Default JIT mode है (AOT नहीं), इसलिए #ifjit active होना चाहिए
    auto tokens = preprocess("#ifjit\n55\n#endif");
    CHECK(tokens.size() == 2, "ifjit active: count == 2");
    CHECK(tokens[0].intVal == 55, "value == 55");

    auto tokens2 = preprocess("#ifaot\n66\n#endif");
    CHECK(tokens2.size() == 1, "ifaot skipped: count == 1");
}

static void test_nested_ifdef() {
    std::printf("[test_nested_ifdef]\n");
    auto tokens = preprocess("#define A\n#ifdef A\n#ifdef B\n10\n#else\n20\n#endif\n#endif");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].intVal == 20, "value == 20 (inner else)");
}

static void test_predefined_line() {
    std::printf("[test_predefined_line]\n");
    auto tokens = preprocess("__LINE__");
    CHECK(tokens.size() == 2, "token count == 2");
    CHECK(tokens[0].kind == TokenKind::IntLiteral, "__LINE__ is IntLiteral");
}

static void test_function_macro() {
    std::printf("[test_function_macro]\n");
    auto tokens = preprocess("#define ADD(a,b) a + b\nADD(3, 4)");
    // 3 + 4 में expand होना चाहिए
    CHECK(tokens.size() >= 4, "at least 4 tokens (3 + 4 Eof)");
    CHECK(tokens[0].kind == TokenKind::IntLiteral, "first is IntLiteral");
    CHECK(tokens[1].kind == TokenKind::Plus, "second is Plus");
    CHECK(tokens[2].kind == TokenKind::IntLiteral, "third is IntLiteral");
}

static void test_multi_token_define() {
    std::printf("[test_multi_token_define]\n");
    auto tokens = preprocess("#define HELLO 1 + 2\nHELLO");
    CHECK(tokens.size() >= 4, "token count >= 4 (1 + 2 Eof)");
    if (tokens.size() >= 4) {
        CHECK(tokens[0].intVal == 1, "first is 1");
        CHECK(tokens[1].kind == TokenKind::Plus, "second is +");
        CHECK(tokens[2].intVal == 2, "third is 2");
    }
}

int main() {
    std::printf("=== HolyC Preprocessor Tests ===\n\n");

    test_object_macro();
    test_undef();
    test_ifdef_true();
    test_ifdef_false();
    test_ifndef();
    test_ifdef_else();
    test_if_const_expr();
    test_if_defined();
    test_ifaot_ifjit();
    test_nested_ifdef();
    test_predefined_line();
    test_function_macro();
    test_multi_token_define();

    std::printf("\n%d/%d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
