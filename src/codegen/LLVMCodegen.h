#pragma once

#ifdef HCC_HAS_LLVM

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "ast/AST.h"
#include "ast/TranslationUnit.h"
#include "support/Diagnostics.h"

namespace holyc {

struct BitFieldInfo {
    int bit_start = 0;
    int bit_width = -1; // -1 = bit field नहीं है
};

/**
 * @brief HolyC translation units के लिए LLVM IR code generator।
 */
class LLVMCodegen {
public:
    LLVMCodegen(Diagnostics& diag, bool emitDebugInfo = false);

    /**
     * @brief दिए गए translation unit के लिए LLVM module generate करो।
     *
     * Internal caches clear करता है, सभी runtime builtins declare करता है,
     * forward pass में top-level declarations register करता है, फिर हर node lower करता है।
     *
     * @param tu Lower करने वाला translation unit। Null नहीं होना चाहिए।
     * @param filename Debug info के लिए source file name; default "input.HC"।
     * @return JIT या AOT compilation के लिए ready completed LLVM module।
     */
    std::unique_ptr<llvm::Module> generate(TranslationUnit* tu, const std::string& filename = "");

    /**
     * @brief इस codegen instance के owned LLVM context का reference लौटाओ।
     */
    llvm::LLVMContext& getContext() { return *ctx_; }

    /**
     * @brief इस codegen instance से LLVM context का ownership transfer करो।
     *
     * @return Context का unique_ptr; इसके बाद codegen instance use नहीं होना चाहिए।
     */
    std::unique_ptr<llvm::LLVMContext> takeContext() { return std::move(ctx_); }

private:
    std::unique_ptr<llvm::LLVMContext> ctx_;
    std::unique_ptr<llvm::Module> mod_;
    llvm::IRBuilder<> builder_;
    Diagnostics& diag_;

    bool debugInfo_ = false;
    std::unique_ptr<llvm::DIBuilder> DI_;
    llvm::DICompileUnit* diCU_ = nullptr;
    llvm::DIFile* diFile_ = nullptr;
    std::vector<llvm::DIScope*> diScopeStack_;

    /**
     * @brief HolyC type को corresponding LLVM debug info type में map करो।
     *
     * @param ty Convert करने वाला HolyC type।
     * @return Debug metadata में use के लिए DIType*, या void types के लिए nullptr।
     */
    llvm::DIType* toDIType(const Type* ty);

    /**
     * @brief IRBuilder की debug location को node n की source position पर set करो।
     *
     * @param n वह AST node जिसकी source location emit करनी है।
     */
    void emitLocation(Node* n);

    std::unordered_map<std::string, llvm::Function*> functions_;
    std::unordered_map<std::string, llvm::GlobalVariable*> globals_;
    std::unordered_map<std::string, llvm::Value*> locals_; // locals के लिए AllocaInst*, static locals के लिए GlobalVariable*

    std::vector<llvm::BasicBlock*> breakTargets_;
    std::vector<llvm::BasicBlock*> continueTargets_;

    std::unordered_map<std::string, FuncDecl*> funcDecls_;

    std::unordered_set<std::string> holyc_vararg_funcs_; // TLS buffer use करते हैं, LLVM vararg ABI नहीं

    std::unordered_map<std::string, ClassDecl*> classDecls_;
    ClassDecl* lastClassDecl_ = nullptr;

    std::unordered_map<std::string, size_t> structSizeCache_;
    std::unordered_map<std::string, size_t> fieldOffsetCache_; // key: "ClassName::fieldName"

    std::unordered_map<const Type*, llvm::Type*> llvmTypeCache_; // हर generate() call पर clear होता है

    std::unordered_map<std::string, int64_t> enumConsts_;

    std::unordered_map<std::string, llvm::BasicBlock*> labelBlocks_;

    llvm::Function* currentFunc_ = nullptr;
    ClassDecl* currentMethodClass_ = nullptr;

    llvm::Type* i1Ty_   = nullptr;
    llvm::Type* i8Ty_   = nullptr;
    llvm::Type* i16Ty_  = nullptr;
    llvm::Type* i32Ty_  = nullptr;
    llvm::Type* i64Ty_  = nullptr;
    llvm::Type* f32Ty_  = nullptr;
    llvm::Type* f64Ty_  = nullptr;
    llvm::Type* voidTy_ = nullptr;
    llvm::Type* ptrTy_  = nullptr;

    llvm::Function* fn_holyc_print_ = nullptr;
    llvm::Function* fn_malloc_      = nullptr;
    llvm::Function* fn_calloc_      = nullptr;
    llvm::Function* fn_free_        = nullptr;
    llvm::Function* fn_realloc_     = nullptr;
    llvm::Function* fn_va_store_    = nullptr;
    llvm::Function* fn_va_count_    = nullptr;
    llvm::Function* fn_va_get_      = nullptr;

    /**
     * @brief HolyC type को उसके LLVM IR equivalent में lower करो, caching के साथ।
     *
     * Results llvmTypeCache_ में store होते हैं और single generate() invocation में
     * same Type* pointer के subsequent calls के लिए reuse होते हैं।
     *
     * @param ty Convert करने वाला HolyC type। Null हो तो default i64।
     * @return Corresponding llvm::Type*, LLVM context का owner।
     */
    llvm::Type* toLLVMType(const Type* ty);

    /**
     * @brief Primitive kind को directly उसके LLVM type में map करो (caching नहीं)।
     *
     * @param pk Convert करने वाला primitive kind।
     * @return Corresponding llvm::Type*।
     */
    llvm::Type* primToLLVM(PrimKind pk);

