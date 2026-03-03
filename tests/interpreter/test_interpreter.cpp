#include "support/SourceManager.h"
#include "support/Diagnostics.h"
#include "support/Arena.h"
#include "preprocessor/Preprocessor.h"
#include "parser/Parser.h"
#include "sema/Sema.h"
#include "interpreter/Interpreter.h"
#include "interpreter/Value.h"
#include "ast/AST.h"
#include "ast/TranslationUnit.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include <unistd.h>

using namespace holyc;

static int total = 0, passed = 0;

#define CHECK(cond, msg) do { \
    total++; \
    if (cond) { passed++; std::printf("  PASS: %s\n", msg); } \
    else { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

// --------------------------------------------------------------------------
// Helper: HolyC source को full pipeline से चलाओ, stdout capture करो
// --------------------------------------------------------------------------

struct RunResult {
    int exit_code;
    std::string output;
};

static RunResult run_holyc(const std::string& src) {
    // stdout capture करो
    fflush(stdout);
    char tmpname[] = "/tmp/holyc_test_XXXXXX";
    int tmpfd = mkstemp(tmpname);
    if (tmpfd < 0) return {-1, ""};

    FILE* tmpf = fdopen(tmpfd, "w");
    if (!tmpf) { close(tmpfd); return {-1, ""}; }

    // stdout redirect करो
    int saved_stdout = dup(fileno(stdout));
    dup2(fileno(tmpf), fileno(stdout));

    int exit_code = 0;
    {
        SourceManager sm;
        Diagnostics diag(&sm);
        Arena arena;

        int fid = sm.loadString("<test>", src);
        Preprocessor pp(sm, diag, fid);
        Parser parser(pp, diag, arena);
        TranslationUnit* tu = parser.parse();

        Sema sema(diag, arena);
        sema.analyze(tu);

        Interpreter interp(diag);
        exit_code = interp.run(tu);
    }

    // stdout restore करो
    fflush(stdout);
    dup2(saved_stdout, fileno(stdout));
    close(saved_stdout);
    fclose(tmpf);

    // captured output पढ़ो
    FILE* rf = fopen(tmpname, "r");
    std::string output;
    if (rf) {
        char buf[4096];
        while (size_t n = fread(buf, 1, sizeof(buf), rf))
            output.append(buf, n);
        fclose(rf);
    }
    unlink(tmpname);

    return {exit_code, output};
}

// ==========================================================================
// Test cases — सभी test functions यहाँ हैं
// ==========================================================================

static void test_hello_world() {
    std::printf("[test_hello_world]\n");
    auto r = run_holyc("\"Hello World\\n\";");
    CHECK(r.output == "Hello World\n", "hello world output");
    CHECK(r.exit_code == 0, "exit code 0");
}

static void test_print_builtin() {
    std::printf("[test_print_builtin]\n");
    auto r = run_holyc("Print(\"Hello Print\\n\");");
    CHECK(r.output == "Hello Print\n", "Print builtin output");
}

static void test_var_decl_i64() {
    std::printf("[test_var_decl_i64]\n");
    auto r = run_holyc("I64 x = 42;\n\"%d\\n\", x;");
    CHECK(r.output == "42\n", "I64 variable declaration");
}

static void test_var_decl_u8() {
    std::printf("[test_var_decl_u8]\n");
    auto r = run_holyc("U8 c = 65;\n\"%c\\n\", c;");
    CHECK(r.output == "A\n", "U8 variable as char");
}

static void test_var_decl_f64() {
    std::printf("[test_var_decl_f64]\n");
    auto r = run_holyc("F64 x = 3.14;\n\"%.2f\\n\", x;");
    CHECK(r.output == "3.14\n", "F64 variable declaration");
}

static void test_var_decl_bool() {
    std::printf("[test_var_decl_bool]\n");
    auto r = run_holyc("Bool b = TRUE;\n\"%d\\n\", b;");
    CHECK(r.output == "1\n", "Bool variable declaration");
}

static void test_arithmetic_add() {
    std::printf("[test_arithmetic_add]\n");
    auto r = run_holyc("I64 x = 10 + 32;\n\"%d\\n\", x;");
    CHECK(r.output == "42\n", "addition");
}

static void test_arithmetic_sub() {
    std::printf("[test_arithmetic_sub]\n");
    auto r = run_holyc("I64 x = 50 - 8;\n\"%d\\n\", x;");
    CHECK(r.output == "42\n", "subtraction");
}

static void test_arithmetic_mul() {
    std::printf("[test_arithmetic_mul]\n");
    auto r = run_holyc("I64 x = 6 * 7;\n\"%d\\n\", x;");
    CHECK(r.output == "42\n", "multiplication");
}

static void test_arithmetic_div() {
    std::printf("[test_arithmetic_div]\n");
    auto r = run_holyc("I64 x = 84 / 2;\n\"%d\\n\", x;");
    CHECK(r.output == "42\n", "division");
}

static void test_arithmetic_mod() {
    std::printf("[test_arithmetic_mod]\n");
    auto r = run_holyc("I64 x = 47 % 5;\n\"%d\\n\", x;");
    CHECK(r.output == "2\n", "modulus");
}

static void test_comparison_eq() {
    std::printf("[test_comparison_eq]\n");
    auto r = run_holyc("I64 a = 5;\nif (a == 5) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "equality");
}

static void test_comparison_ne() {
    std::printf("[test_comparison_ne]\n");
    auto r = run_holyc("I64 a = 5;\nif (a != 3) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "not equal");
}

static void test_comparison_lt() {
    std::printf("[test_comparison_lt]\n");
    auto r = run_holyc("I64 a = 3;\nif (a < 5) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "less than");
}

static void test_comparison_gt() {
    std::printf("[test_comparison_gt]\n");
    auto r = run_holyc("I64 a = 10;\nif (a > 5) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "greater than");
}

static void test_comparison_le() {
    std::printf("[test_comparison_le]\n");
    auto r = run_holyc("I64 a = 5;\nif (a <= 5) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "less or equal");
}

static void test_comparison_ge() {
    std::printf("[test_comparison_ge]\n");
    auto r = run_holyc("I64 a = 5;\nif (a >= 5) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "greater or equal");
}

static void test_logical_and() {
    std::printf("[test_logical_and]\n");
    auto r = run_holyc("if (1 && 1) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "logical AND true");
    auto r2 = run_holyc("if (1 && 0) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r2.output == "0\n", "logical AND false");
}

static void test_logical_or() {
    std::printf("[test_logical_or]\n");
    auto r = run_holyc("if (0 || 1) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "logical OR true");
    auto r2 = run_holyc("if (0 || 0) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r2.output == "0\n", "logical OR false");
}

static void test_logical_not() {
    std::printf("[test_logical_not]\n");
    auto r = run_holyc("if (!0) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r.output == "1\n", "logical NOT of 0");
    auto r2 = run_holyc("if (!1) \"%d\\n\", 1; else \"%d\\n\", 0;");
    CHECK(r2.output == "0\n", "logical NOT of 1");
}

static void test_bitwise_and() {
    std::printf("[test_bitwise_and]\n");
    auto r = run_holyc("I64 x = 0xFF & 0x0F;\n\"%d\\n\", x;");
    CHECK(r.output == "15\n", "bitwise AND");
}

static void test_bitwise_or() {
    std::printf("[test_bitwise_or]\n");
    auto r = run_holyc("I64 x = 0xF0 | 0x0F;\n\"%d\\n\", x;");
    CHECK(r.output == "255\n", "bitwise OR");
}

static void test_bitwise_xor() {
    std::printf("[test_bitwise_xor]\n");
    auto r = run_holyc("I64 x = 0xFF ^ 0x0F;\n\"%d\\n\", x;");
    CHECK(r.output == "240\n", "bitwise XOR");
}

static void test_bitwise_not() {
    std::printf("[test_bitwise_not]\n");
    // ~0 in 64-bit signed = -1 होता है
    auto r = run_holyc("I64 x = ~0;\n\"%d\\n\", x;");
    CHECK(r.output == "-1\n", "bitwise NOT");
}

static void test_bitwise_shift() {
    std::printf("[test_bitwise_shift]\n");
    auto r = run_holyc("I64 x = 1 << 4;\n\"%d\\n\", x;");
    CHECK(r.output == "16\n", "left shift");
    auto r2 = run_holyc("I64 x = 16 >> 2;\n\"%d\\n\", x;");
    CHECK(r2.output == "4\n", "right shift");
}

static void test_if_else() {
    std::printf("[test_if_else]\n");
    auto r = run_holyc(
        "I64 x = 10;\n"
        "if (x > 5) {\n"
        "  \"big\\n\";\n"
        "} else {\n"
        "  \"small\\n\";\n"
        "}\n"
    );
    CHECK(r.output == "big\n", "if-else true branch");

    auto r2 = run_holyc(
        "I64 x = 2;\n"
        "if (x > 5) {\n"
        "  \"big\\n\";\n"
        "} else {\n"
        "  \"small\\n\";\n"
        "}\n"
    );
    CHECK(r2.output == "small\n", "if-else false branch");
}

static void test_if_else_if() {
    std::printf("[test_if_else_if]\n");
    auto r = run_holyc(
        "I64 x = 2;\n"
        "if (x == 1) { \"one\\n\"; }\n"
        "else if (x == 2) { \"two\\n\"; }\n"
        "else { \"other\\n\"; }\n"
    );
    CHECK(r.output == "two\n", "else-if chain");
}

static void test_for_loop() {
    std::printf("[test_for_loop]\n");
    auto r = run_holyc(
        "I64 sum = 0;\n"
        "I64 i;\n"
        "for (i = 1; i <= 5; i++) {\n"
        "  sum += i;\n"
        "}\n"
        "\"%d\\n\", sum;\n"
    );
    CHECK(r.output == "15\n", "for loop sum 1..5");
}

static void test_while_loop() {
    std::printf("[test_while_loop]\n");
    auto r = run_holyc(
        "I64 n = 5;\n"
        "I64 fact = 1;\n"
        "while (n > 1) {\n"
        "  fact *= n;\n"
        "  n--;\n"
        "}\n"
        "\"%d\\n\", fact;\n"
    );
    CHECK(r.output == "120\n", "while loop factorial");
}

static void test_do_while_loop() {
    std::printf("[test_do_while_loop]\n");
    auto r = run_holyc(
        "I64 x = 0;\n"
        "do {\n"
        "  x++;\n"
        "} while (x < 3);\n"
        "\"%d\\n\", x;\n"
    );
    CHECK(r.output == "3\n", "do-while loop");
}

static void test_switch_case() {
    std::printf("[test_switch_case]\n");
    auto r = run_holyc(
        "I64 x = 2;\n"
        "switch (x) {\n"
        "  case 1: \"one\\n\"; break;\n"
        "  case 2: \"two\\n\"; break;\n"
        "  case 3: \"three\\n\"; break;\n"
        "  default: \"other\\n\"; break;\n"
        "}\n"
    );
    CHECK(r.output == "two\n", "switch case match");
}

static void test_switch_default() {
    std::printf("[test_switch_default]\n");
    auto r = run_holyc(
        "I64 x = 99;\n"
        "switch (x) {\n"
        "  case 1: \"one\\n\"; break;\n"
        "  default: \"default\\n\"; break;\n"
        "}\n"
    );
    CHECK(r.output == "default\n", "switch default");
}

static void test_switch_fallthrough() {
    std::printf("[test_switch_fallthrough]\n");
    auto r = run_holyc(
        "I64 x = 1;\n"
        "switch (x) {\n"
        "  case 1: \"a\";\n"
        "  case 2: \"b\"; break;\n"
        "  case 3: \"c\"; break;\n"
        "}\n"
    );
    CHECK(r.output == "ab", "switch fall-through");
}

static void test_func_decl_and_call() {
    std::printf("[test_func_decl_and_call]\n");
    auto r = run_holyc(
        "I64 Add(I64 a, I64 b) {\n"
        "  return a + b;\n"
        "}\n"
        "\"%d\\n\", Add(3, 4);\n"
    );
    CHECK(r.output == "7\n", "function call");
}

static void test_func_void() {
    std::printf("[test_func_void]\n");
    auto r = run_holyc(
        "U0 Greet() {\n"
        "  \"Hi\\n\";\n"
        "}\n"
        "Greet();\n"
    );
    CHECK(r.output == "Hi\n", "void function call");
}

static void test_recursive_fibonacci() {
    std::printf("[test_recursive_fibonacci]\n");
    auto r = run_holyc(
        "I64 Fib(I64 n) {\n"
        "  if (n <= 1) return n;\n"
        "  return Fib(n - 1) + Fib(n - 2);\n"
        "}\n"
        "\"%d\\n\", Fib(10);\n"
    );
    CHECK(r.output == "55\n", "fibonacci(10)");
}

static void test_func_default_args() {
    std::printf("[test_func_default_args]\n");
    auto r = run_holyc(
        "I64 Mul(I64 a, I64 b=10) {\n"
        "  return a * b;\n"
        "}\n"
        "\"%d\\n\", Mul(5);\n"
    );
    CHECK(r.output == "50\n", "default argument");
}

static void test_format_d() {
    std::printf("[test_format_d]\n");
    auto r = run_holyc("\"%d\\n\", 42;");
    CHECK(r.output == "42\n", "format %%d");
}

static void test_format_x() {
    std::printf("[test_format_x]\n");
    auto r = run_holyc("\"%x\\n\", 255;");
    CHECK(r.output == "ff\n", "format %%x");
}

static void test_format_c() {
    std::printf("[test_format_c]\n");
    auto r = run_holyc("\"%c\\n\", 65;");
    CHECK(r.output == "A\n", "format %%c");
}

static void test_format_s() {
    std::printf("[test_format_s]\n");
    auto r = run_holyc("\"%s\\n\", \"hello\";");
    CHECK(r.output == "hello\n", "format %%s");
}

static void test_format_f() {
    std::printf("[test_format_f]\n");
    auto r = run_holyc("\"%.1f\\n\", 3.5;");
    CHECK(r.output == "3.5\n", "format %%f with precision");
}

static void test_nested_calls() {
    std::printf("[test_nested_calls]\n");
    auto r = run_holyc(
        "I64 Double(I64 x) { return x * 2; }\n"
        "I64 Inc(I64 x) { return x + 1; }\n"
        "\"%d\\n\", Double(Inc(5));\n"
    );
    CHECK(r.output == "12\n", "nested function calls");
}

static void test_break_in_loop() {
    std::printf("[test_break_in_loop]\n");
    auto r = run_holyc(
        "I64 i;\n"
        "for (i = 0; i < 10; i++) {\n"
        "  if (i == 3) break;\n"
        "}\n"
        "\"%d\\n\", i;\n"
    );
    CHECK(r.output == "3\n", "break in for loop");
}

static void test_continue_in_loop() {
    std::printf("[test_continue_in_loop]\n");
    auto r = run_holyc(
        "I64 sum = 0;\n"
        "I64 i;\n"
        "for (i = 0; i < 6; i++) {\n"
        "  if (i % 2 == 0) continue;\n"
        "  sum += i;\n"
        "}\n"
        "\"%d\\n\", sum;\n"
    );
    // odd numbers 1+3+5 = 9 का sum होना चाहिए
    CHECK(r.output == "9\n", "continue skips even numbers");
}

static void test_global_vs_local() {
    std::printf("[test_global_vs_local]\n");
    auto r = run_holyc(
        "I64 g = 100;\n"
        "I64 GetG() { return g; }\n"
        "I64 UseLocal() {\n"
        "  I64 loc = 5;\n"
        "  return loc;\n"
        "}\n"
        "\"%d\\n\", GetG();\n"
        "\"%d\\n\", UseLocal();\n"
    );
    CHECK(r.output == "100\n5\n", "global vs local variable");
}

static void test_assignment_operators() {
    std::printf("[test_assignment_operators]\n");
    auto r = run_holyc(
        "I64 x = 10;\n"
        "x += 5;\n\"%d\\n\", x;\n"
        "x -= 3;\n\"%d\\n\", x;\n"
        "x *= 2;\n\"%d\\n\", x;\n"
        "x /= 4;\n\"%d\\n\", x;\n"
        "x %= 5;\n\"%d\\n\", x;\n"
    );
    // 10+5=15, 15-3=12, 12*2=24, 24/4=6, 6%5=1 होना चाहिए
    CHECK(r.output == "15\n12\n24\n6\n1\n", "compound assignment operators");
}

static void test_unary_negate() {
    std::printf("[test_unary_negate]\n");
    auto r = run_holyc("I64 x = -42;\n\"%d\\n\", x;");
    CHECK(r.output == "-42\n", "unary negate");
}

static void test_pre_post_increment() {
    std::printf("[test_pre_post_increment]\n");
    auto r = run_holyc(
        "I64 x = 5;\n"
        "\"%d\\n\", x++;\n"
        "\"%d\\n\", x;\n"
        "\"%d\\n\", ++x;\n"
    );
    // x++ 5 return करता है, x 6 बनता है, ++x x=7 बनाकर 7 return करता है
    CHECK(r.output == "5\n6\n7\n", "pre/post increment");
}

static void test_ternary_expr() {
    std::printf("[test_ternary_expr]\n");
    auto r = run_holyc("I64 x = (5 > 3) ? 1 : 0;\n\"%d\\n\", x;");
    CHECK(r.output == "1\n", "ternary expression");
}

static void test_main_function() {
    std::printf("[test_main_function]\n");
    auto r = run_holyc(
        "U0 Main() {\n"
        "  \"from main\\n\";\n"
        "}\n"
    );
    CHECK(r.output == "from main\n", "Main function auto-called");
}

static void test_main_return_code() {
    std::printf("[test_main_return_code]\n");
    auto r = run_holyc(
        "I64 Main() {\n"
        "  return 42;\n"
        "}\n"
    );
    CHECK(r.exit_code == 42, "Main return code");
}

static void test_float_arithmetic() {
    std::printf("[test_float_arithmetic]\n");
    auto r = run_holyc(
        "F64 a = 1.5;\n"
        "F64 b = 2.5;\n"
        "\"%.1f\\n\", a + b;\n"
    );
    CHECK(r.output == "4.0\n", "float addition");
}

static void test_for_loop_with_decl() {
    std::printf("[test_for_loop_with_decl]\n");
    auto r = run_holyc(
        "I64 sum = 0;\n"
        "I64 i;\n"
        "for (i = 0; i < 3; i++) {\n"
        "  sum += i;\n"
        "}\n"
        "\"%d\\n\", sum;\n"
    );
    // 0+1+2=3 होना चाहिए
    CHECK(r.output == "3\n", "for loop with i=0..2");
}

static void test_multiple_functions() {
    std::printf("[test_multiple_functions]\n");
    auto r = run_holyc(
        "I64 Square(I64 x) { return x * x; }\n"
        "I64 Cube(I64 x) { return x * Square(x); }\n"
        "\"%d\\n\", Cube(3);\n"
    );
    CHECK(r.output == "27\n", "function calling function");
}

static void test_string_output_multi_args() {
    std::printf("[test_string_output_multi_args]\n");
    auto r = run_holyc("\"%d %d %d\\n\", 1, 2, 3;");
    CHECK(r.output == "1 2 3\n", "string output with multiple args");
}

static void test_malloc_free() {
    std::printf("[test_malloc_free]\n");
    // बस verify करो कि crash नहीं होता
    auto r = run_holyc(
        "U8 *p = MAlloc(64);\n"
        "if (p) \"ok\\n\";\n"
        "Free(p);\n"
    );
    CHECK(r.output == "ok\n", "MAlloc/Free no crash");
}

static void test_nested_loops() {
    std::printf("[test_nested_loops]\n");
    auto r = run_holyc(
        "I64 count = 0;\n"
        "I64 i;\n"
        "I64 j;\n"
        "for (i = 0; i < 3; i++) {\n"
        "  for (j = 0; j < 3; j++) {\n"
        "    count++;\n"
        "  }\n"
        "}\n"
        "\"%d\\n\", count;\n"
    );
    CHECK(r.output == "9\n", "nested for loops 3x3");
}

static void test_recursive_factorial() {
    std::printf("[test_recursive_factorial]\n");
    auto r = run_holyc(
        "I64 Fact(I64 n) {\n"
        "  if (n <= 1) return 1;\n"
        "  return n * Fact(n - 1);\n"
        "}\n"
        "\"%d\\n\", Fact(6);\n"
    );
    CHECK(r.output == "720\n", "recursive factorial(6)");
}

static void test_while_break() {
    std::printf("[test_while_break]\n");
    auto r = run_holyc(
        "I64 x = 0;\n"
        "while (1) {\n"
        "  if (x >= 5) break;\n"
        "  x++;\n"
        "}\n"
        "\"%d\\n\", x;\n"
    );
    CHECK(r.output == "5\n", "while with break");
}

static void test_multiple_string_outputs() {
    std::printf("[test_multiple_string_outputs]\n");
    auto r = run_holyc(
        "\"a\";\n"
        "\"b\";\n"
        "\"c\\n\";\n"
    );
    CHECK(r.output == "abc\n", "multiple string outputs");
}

static void test_division_by_zero() {
    std::printf("[test_division_by_zero]\n");
    // integer division by zero के लिए 0 return होना चाहिए (crash नहीं)
    auto r = run_holyc("I64 x = 10 / 0;\n\"%d\\n\", x;");
    CHECK(r.output == "0\n", "division by zero returns 0");
}

static void test_negative_numbers() {
    std::printf("[test_negative_numbers]\n");
    auto r = run_holyc(
        "I64 a = -5;\n"
        "I64 b = -3;\n"
        "\"%d\\n\", a + b;\n"
    );
    CHECK(r.output == "-8\n", "negative number addition");
}

static void test_print_format_width() {
    std::printf("[test_print_format_width]\n");
    auto r = run_holyc("Print(\"%5d\\n\", 42);");
    CHECK(r.output == "   42\n", "Print with width specifier");
}

static void test_builtin_abs() {
    std::printf("[test_builtin_abs]\n");
    auto r = run_holyc("\"%d\\n\", Abs(-42);");
    CHECK(r.output == "42\n", "Abs builtin");
}

static void test_builtin_sqrt() {
    std::printf("[test_builtin_sqrt]\n");
    auto r = run_holyc("\"%.1f\\n\", Sqrt(9.0);");
    CHECK(r.output == "3.0\n", "Sqrt builtin");
}

static void test_bitwise_assign() {
    std::printf("[test_bitwise_assign]\n");
    auto r = run_holyc(
        "I64 x = 0xFF;\n"
        "x &= 0x0F;\n"
        "\"%d\\n\", x;\n"
        "x |= 0xF0;\n"
        "\"%d\\n\", x;\n"
        "x ^= 0xFF;\n"
        "\"%d\\n\", x;\n"
    );
    // 0xFF & 0x0F = 15, 15 | 0xF0 = 255, 255 ^ 0xFF = 0 होना चाहिए
    CHECK(r.output == "15\n255\n0\n", "bitwise assignment operators");
}

static void test_shift_assign() {
    std::printf("[test_shift_assign]\n");
    auto r = run_holyc(
        "I64 x = 1;\n"
        "x <<= 4;\n"
        "\"%d\\n\", x;\n"
    );
    CHECK(r.output == "16\n", "shift-left assign");
}

static void test_do_while_executes_once() {
    std::printf("[test_do_while_executes_once]\n");
    auto r = run_holyc(
        "I64 x = 0;\n"
        "do {\n"
        "  x++;\n"
        "} while (0);\n"
        "\"%d\\n\", x;\n"
    );
    CHECK(r.output == "1\n", "do-while executes at least once");
}

static void test_empty_main() {
    std::printf("[test_empty_main]\n");
    auto r = run_holyc("U0 Main() {}");
    CHECK(r.exit_code == 0, "empty Main returns 0");
    CHECK(r.output.empty(), "empty Main no output");
}

static void test_func_multiple_params() {
    std::printf("[test_func_multiple_params]\n");
    auto r = run_holyc(
        "I64 Sum3(I64 a, I64 b, I64 c) {\n"
        "  return a + b + c;\n"
        "}\n"
        "\"%d\\n\", Sum3(10, 20, 30);\n"
    );
    CHECK(r.output == "60\n", "function with 3 params");
}

static void test_calloc_builtin() {
    std::printf("[test_calloc_builtin]\n");
    auto r = run_holyc(
        "U8 *p = CAlloc(8);\n"
        "if (p) \"ok\\n\";\n"
        "Free(p);\n"
    );
    CHECK(r.output == "ok\n", "CAlloc builtin");
}

// ==========================================================================
// main — सभी tests यहाँ से चलाओ
// ==========================================================================

int main() {
    test_hello_world();
    test_print_builtin();
    test_var_decl_i64();
    test_var_decl_u8();
    test_var_decl_f64();
    test_var_decl_bool();
    test_arithmetic_add();
    test_arithmetic_sub();
    test_arithmetic_mul();
    test_arithmetic_div();
    test_arithmetic_mod();
    test_comparison_eq();
    test_comparison_ne();
    test_comparison_lt();
    test_comparison_gt();
    test_comparison_le();
    test_comparison_ge();
    test_logical_and();
    test_logical_or();
    test_logical_not();
    test_bitwise_and();
    test_bitwise_or();
    test_bitwise_xor();
    test_bitwise_not();
    test_bitwise_shift();
    test_if_else();
    test_if_else_if();
    test_for_loop();
    test_while_loop();
    test_do_while_loop();
    test_switch_case();
    test_switch_default();
    test_switch_fallthrough();
    test_func_decl_and_call();
    test_func_void();
    test_recursive_fibonacci();
    test_func_default_args();
    test_format_d();
    test_format_x();
    test_format_c();
    test_format_s();
    test_format_f();
    test_nested_calls();
    test_break_in_loop();
    test_continue_in_loop();
    test_global_vs_local();
    test_assignment_operators();
    test_unary_negate();
    test_pre_post_increment();
    test_ternary_expr();
    test_main_function();
    test_main_return_code();
    test_float_arithmetic();
    test_for_loop_with_decl();
    test_multiple_functions();
    test_string_output_multi_args();
    test_malloc_free();
    test_nested_loops();
    test_recursive_factorial();
    test_while_break();
    test_multiple_string_outputs();
    test_division_by_zero();
    test_negative_numbers();
    test_print_format_width();
    test_builtin_abs();
    test_builtin_sqrt();
    test_bitwise_assign();
    test_shift_assign();
    test_do_while_executes_once();
    test_empty_main();
    test_func_multiple_params();
    test_calloc_builtin();

    std::printf("\n%d/%d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
