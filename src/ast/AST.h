#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "support/SourceLocation.h"
#include "ast/Types.h"

namespace holyc {

// ============================================================================
// Enums — सभी enum definitions यहाँ हैं
// ============================================================================

enum class BinOpKind {
    Add, Sub, Mul, Div, Mod,
    BitAnd, BitOr, BitXor, Shl, Shr,
    Eq, Ne, Lt, Le, Gt, Ge,
    LogAnd, LogOr, LogXor,
    Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign,
    BitAndAssign, BitOrAssign, BitXorAssign, ShlAssign, ShrAssign,
    PPAssign, MMAssign
};

enum class UnOpKind {
    PreInc, PreDec, PostInc, PostDec,
    AddrOf, Deref, LogNot, BitNot, Negate
};

enum class Storage { Default, Static, Reg, NoReg };
enum class Linkage { None, Public, Extern, ExternC, ExternAsm, Intern, Import };

/** @brief HolyC function declaration से attach हो सकने वाले modifier flags। */
struct FuncFlags {
    bool interrupt = false;   //!< Function interrupt handler है।
    bool haserrcode = false;  //!< Interrupt handler error code receive करता है।
    bool argpop = false;      //!< Callee अपने arguments खुद pop करता है।
    bool noargpop = false;    //!< Callee argument pop suppress करो।
};

/**
 * @brief Binary operator का source spelling लौटाओ (जैसे Add के लिए "+")।
 *
 * @param op Binary operator kind।
 * @return Null-terminated string spelling।
 */
inline const char* binOpKindToString(BinOpKind op) {
    switch (op) {
    case BinOpKind::Add: return "+"; case BinOpKind::Sub: return "-";
    case BinOpKind::Mul: return "*"; case BinOpKind::Div: return "/";
    case BinOpKind::Mod: return "%";
    case BinOpKind::BitAnd: return "&"; case BinOpKind::BitOr: return "|";
    case BinOpKind::BitXor: return "^"; case BinOpKind::Shl: return "<<";
    case BinOpKind::Shr: return ">>";
    case BinOpKind::Eq: return "=="; case BinOpKind::Ne: return "!=";
    case BinOpKind::Lt: return "<"; case BinOpKind::Le: return "<=";
    case BinOpKind::Gt: return ">"; case BinOpKind::Ge: return ">=";
    case BinOpKind::LogAnd: return "&&"; case BinOpKind::LogOr: return "||";
    case BinOpKind::LogXor: return "^^";
    case BinOpKind::Assign: return "=";
    case BinOpKind::AddAssign: return "+="; case BinOpKind::SubAssign: return "-=";
    case BinOpKind::MulAssign: return "*="; case BinOpKind::DivAssign: return "/=";
    case BinOpKind::ModAssign: return "%=";
    case BinOpKind::BitAndAssign: return "&="; case BinOpKind::BitOrAssign: return "|=";
    case BinOpKind::BitXorAssign: return "^="; case BinOpKind::ShlAssign: return "<<=";
    case BinOpKind::ShrAssign: return ">>=";
    case BinOpKind::PPAssign: return "++="; case BinOpKind::MMAssign: return "--=";
    }
    return "?";
}

/**
 * @brief Unary operator का source spelling लौटाओ (जैसे Deref के लिए "*")।
 *
 * @param op Unary operator kind।
 * @return Null-terminated string spelling।
 */
inline const char* unOpKindToString(UnOpKind op) {
    switch (op) {
    case UnOpKind::PreInc: return "++"; case UnOpKind::PreDec: return "--";
    case UnOpKind::PostInc: return "++"; case UnOpKind::PostDec: return "--";
    case UnOpKind::AddrOf: return "&"; case UnOpKind::Deref: return "*";
    case UnOpKind::LogNot: return "!"; case UnOpKind::BitNot: return "~";
    case UnOpKind::Negate: return "-";
    }
    return "?";
}

// ============================================================================
// Node kind enum (for RTTI) — dynamic_cast की जगह यही use होता है
// ============================================================================

/** @brief dynamic_cast की जगह AST node dispatch के लिए discriminant। */
enum class NodeKind {
    // Expressions — expression node kinds के प्रकार
    IntLiteralExpr, FloatLiteralExpr, StringLiteralExpr, CharLiteralExpr,
    BoolLiteralExpr, IdentifierExpr, BinaryExpr, UnaryExpr, TernaryExpr,
    ChainedCmpExpr, PowerExpr, CallExpr, PostfixCastExpr, SizeofExpr,
    OffsetExpr, ArrayIndexExpr, FieldAccessExpr, AddrOfExpr, DerefExpr,
    ThrowExpr,
    InitListExpr,
    // Statements — statement node kinds के प्रकार
    CompoundStmt, DeclStmt, ExprStmt, IfStmt, ForStmt, WhileStmt,
    DoWhileStmt, SwitchStmt, CaseStmt, BreakStmt, ContinueStmt,
    ReturnStmt, GotoStmt, LabelStmt, AsmStmt, TryCatchStmt,
    ExeBlockStmt, StringOutputStmt,
    // Declarations — declaration node kinds के प्रकार
    VarDecl, FuncDecl, ParamDecl, ClassDecl, UnionDecl, FieldDecl,
    TypedefDecl, ExternDecl, EnumDecl, CompoundDecl
};

// ============================================================================
// Base classes — सभी AST nodes के base class definitions
// ============================================================================

/** @brief सभी AST nodes का base class; NodeKind tag और source location carry करता है। */
struct Node {
    NodeKind nk;
    SourceLocation loc;
    explicit Node(NodeKind k) : nk(k) {}
    virtual ~Node() = default;
};

/** @brief सभी expression nodes का base class; sema के बाद resolved type carry करता है। */
struct Expr : Node {
    Type* resolved_type = nullptr; //!< Semantic analysis से पहले Null।
    explicit Expr(NodeKind k) : Node(k) {}
};

/** @brief सभी statement nodes का base class। */
struct Stmt : Node {
    explicit Stmt(NodeKind k) : Node(k) {}
};

/** @brief सभी declaration nodes का base class। */
struct Decl : Node {
    explicit Decl(NodeKind k) : Node(k) {}
};

// ============================================================================
// Forward declarations — आगे use होने वाले types के forward declarations
// ============================================================================

struct CaseStmt;
struct CompoundStmt;
struct ParamDecl;
struct FieldDecl;
struct StringLiteralExpr;

// ============================================================================
// Expressions — सभी expression AST node types
// ============================================================================

/** @brief Integer literal constant (जैसे `42`, `0xFF`)। */
struct IntLiteralExpr : Expr {
    uint64_t value;
    PrimKind type_hint;
    IntLiteralExpr() : Expr(NodeKind::IntLiteralExpr), value(0), type_hint(PrimKind::I64) {}
};

/** @brief Floating-point literal constant (जैसे `3.14`)। */
struct FloatLiteralExpr : Expr {
    double value;
    FloatLiteralExpr() : Expr(NodeKind::FloatLiteralExpr), value(0.0) {}
};

/** @brief String literal (जैसे `"hello"`); value unescaped content hold करता है। */
struct StringLiteralExpr : Expr {
    std::string value;
    StringLiteralExpr() : Expr(NodeKind::StringLiteralExpr) {}
};

/** @brief Character literal (जैसे `'A'` या `'AB'`); byte_count 1–8 होता है। */
struct CharLiteralExpr : Expr {
    uint64_t value;
    int byte_count;
    CharLiteralExpr() : Expr(NodeKind::CharLiteralExpr), value(0), byte_count(1) {}
};

/** @brief Boolean literal: TRUE या FALSE। */
struct BoolLiteralExpr : Expr {
    bool value;
    BoolLiteralExpr() : Expr(NodeKind::BoolLiteralExpr), value(false) {}
};

/** @brief Named variable, function, या constant का reference। */
struct IdentifierExpr : Expr {
    std::string name;
    Decl* resolved = nullptr;
    IdentifierExpr() : Expr(NodeKind::IdentifierExpr) {}
};

/** @brief Binary expression (जैसे `a + b`, `x = y`)। */
struct BinaryExpr : Expr {
    BinOpKind op;
    Expr* lhs;
    Expr* rhs;
    BinaryExpr() : Expr(NodeKind::BinaryExpr), op(BinOpKind::Add), lhs(nullptr), rhs(nullptr) {}
};

/** @brief Unary expression (जैसे `-x`, `!b`, `++i`, `i++`)। */
struct UnaryExpr : Expr {
    UnOpKind op;
    Expr* operand;
    bool is_postfix;
    UnaryExpr() : Expr(NodeKind::UnaryExpr), op(UnOpKind::Negate), operand(nullptr), is_postfix(false) {}
};

/** @brief Conditional ternary expression: `cond ? then_expr : else_expr`। */
struct TernaryExpr : Expr {
    Expr* cond;
    Expr* then_expr;
    Expr* else_expr;
    TernaryExpr() : Expr(NodeKind::TernaryExpr), cond(nullptr), then_expr(nullptr), else_expr(nullptr) {}
};

/** @brief Chained comparison (जैसे `a < b < c`): operands और ops parallel arrays हैं। */
struct ChainedCmpExpr : Expr {
    std::vector<Expr*> operands;
    std::vector<BinOpKind> ops;
    ChainedCmpExpr() : Expr(NodeKind::ChainedCmpExpr) {}
};

/** @brief Backtick operator से exponentiation expression: `base \` exp`। */
struct PowerExpr : Expr {
    Expr* base;
    Expr* exp;
    PowerExpr() : Expr(NodeKind::PowerExpr), base(nullptr), exp(nullptr) {}
};

/** @brief Function या method call expression। */
struct CallExpr : Expr {
    Expr* callee;
    std::vector<Expr*> args;
    CallExpr() : Expr(NodeKind::CallExpr), callee(nullptr) {}
};

/** @brief HolyC postfix type cast: `expr(TypeName)`। */
struct PostfixCastExpr : Expr {
    Expr* expr;
    Type* target_type;
    PostfixCastExpr() : Expr(NodeKind::PostfixCastExpr), expr(nullptr), target_type(nullptr) {}
};

/** @brief Sizeof expression; target_type या target_expr में से exactly एक non-null होता है। */
struct SizeofExpr : Expr {
    Type* target_type;
    Expr* target_expr; // एक null होता है
    SizeofExpr() : Expr(NodeKind::SizeofExpr), target_type(nullptr), target_expr(nullptr) {}
};

/** @brief Offset-of expression: `Offset(ClassName, member)`। */
struct OffsetExpr : Expr {
    std::string class_name;
    std::string member_name;
    OffsetExpr() : Expr(NodeKind::OffsetExpr) {}
};

/** @brief Array subscript expression: `base[index]`। */
struct ArrayIndexExpr : Expr {
    Expr* base;
    Expr* index;
    ArrayIndexExpr() : Expr(NodeKind::ArrayIndexExpr), base(nullptr), index(nullptr) {}
};

/** @brief Field access expression: `object.field` या `ptr->field`। */
struct FieldAccessExpr : Expr {
    Expr* object;
    std::string field;
    bool is_arrow;
    FieldAccessExpr() : Expr(NodeKind::FieldAccessExpr), object(nullptr), is_arrow(false) {}
};

/** @brief Address-of expression: `&operand`। */
struct AddrOfExpr : Expr {
    Expr* operand;
    AddrOfExpr() : Expr(NodeKind::AddrOfExpr), operand(nullptr) {}
};

/** @brief Pointer dereference expression: `*operand`। */
struct DerefExpr : Expr {
    Expr* operand;
    DerefExpr() : Expr(NodeKind::DerefExpr), operand(nullptr) {}
};

/** @brief HolyC exception raise करने वाला throw expression: `throw(code)`। */
struct ThrowExpr : Expr {
    Expr* code;
    ThrowExpr() : Expr(NodeKind::ThrowExpr), code(nullptr) {}
};

/** @brief Brace-enclosed aggregate initializer: `{v0, v1, v2}`। */
struct InitListExpr : Expr {
    std::vector<Expr*> values;
    InitListExpr() : Expr(NodeKind::InitListExpr) {}
};

// ============================================================================
// Statements — सभी statement AST node types
// ============================================================================

/** @brief Statements का sequence contain करने वाला braced block। */
struct CompoundStmt : Stmt {
    std::vector<Stmt*> stmts;
    CompoundStmt() : Stmt(NodeKind::CompoundStmt) {}
};

/** @brief Declaration wrap करने वाला statement। */
struct DeclStmt : Stmt {
    Decl* decl;
    DeclStmt() : Stmt(NodeKind::DeclStmt), decl(nullptr) {}
};

/** @brief Statement की तरह use होने वाला expression (जैसे function call)। */
struct ExprStmt : Stmt {
    Expr* expr;
    ExprStmt() : Stmt(NodeKind::ExprStmt), expr(nullptr) {}
};

/** @brief If/else statement; else_body null हो सकता है। */
struct IfStmt : Stmt {
    Expr* cond;
    Stmt* then_body;
    Stmt* else_body; // nullable — null हो सकता है
    IfStmt() : Stmt(NodeKind::IfStmt), cond(nullptr), then_body(nullptr), else_body(nullptr) {}
};

/** @brief For loop; init, cond, और post सभी optional (nullable) हैं। */
struct ForStmt : Stmt {
    Stmt* init;   // nullable — null हो सकता है (optional)
    Expr* cond;   // nullable — null हो सकता है (optional)
    Expr* post;   // nullable — null हो सकता है (optional)
    Stmt* body;
    ForStmt() : Stmt(NodeKind::ForStmt), init(nullptr), cond(nullptr), post(nullptr), body(nullptr) {}
};

/** @brief While loop। */
struct WhileStmt : Stmt {
    Expr* cond;
    Stmt* body;
    WhileStmt() : Stmt(NodeKind::WhileStmt), cond(nullptr), body(nullptr) {}
};

/** @brief Do-while loop। */
struct DoWhileStmt : Stmt {
    Stmt* body;
    Expr* cond;
    DoWhileStmt() : Stmt(NodeKind::DoWhileStmt), body(nullptr), cond(nullptr) {}
};

/** @brief Case branches की list वाला switch statement। */
struct SwitchStmt : Stmt {
    Expr* expr;
    std::vector<CaseStmt*> cases;
    SwitchStmt() : Stmt(NodeKind::SwitchStmt), expr(nullptr) {}
};

/** @brief Switch के अंदर case या default clause; `case N..M:` range syntax support करता है। */
struct CaseStmt : Stmt {
    Expr* value;       // null = default — default case के लिए null
    Expr* range_end;   // case N..M: syntax के लिए non-null
    std::vector<Stmt*> stmts;
    CaseStmt() : Stmt(NodeKind::CaseStmt), value(nullptr), range_end(nullptr) {}
};

/** @brief Enclosing loop या switch से बाहर निकलने वाला break statement। */
struct BreakStmt : Stmt {
    BreakStmt() : Stmt(NodeKind::BreakStmt) {}
};

/** @brief Enclosing loop की next iteration पर jump करने वाला continue statement। */
struct ContinueStmt : Stmt {
    ContinueStmt() : Stmt(NodeKind::ContinueStmt) {}
};

/** @brief Return statement; void returns के लिए value null हो सकती है। */
struct ReturnStmt : Stmt {
    Expr* value; // nullable — void return के लिए null
    ReturnStmt() : Stmt(NodeKind::ReturnStmt), value(nullptr) {}
};

/** @brief Named label को target करने वाला unconditional goto। */
struct GotoStmt : Stmt {
    std::string label;
    GotoStmt() : Stmt(NodeKind::GotoStmt) {}
};

/** @brief Optional statement के साथ named label। */
struct LabelStmt : Stmt {
    std::string name;
    Stmt* stmt;
    LabelStmt() : Stmt(NodeKind::LabelStmt), stmt(nullptr) {}
};

/** @brief Extended asm statement के लिए operand constraint (जैसे `"=r"(var)`)। */
struct AsmConstraint {
    std::string constraint; // जैसे "=r", "r", "m", "i"
    Expr* expr = nullptr;
};

/** @brief Optional GCC-style input/output constraints वाला inline assembly statement। */
struct AsmStmt : Stmt {
    std::string raw_text;
    std::vector<std::string> imports;
    std::vector<std::string> exports;
    std::vector<AsmConstraint> outputs; // "=r"(expr) — output constraint (लिखने वाले registers)
    std::vector<AsmConstraint> inputs;  // "r"(expr) — input constraint (पढ़ने वाले registers)
    std::vector<std::string>   clobbers; // "rax", "memory", "cc" — clobbered registers (बदल जाने वाले)
    AsmStmt() : Stmt(NodeKind::AsmStmt) {}
};

/** @brief setjmp/longjmp exception handling से try/catch block। */
struct TryCatchStmt : Stmt {
    CompoundStmt* try_body;
    CompoundStmt* catch_body;
    TryCatchStmt() : Stmt(NodeKind::TryCatchStmt), try_body(nullptr), catch_body(nullptr) {}
};

/** @brief HolyC `exe { }` compile-time execution block। */
struct ExeBlockStmt : Stmt {
    CompoundStmt* body;
    ExeBlockStmt() : Stmt(NodeKind::ExeBlockStmt), body(nullptr) {}
};

/** @brief HolyC `"fmt", arg1, arg2;` string-output statement। */
struct StringOutputStmt : Stmt {
    StringLiteralExpr* format;
    std::vector<Expr*> args;
    StringOutputStmt() : Stmt(NodeKind::StringOutputStmt), format(nullptr) {}
};

// ============================================================================
// Declarations — सभी declaration AST node types
// ============================================================================

/** @brief Variable declaration, optionally type, storage class, और initializer के साथ। */
struct VarDecl : Decl {
    Type* type;
    std::string name;
    Expr* init;
    Storage storage;
    bool no_warn;
    VarDecl() : Decl(NodeKind::VarDecl), type(nullptr), init(nullptr), storage(Storage::Default), no_warn(false) {}
};

/** @brief Optional default value के साथ single function parameter declaration। */
struct ParamDecl : Decl {
    Type* type;
    std::string name;
    Expr* default_value;
    ParamDecl() : Decl(NodeKind::ParamDecl), type(nullptr), default_value(nullptr) {}
};

/** @brief Function definition या prototype। */
struct FuncDecl : Decl {
    Type* return_type;
    std::string name;
    std::vector<ParamDecl*> params;
    CompoundStmt* body;
    Linkage linkage;
    FuncFlags flags;
    bool is_vararg;
    FuncDecl() : Decl(NodeKind::FuncDecl), return_type(nullptr), body(nullptr), linkage(Linkage::None), is_vararg(false) {}
};

/** @brief Optional base class और member list के साथ class declaration। */
struct ClassDecl : Decl {
    std::string name;
    std::string base_name;
    std::vector<Decl*> members;
    ClassDecl() : Decl(NodeKind::ClassDecl) {}
};

/** @brief Union declaration। */
struct UnionDecl : Decl {
    std::string name;
    std::vector<FieldDecl*> members;
    UnionDecl() : Decl(NodeKind::UnionDecl) {}
};

/** @brief Struct/class field, optionally bit-field width के साथ। */
struct FieldDecl : Decl {
    Type* type;
    std::string name;
    int bit_width; // -1 अगर bit-field नहीं है
    bool is_union_member = false;
    int union_group = -1;
    FieldDecl() : Decl(NodeKind::FieldDecl), type(nullptr), bit_width(-1) {}
};

/** @brief Type के लिए नया name introduce करने वाला typedef। */
struct TypedefDecl : Decl {
    Type* type;
    std::string name;
    TypedefDecl() : Decl(NodeKind::TypedefDecl), type(nullptr) {}
};

/** @brief किसी दूसरे declaration के इर्द-गिर्द extern/import linkage wrapper। */
struct ExternDecl : Decl {
    Linkage linkage;
    Decl* inner;
    ExternDecl() : Decl(NodeKind::ExternDecl), linkage(Linkage::Extern), inner(nullptr) {}
};

/** @brief Single enumerator; value null हो तो auto-increment। */
struct EnumMember {
    std::string name;
    Expr* value; // nullable; null हो तो auto-increment
};

/** @brief Named integer constants की list के साथ enum declaration। */
struct EnumDecl : Decl {
    std::string name; // empty हो सकता है (anonymous enum)
    std::vector<EnumMember> members;
    EnumDecl() : Decl(NodeKind::EnumDecl) {}
};

/** @brief Single statement जैसे `I64 i, j, k;` से multi-variable declaration। */
struct CompoundDecl : Decl {
    std::vector<VarDecl*> decls;
    CompoundDecl() : Decl(NodeKind::CompoundDecl) {}
};

} // namespace holyc
