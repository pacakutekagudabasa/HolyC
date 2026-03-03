#include "support/SourceManager.h"
#include "support/Diagnostics.h"
#include "support/Arena.h"
#include "preprocessor/Preprocessor.h"
#include "parser/Parser.h"
#include "ast/AST.h"
#include "ast/TranslationUnit.h"

#include <cstdio>
#include <string>

using namespace holyc;

static int total = 0, passed = 0;

#define CHECK(cond, msg) do { \
    total++; \
    if (cond) { passed++; std::printf("  PASS: %s\n", msg); } \
    else { std::printf("  FAIL: %s\n", msg); } \
} while(0)

static TranslationUnit* parse(const std::string& src, Arena& arena) {
    static SourceManager sm;
    static Diagnostics diag(&sm);
    int fid = sm.loadString("<test>", src);
    Preprocessor pp(sm, diag, fid);
    Parser parser(pp, diag, arena);
    return parser.parse();
}

static void test_var_decl() {
    std::printf("[test_var_decl]\n");
    Arena arena;
    auto* tu = parse("I64 x = 42;", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    CHECK(!tu->decls.empty(), "has declarations");
    if (!tu->decls.empty()) {
        // DeclStmt के अंदर VarDecl हो सकता है, या direct VarDecl भी
        Node* n = tu->decls[0];
        bool is_var = (n->nk == NodeKind::VarDecl);
        bool is_decl_stmt = (n->nk == NodeKind::DeclStmt);
        if (is_decl_stmt) {
            auto* ds = static_cast<DeclStmt*>(n);
            is_var = ds->decl && ds->decl->nk == NodeKind::VarDecl;
        }
        CHECK(is_var, "has VarDecl");
    }
}

static void test_func_decl() {
    std::printf("[test_func_decl]\n");
    Arena arena;
    auto* tu = parse("U0 Main() { }", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    CHECK(!tu->decls.empty(), "has declarations");
    if (!tu->decls.empty()) {
        Node* n = tu->decls[0];
        bool is_func = (n->nk == NodeKind::FuncDecl);
        bool is_decl_stmt = (n->nk == NodeKind::DeclStmt);
        if (is_decl_stmt) {
            auto* ds = static_cast<DeclStmt*>(n);
            is_func = ds->decl && ds->decl->nk == NodeKind::FuncDecl;
        }
        CHECK(is_func, "has FuncDecl");
    }
}

static void test_if_stmt() {
    std::printf("[test_if_stmt]\n");
    Arena arena;
    // function में wrap करो ताकि statement की तरह parse हो
    auto* tu = parse("U0 f() { if (x > 0) { x = 1; } }", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    CHECK(!tu->decls.empty(), "has declarations");
    if (!tu->decls.empty()) {
        // function body निकालो
        Node* n = tu->decls[0];
        FuncDecl* fd = nullptr;
        if (n->nk == NodeKind::FuncDecl)
            fd = static_cast<FuncDecl*>(n);
        else if (n->nk == NodeKind::DeclStmt)
            fd = static_cast<FuncDecl*>(static_cast<DeclStmt*>(n)->decl);
        CHECK(fd != nullptr && fd->body != nullptr, "function has body");
        if (fd && fd->body && !fd->body->stmts.empty()) {
            CHECK(fd->body->stmts[0]->nk == NodeKind::IfStmt, "has IfStmt");
        }
    }
}

static void test_for_stmt() {
    std::printf("[test_for_stmt]\n");
    Arena arena;
    auto* tu = parse("U0 f() { for (I64 i = 0; i < 10; i++) { } }", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    if (!tu->decls.empty()) {
        Node* n = tu->decls[0];
        FuncDecl* fd = nullptr;
        if (n->nk == NodeKind::FuncDecl)
            fd = static_cast<FuncDecl*>(n);
        else if (n->nk == NodeKind::DeclStmt)
            fd = static_cast<FuncDecl*>(static_cast<DeclStmt*>(n)->decl);
        if (fd && fd->body && !fd->body->stmts.empty()) {
            CHECK(fd->body->stmts[0]->nk == NodeKind::ForStmt, "has ForStmt");
        }
    }
}

static void test_string_output() {
    std::printf("[test_string_output]\n");
    Arena arena;
    auto* tu = parse("U0 f() { \"hello %d\\n\", x; }", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    if (!tu->decls.empty()) {
        Node* n = tu->decls[0];
        FuncDecl* fd = nullptr;
        if (n->nk == NodeKind::FuncDecl)
            fd = static_cast<FuncDecl*>(n);
        else if (n->nk == NodeKind::DeclStmt)
            fd = static_cast<FuncDecl*>(static_cast<DeclStmt*>(n)->decl);
        if (fd && fd->body && !fd->body->stmts.empty()) {
            CHECK(fd->body->stmts[0]->nk == NodeKind::StringOutputStmt, "has StringOutputStmt");
        }
    }
}

static void test_binary_expr_precedence() {
    std::printf("[test_binary_expr_precedence]\n");
    Arena arena;
    // HolyC में & का precedence + से ज्यादा है
    // तो "x & y + z" को "(x & y) + z" parse होना चाहिए
    // यानी BinaryExpr(Add, BinaryExpr(BitAnd, x, y), z)
    auto* tu = parse("U0 f() { x & y + z; }", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    if (!tu->decls.empty()) {
        Node* n = tu->decls[0];
        FuncDecl* fd = nullptr;
        if (n->nk == NodeKind::FuncDecl)
            fd = static_cast<FuncDecl*>(n);
        else if (n->nk == NodeKind::DeclStmt)
            fd = static_cast<FuncDecl*>(static_cast<DeclStmt*>(n)->decl);
        if (fd && fd->body && !fd->body->stmts.empty()) {
            Stmt* s = fd->body->stmts[0];
            Expr* expr = nullptr;
            if (s->nk == NodeKind::ExprStmt)
                expr = static_cast<ExprStmt*>(s)->expr;
            CHECK(expr != nullptr && expr->nk == NodeKind::BinaryExpr, "top-level is BinaryExpr");
            if (expr && expr->nk == NodeKind::BinaryExpr) {
                auto* bin = static_cast<BinaryExpr*>(expr);
                CHECK(bin->op == BinOpKind::Add, "top-level op is Add");
                CHECK(bin->lhs != nullptr && bin->lhs->nk == NodeKind::BinaryExpr, "lhs is BinaryExpr");
                if (bin->lhs && bin->lhs->nk == NodeKind::BinaryExpr) {
                    auto* inner = static_cast<BinaryExpr*>(bin->lhs);
                    CHECK(inner->op == BinOpKind::BitAnd, "inner op is BitAnd");
                }
            }
        }
    }
}

static void test_class_decl() {
    std::printf("[test_class_decl]\n");
    Arena arena;
    auto* tu = parse("class Foo { I64 a; U8 b; };", arena);
    CHECK(tu != nullptr, "TranslationUnit not null");
    CHECK(!tu->decls.empty(), "has declarations");
    if (!tu->decls.empty()) {
        Node* n = tu->decls[0];
        bool is_class = (n->nk == NodeKind::ClassDecl);
        bool is_decl_stmt = (n->nk == NodeKind::DeclStmt);
        if (is_decl_stmt) {
            auto* ds = static_cast<DeclStmt*>(n);
            is_class = ds->decl && ds->decl->nk == NodeKind::ClassDecl;
        }
        CHECK(is_class, "has ClassDecl");
    }
}

int main() {
    test_var_decl();
    test_func_decl();
    test_if_stmt();
    test_for_stmt();
    test_string_output();
    test_binary_expr_precedence();
    test_class_decl();

    std::printf("\n%d/%d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
