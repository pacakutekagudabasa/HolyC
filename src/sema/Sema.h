#pragma once

#include "ast/AST.h"
#include "ast/TranslationUnit.h"
#include "sema/Env.h"
#include "support/Arena.h"
#include "support/Diagnostics.h"

#include <set>

namespace holyc {

/**
 * @brief Semantic analysis pass: AST को type-check और annotate करता है।
 */
class Sema {
public:
    Sema(Diagnostics& diag, Arena& arena);

    /**
     * @brief Translation unit पर पूरा semantic analysis pass चलाओ।
     *
     * Forward-declaration pre-pass करता है, फिर सभी declarations,
     * statements, और expressions walk करके types resolve करता है और diagnostics report करता है।
     *
     * @param tu Analyze करने वाला translation unit। Null हो तो no-op।
     */
    void analyze(TranslationUnit* tu);

private:
    /**
     * @brief Node kind के हिसाब से analyzeDecl, analyzeStmt, या analyzeExpr पर dispatch करो।
     *
     * @param n Analyze करने वाला AST node।
     */
    void analyzeNode(Node* n);

    /**
     * @brief Declaration के लिए appropriate typed analyze method पर dispatch करो।
     *
     * @param d Analyze करने वाला declaration node।
     */
    void analyzeDecl(Decl* d);

    /**
     * @brief Statement के लिए appropriate typed analyze method पर dispatch करो।
     *
     * @param s Analyze करने वाला statement node।
     */
    void analyzeStmt(Stmt* s);

    /**
     * @brief Expression का type resolve करो, side effect के रूप में e->resolved_type set करो।
     *
     * @param e Analyze करने वाला expression।
     * @return Expression का resolved type, या e null हो तो nullptr।
     */
    Type* analyzeExpr(Expr* e);

    /**
     * @brief typeof() resolve करो, declared type validate करो, और any initializer check करो।
     *
     * @param d Analyze करने वाला variable declaration।
     */
    void analyzeVarDecl(VarDecl* d);

    /**
     * @brief Function declaration type-check करो, implicit 'this' inject करो, और body analyze करो।
     *
     * @param d Analyze करने वाला function declaration।
     */
    void analyzeFuncDecl(FuncDecl* d);

    /**
     * @brief Class type register करो और सभी member declarations analyze करो।
     *
     * @param d Analyze करने वाला class declaration।
     */
    void analyzeClassDecl(ClassDecl* d);

    /**
     * @brief Union type को environment में register करो।
     *
     * @param d Register करने वाला union declaration।
     */
    void analyzeUnionDecl(UnionDecl* d);

    /**
     * @brief Type environment में type alias register करो।
     *
     * @param d Register करने वाला typedef declaration।
     */
    void analyzeTypedefDecl(TypedefDecl* d);

    /**
     * @brief Wrapped inner declaration के लिए analyzeDecl पर delegate करो।
     *
     * @param d Inner decl wrap करने वाला extern declaration।
     */
    void analyzeExternDecl(ExternDecl* d);

    /**
     * @brief Scope push करो, हर statement analyze करो, फिर scope pop करो।
     *
     * @param s Analyze करने वाला compound statement।
     */
    void analyzeCompoundStmt(CompoundStmt* s);

    /**
     * @brief Condition, then-branch, और optional else-branch expressions analyze करो।
     *
     * @param s Analyze करने वाला if statement।
     */
    void analyzeIfStmt(IfStmt* s);

    /**
     * @brief Body analyze करने से पहले for-loop header variables के लिए scope push करो।
     *
     * @param s Analyze करने वाला for statement।
     */
    void analyzeForStmt(ForStmt* s);

    /**
     * @brief While condition और loop body analyze करो।
     *
     * @param s Analyze करने वाला while statement।
     */
    void analyzeWhileStmt(WhileStmt* s);

    /**
     * @brief Do-while body और trailing condition analyze करो।
     *
     * @param s Analyze करने वाला do-while statement।
     */
    void analyzeDoWhileStmt(DoWhileStmt* s);

    /**
     * @brief Switch discriminant और सभी case arms analyze करो।
     *
     * @param s Analyze करने वाला switch statement।
     */
    void analyzeSwitchStmt(SwitchStmt* s);

    /**
     * @brief Check करो कि return value enclosing function के return type से compatible है।
     *
     * @param s Analyze करने वाला return statement।
     */
    void analyzeReturnStmt(ReturnStmt* s);

    /**
     * @brief Declaration statement में wrapped declaration analyze करो।
     *
     * @param s Analyze करने वाला declaration statement।
     */
    void analyzeDeclStmt(DeclStmt* s);