    /**
     * @brief Expression को LLVM Value में lower करो।
     *
     * @param e Lower करने वाला expression। Null हो तो constant zero लौटाता है।
     * @return Expression की rvalue represent करने वाला LLVM Value*।
     */
    llvm::Value* lowerExpr(Expr* e);

    /**
     * @brief Lvalue expression का address (alloca/GEP) लौटाओ, या nullptr।
     *
     * @param e Address लेने वाला lvalue expression।
     * @return Load/store के लिए suitable pointer-typed Value*, या addressable न हो तो nullptr।
     */
    llvm::Value* lowerExprAddr(Expr* e);

    /**
     * @brief Statement को LLVM IR में lower करो।
     *
     * @param s Lower करने वाला statement। Null हो या current block में already
     *          terminator हो तो no-op।
     */
    void lowerStmt(Stmt* s);

    /**
     * @brief Top-level declaration lower करो (function, global variable, enum, class)।
     *
     * @param d Lower करने वाला top-level AST node।
     */
    void lowerTopLevel(Node* d);

    /**
     * @brief mem2reg complications से बचने के लिए function entry block में alloca insert करो।
     *
     * Function entry पर सभी allocas रखने से mem2reg उन्हें promote कर सकता है।
     *
     * @param fn वह function जिसके entry block में alloca जाएगी।
     * @param name Alloca का debug name।
     * @param ty Allocate करने वाला LLVM type।
     * @return Created AllocaInst*।
     */
    llvm::AllocaInst* createEntryAlloca(llvm::Function* fn,
                                         const std::string& name,
                                         llvm::Type* ty);

    /**
     * @brief Module में सभी runtime builtin functions declare करो।
     *
     * हर HolyC built-in के लिए external-linkage Function declarations create करता है,
     * functions_ map populate करता है, और frequently used functions को dedicated
     * member pointers (fn_holyc_print_, fn_va_store_, आदि) में cache करता है।
     */
    void declareBuiltins();

    /**
     * @brief HolyC string-output statement को __holyc_print call में lower करो।
     *
     * 64 bits से narrow integer arguments और float arguments को variadic arguments
     * के रूप में __holyc_print को pass करने से पहले double में promote करता है।
     *
     * @param s Lower करने वाला string-output statement।
     * @return __holyc_print call का CallInst*, या format न हो तो nullptr।
     */
    llvm::Value* lowerStringOutput(StringOutputStmt* s);

    /**
     * @brief किसी भी scalar value को i1 में convert करो (integer, float, या pointer zero से comparison)।
     *
     * @param v Boolean में convert करने वाली value।
     * @return i1 Value* जो v non-zero/non-null हो तो 1 होता है।
     */
    llvm::Value* toBool(llvm::Value* v);

    /**
     * @brief True लौटाओ अगर दिया गया type signed integer है।
     *
     * @param ty Query करने वाला type।
     */
    bool isSigned(const Type* ty);

    /**
     * @brief True लौटाओ अगर दिया गया type floating-point type है।
     *
     * @param ty Query करने वाला type।
     */
    bool isFloat(const Type* ty);

    /**
     * @brief True लौटाओ अगर दिया गया type pointer type है।
     *
     * @param ty Query करने वाला type।
     */
    bool isPointer(const Type* ty);

    /**
     * @brief दिए गए expression के resolved type के लिए LLVM type लौटाओ, default i64।
     *
     * @param e वह expression जिसका resolved_type conversion के लिए use होता है।
     * @return Corresponding llvm::Type*, या expression का resolved type न हो तो i64।
     */
    llvm::Type* getExprLLVMType(Expr* e);

    /**
     * @brief Shared pre/post increment/decrement implementation।
     *
     * Integer arithmetic increment/decrement और pointer GEP-based stepping दोनों
     * handle करता है। Post variants के लिए pre-modification value और pre variants के
     * लिए post-modification value लौटाता है।
     *
     * @param isInc Increment के लिए True, decrement के लिए false।
     * @param isPre New value लौटाने के लिए True, old value के लिए false।
     * @param un Operand contain करने वाला unary expression node।
     * @return Resulting LLVM Value* (isPre के हिसाब से old या new)।
     */
    llvm::Value* emitIncDec(bool isInc, bool isPre, UnaryExpr* un);

    /**
     * @brief Class का total byte size लौटाओ, inherited base fields सहित।
     *
     * Results class name से key करके structSizeCache_ में cache होते हैं।
     *
     * @param class_name जिस class का size चाहिए उसका name।
     * @return Total size bytes में, या class न मिले तो 8।
     */
    size_t computeStructSize(const std::string& class_name);

    /**
     * @brief Class में named field का byte offset लौटाओ, inheritance handle करते हुए।
     *
     * पहले base class search करता है, फिर derived class। Results "ClassName::fieldName"
     * से key करके fieldOffsetCache_ में cache होते हैं।
     *
     * @param class_name Field contain करने वाली class का name।
     * @param field_name Locate करने वाले field का name।
     * @return Struct की शुरुआत से byte offset, या न मिले तो 0।
     */
    size_t computeFieldOffset(const std::string& class_name, const std::string& field_name);

    /**
     * @brief Named field के लिए bit-field layout info लौटाओ (read/write masking के लिए)।
     *
     * @param class_name Field contain करने वाली class का name।
     * @param field_name Locate करने वाले bit-field का name।
     * @return bit_start और bit_width वाला BitFieldInfo; bit field न हो तो bit_width == -1।
     */
    BitFieldInfo getBitFieldInfo(const std::string& class_name, const std::string& field_name);
};

} // namespace holyc

#endif // HCC_HAS_LLVM
