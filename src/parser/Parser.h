#pragma once

#include "ast/AST.h"
#include "ast/TranslationUnit.h"
#include "preprocessor/Preprocessor.h"
#include "interpreter/Interpreter.h"
#include "support/Arena.h"
#include "support/Diagnostics.h"

#include <functional>
#include <set>
#include <string>

namespace holyc {

/**
 * @brief HolyC source के लिए recursive-descent parser।
 */
class Parser {
public:
    /**
     * @brief दिए गए preprocessor, diagnostics, और arena से parser बनाओ।
     *
     * @param pp   Token supply करने वाला preprocessor।
     * @param diag Errors और warnings के लिए diagnostics sink।
     * @param arena सभी AST nodes के लिए arena allocator।
     */
    Parser(Preprocessor& pp, Diagnostics& diag, Arena& arena);

    /**
     * @brief पूरा translation unit parse करो।
     *
     * @return सभी top-level declarations वाला parsed TranslationUnit।
     */
    TranslationUnit* parse();

private:
    /**
     * @brief बिना consume किए दिए गए positions आगे का token लौटाओ।
     *
     * @param ahead Lookahead buffer में आगे की दूरी; 0 current token है।
     * @return Requested lookahead position का token।
     */
    Token peek(int ahead = 0);

    /**
     * @brief Current token को consume करके lookahead buffer भरो।
     *
     * @return Consume किया गया token।
     */
    Token consume();

    /**
     * @brief Current token consume करो, expected kind नहीं मिला तो error emit करो।
     *
     * @param kind Expected token kind।
     * @return Consumed token, या mismatch पर error token।
     */
    Token expect(TokenKind kind);

    /**
     * @brief Current token दिए गए kind से match करे तो consume करके true लौटाओ।
     *
     * @param kind Match करने वाला token kind।
     * @return True जब token match हुआ और consume किया; वरना false।
     */
    bool match(TokenKind kind);
    bool check(TokenKind kind);

    /**
     * @brief Parse error के बाद safe resynchronisation point तक tokens skip करो।
     */
    void synchronize();

    /**
     * @brief Type specifier parse करो, pointer stars और array dimensions सहित।
     *
     * @return Parsed Type node।
     */
    Type* parseType();

    /**
     * @brief True लौटाओ अगर current token type specifier शुरू कर सकता है।
     */
    bool isTypeStart();

    /**
     * @brief पूरा expression parse करो (entry point)।
     *
     * @return Parsed expression node।
     */
    Expr* parseExpr();

    /**
     * @brief Assignment या compound-assignment expression parse करो (right-associative)।
     *
     * @return Parsed expression node।
     */
    Expr* parseAssignment();

    /**
     * @brief Ternary conditional expression parse करो।
     *
     * @return Parsed expression node।
     */
    Expr* parseTernary();
    Expr* parseLogicalOr();
    Expr* parseLogicalXor();
    Expr* parseLogicalAnd();

    /**
     * @brief Shift expression parse करो (HolyC में comparison से कम precedence)।
     *
     * @return Parsed expression node।
     */
    Expr* parseShift();

    /**
     * @brief Comparison expression parse करो, chained comparisons के लिए ChainedCmpExpr बनाओ।
     *
     * @return Parsed expression node।
     */
    Expr* parseComparison();
    Expr* parseAdditive();

    /**
     * @brief Bitwise-OR expression parse करो (HolyC में +/- से ज़्यादा precedence)।
     *
     * @return Parsed expression node।
     */
    Expr* parseBitOr();
    Expr* parseBitXor();
    Expr* parseBitAnd();
    Expr* parseMultiplicative();

    /**
     * @brief Power expression parse करो backtick operator से (right-associative)।
     *
     * @return Parsed expression node।
     */
    Expr* parsePower();

    /**
     * @brief Unary prefix expression parse करो।
     *
     * @return Parsed expression node।
     */
    Expr* parseUnary();

    /**
     * @brief Postfix operations parse करो: calls, subscripts, field access, casts, ++/--।
     *
     * @param base पहले से parsed base expression जिसपर postfix लगाना है।
     * @return सभी postfix operations के बाद result expression node।
     */
    Expr* parsePostfix();

    /**
     * @brief Primary expression parse करो: literal, identifier, parenthesised expr, sizeof, etc.
     *
     * @return Parsed expression node।
     */
    Expr* parsePrimary();