    /**
     * @brief Expression statement में expression type-check करो।
     *
     * @param s Analyze करने वाला expression statement।
     */
    void analyzeExprStmt(ExprStmt* s);

    /**
     * @brief Label से attached statement analyze करो।
     *
     * @param s Analyze करने वाला label statement।
     */
    void analyzeLabelStmt(LabelStmt* s);

    /**
     * @brief Catch scope में __except_code inject करो।
     *
     * @param s Analyze करने वाला try-catch statement।
     */
    void analyzeTryCatchStmt(TryCatchStmt* s);

    /**
     * @brief Format expression और सभी interpolated arguments analyze करो।
     *
     * @param s Analyze करने वाला string-output statement।
     */
    void analyzeStringOutputStmt(StringOutputStmt* s);

    /**
     * @brief Binary expression type-check करो और उसका result type लौटाओ।
     *
     * @param e Check करने वाला binary expression।
     * @return Binary operation का result type।
     */
    Type* checkBinaryOp(BinaryExpr* e);

    /**
     * @brief Unary expression type-check करो और उसका result type लौटाओ।
     *
     * @param e Check करने वाला unary expression।
     * @return Unary operation का result type।
     */
    Type* checkUnaryOp(UnaryExpr* e);

    /**
     * @brief Call के लिए argument count और types validate करो; callee का return type लौटाओ।
     *
     * @param e Check करने वाला call expression।
     * @return Called function का return type, या callee type unknown हो तो I64।
     */
    Type* checkCall(CallExpr* e);

    /**
     * @brief Usual arithmetic conversions के तहत दो types में से wider लौटाओ।
     *
     * Float integer को beat करता है; F64, F32 को beat करता है; equal rank पर unsigned जीतता है।
     *
     * @param a पहला type operand।
     * @param b दूसरा type operand।
     * @return Promoted result type।
     */
    Type* promoteType(Type* a, Type* b);

    /**
     * @brief True लौटाओ अगर source implicitly target में convertible है।
     *
     * @param target वो type जिसमें assign हो रहा है।
     * @param source assign हो रही value का type।
     */
    bool isAssignable(Type* target, Type* source);

    /**
     * @brief True लौटाओ अगर 'from' implicitly 'to' में convert हो सकता है।
     *
     * @param from Source type।
     * @param to Destination type।
     */
    bool isImplicitlyConvertible(Type* from, Type* to);

    /**
     * @brief दिए गए kind के लिए cached singleton primitive type लौटाओ।
     *
     * @param kind Primitive type kind।
     * @return Canonical Type object का pointer; Arena का owner है।
     */
    Type* makePrimType(PrimKind kind);

    /**
     * @brief Pointee को wrap करने वाला single-star pointer type allocate करो।
     *
     * @param pointee Pointed-to type।
     * @return stars == 1 वाला नया pointer type।
     */
    Type* makePointerType(Type* pointee);

    /**
     * @brief दिए गए indirection depth के साथ pointer type allocate करो।
     *
     * @param pointee Pointed-to base type।
     * @param stars Pointer indirections की संख्या (कम से कम 1 clamped)।
     * @return Specified star count वाला नया pointer type।
     */
    Type* makePointerType(Type* pointee, int stars);

    /**
     * @brief Global environment में सभी built-in functions pre-declare करो।
     */
    void registerBuiltins();

    /**
     * @brief Comparisons और arithmetic के लिए integer promotion rank लौटाओ।
     *
     * @param k Rank करने वाला primitive kind।
     * @return [0, 4] range में rank value, या non-integer kinds के लिए -1।
     */
    static int intRank(PrimKind k);

    Diagnostics& diag_;
    Arena& arena_;
    Env env_;
    FuncDecl* current_func_ = nullptr;
    ClassDecl* current_class_ = nullptr;

    Type* ty_u0_   = nullptr;
    Type* ty_u8_   = nullptr;
    Type* ty_i8_   = nullptr;
    Type* ty_u16_  = nullptr;
    Type* ty_i16_  = nullptr;
    Type* ty_u32_  = nullptr;
    Type* ty_i32_  = nullptr;
    Type* ty_u64_  = nullptr;
    Type* ty_i64_  = nullptr;
    Type* ty_f64_  = nullptr;
    Type* ty_f32_  = nullptr;
    Type* ty_bool_ = nullptr;
    Type* ty_u8ptr_ = nullptr; // U8*
    ClassDecl* last_class_decl_ = nullptr;

    std::set<const Expr*> resolving_typeof_; // typeof() के लिए cycle guard

    Type* prim_type_cache_[13] = {}; // static_cast<int>(PrimKind) से index होता है
};

} // namespace holyc