    /**
     * @brief Single statement parse करो, current token के हिसाब से dispatch करो।
     *
     * @return Parsed statement node।
     */
    Stmt* parseStmt();

    /**
     * @brief Brace-enclosed statements का block parse करो।
     *
     * @return Parsed compound statement node।
     */
    CompoundStmt* parseCompoundStmt();
    Stmt* parseIfStmt();
    Stmt* parseForStmt();
    Stmt* parseWhileStmt();
    Stmt* parseDoWhileStmt();

    /**
     * @brief Switch statement parse करो, case/default arms और optional range syntax सहित।
     *
     * @return Parsed switch statement node।
     */
    Stmt* parseSwitchStmt();
    Stmt* parseReturnStmt();
    Stmt* parseGotoStmt();
    Stmt* parseTryCatchStmt();

    /**
     * @brief Asm statement parse करो; raw-token और constraint दोनों forms support करता है।
     *
     * @return Parsed asm statement node।
     */
    Stmt* parseAsmStmt();

    /**
     * @brief Exe block parse करो, parse time पर execute करो, और emitted tokens inject करो।
     *
     * @return Parsed statement node (injection के बाद typically discard होता है)।
     */
    Stmt* parseExeStmt();

    /**
     * @brief Top-level या local declaration parse करो, linkage/storage modifiers consume करके।
     *
     * @return Parsed declaration node, या error emit हुई तो nullptr।
     */
    Decl* parseDecl();

    /**
     * @brief Type और name जानने के बाद variable या function declaration finish करो।
     *
     * @param type     पहले से parsed type।
     * @param name     पहले से parsed declarator name।
     * @param storage  Declaration का storage class।
     * @param linkage  Declaration का linkage kind।
     * @param flags    Function-specific flags (interrupt, argpop, etc.)।
     * @param no_warn  no_warn modifier था तो true।
     * @return Complete declaration node।
     */
    Decl* parseVarOrFuncDecl(Type* type, const std::string& name, Storage storage,
                             Linkage linkage, FuncFlags flags, bool no_warn);

    /**
     * @brief Function declaration parse करो: parameter list और optional body।
     *
     * @param return_type Declared return type।
     * @param name        Function name।
     * @param linkage     Function का linkage kind।
     * @param flags       Function-specific flags।
     * @return Parsed FuncDecl node।
     */
    FuncDecl* parseFuncDecl(Type* return_type, const std::string& name,
                            Linkage linkage, FuncFlags flags);

    /**
     * @brief Class declaration parse करो, fields, methods, और anonymous unions सहित।
     *
     * @return Parsed ClassDecl node।
     */
    ClassDecl* parseClassDecl();
    UnionDecl* parseUnionDecl();
    EnumDecl* parseEnumDecl();

    /**
     * @brief True लौटाओ अगर current token declaration शुरू करता है, expression नहीं।
     */
    bool isDeclaration();
    bool isComparisonOp(TokenKind k);
    bool isAssignmentOp(TokenKind k);

    /**
     * @brief Assignment-operator token को उसके BinOpKind में map करो।
     *
     * @param k Assignment operator token kind।
     * @return Corresponding binary operation kind।
     */
    BinOpKind tokenToAssignOp(TokenKind k);

    /**
     * @brief Comparison-operator token को उसके BinOpKind में map करो।
     *
     * @param k Comparison operator token kind।
     * @return Corresponding binary operation kind।
     */
    BinOpKind tokenToCmpOp(TokenKind k);

    /**
     * @brief True लौटाओ अगर दिया गया token kind primitive type keywords में से एक है।
     *
     * @param k Test करने वाला token kind।
     */
    bool isTypeToken(TokenKind k);

    /**
     * @brief Parse time पर exe block body execute करो और Emit output को tokens की तरह inject करो।
     *
     * @param body Execute करने वाले exe block का compound statement body।
     */
    void executeExeBlock(CompoundStmt* body);

    Preprocessor& pp_;
    Diagnostics& diag_;
    Arena& arena_;
    Interpreter exe_interp_;
    Token current_;
    Token lookahead_[4];
    int la_count_ = 0;
    int la_head_  = 0;
    int parse_depth_ = 0;
    static constexpr int kMaxParseDepth = 1000;
    std::set<std::string, std::less<>> known_class_names_;
};

} // namespace holyc
