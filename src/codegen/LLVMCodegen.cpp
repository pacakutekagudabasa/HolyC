#ifdef HCC_HAS_LLVM

#include "codegen/LLVMCodegen.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace holyc {

/**
 * @brief LLVM context initialize करता है और common primitive LLVM types cache करता है।
 *
 * @param diag Code generation के दौरान errors के लिए Diagnostics sink।
 * @param emitDebugInfo अगर true हो तो module में DWARF debug information attach होगी।
 */
LLVMCodegen::LLVMCodegen(Diagnostics& diag, bool emitDebugInfo)
    : ctx_(std::make_unique<llvm::LLVMContext>()), builder_(*ctx_), diag_(diag),
      debugInfo_(emitDebugInfo) {
    llvm::LLVMContext& ctx = *ctx_;
    i1Ty_   = llvm::Type::getInt1Ty(ctx);
    i8Ty_   = llvm::Type::getInt8Ty(ctx);
    i16Ty_  = llvm::Type::getInt16Ty(ctx);
    i32Ty_  = llvm::Type::getInt32Ty(ctx);
    i64Ty_  = llvm::Type::getInt64Ty(ctx);
    f32Ty_  = llvm::Type::getFloatTy(ctx);
    f64Ty_  = llvm::Type::getDoubleTy(ctx);
    voidTy_ = llvm::Type::getVoidTy(ctx);
    ptrTy_  = llvm::PointerType::getUnqual(ctx);
}

/**
 * @brief HolyC type को corresponding LLVM debug info type पर map करता है।
 *
 * @param ty Convert करने वाला HolyC type।
 * @return Debug metadata में use के लिए DIType*, या void types के लिए अथवा DI_ null हो तो nullptr।
 */
llvm::DIType* LLVMCodegen::toDIType(const Type* ty) {
    if (!ty || !DI_) return nullptr;

    if (ty->kind == Type::Prim) {
        auto& p = std::get<PrimitiveType>(ty->data);
        switch (p.kind) {
        case PrimKind::U0:
        case PrimKind::I0:
            return nullptr;
        case PrimKind::Bool:
            return DI_->createBasicType("Bool", 8, llvm::dwarf::DW_ATE_boolean);
        case PrimKind::I8:
            return DI_->createBasicType("I8", 8, llvm::dwarf::DW_ATE_signed_char);
        case PrimKind::U8:
            return DI_->createBasicType("U8", 8, llvm::dwarf::DW_ATE_unsigned_char);
        case PrimKind::I16:
            return DI_->createBasicType("I16", 16, llvm::dwarf::DW_ATE_signed);
        case PrimKind::U16:
            return DI_->createBasicType("U16", 16, llvm::dwarf::DW_ATE_unsigned);
        case PrimKind::I32:
            return DI_->createBasicType("I32", 32, llvm::dwarf::DW_ATE_signed);
        case PrimKind::U32:
            return DI_->createBasicType("U32", 32, llvm::dwarf::DW_ATE_unsigned);
        case PrimKind::I64:
            return DI_->createBasicType("I64", 64, llvm::dwarf::DW_ATE_signed);
        case PrimKind::U64:
            return DI_->createBasicType("U64", 64, llvm::dwarf::DW_ATE_unsigned);
        case PrimKind::F64:
            return DI_->createBasicType("F64", 64, llvm::dwarf::DW_ATE_float);
        case PrimKind::F32:
            return DI_->createBasicType("F32", 32, llvm::dwarf::DW_ATE_float);
        }
    }
    if (ty->kind == Type::Pointer) {
        return DI_->createPointerType(
            toDIType(std::get<PointerType>(ty->data).pointee), 64);
    }
    return DI_->createBasicType("I64", 64, llvm::dwarf::DW_ATE_signed);
}

void LLVMCodegen::emitLocation(Node* n) {
    if (!debugInfo_ || !n || diScopeStack_.empty()) return;
    auto& loc = n->loc;
    unsigned line = loc.line ? loc.line : 1;
    unsigned col = loc.col ? loc.col : 0;
    builder_.SetCurrentDebugLocation(
        llvm::DILocation::get(*ctx_, line, col, diScopeStack_.back()));
}

llvm::Type* LLVMCodegen::primToLLVM(PrimKind pk) {
    switch (pk) {
    case PrimKind::U0:
    case PrimKind::I0:
        return voidTy_;
    case PrimKind::Bool:
        return i1Ty_;
    case PrimKind::I8:
    case PrimKind::U8:
        return i8Ty_;
    case PrimKind::I16:
    case PrimKind::U16:
        return i16Ty_;
    case PrimKind::I32:
    case PrimKind::U32:
        return i32Ty_;
    case PrimKind::I64:
    case PrimKind::U64:
        return i64Ty_;
    case PrimKind::F64:
        return f64Ty_;
    case PrimKind::F32:
        return f32Ty_;
    }
    return i64Ty_;
}

llvm::Type* LLVMCodegen::toLLVMType(const Type* ty) {
    if (!ty) return i64Ty_;

    auto it = llvmTypeCache_.find(ty);
    if (it != llvmTypeCache_.end()) return it->second;

    llvm::Type* result;
    switch (ty->kind) {
    case Type::Prim:
        result = primToLLVM(std::get<PrimitiveType>(ty->data).kind);
        break;
    case Type::IntrinsicUnion:
        result = i64Ty_;
        break;
    case Type::Pointer:
        result = ptrTy_;
        break;
    case Type::Array: {
        auto& a = std::get<ArrayType>(ty->data);
        llvm::Type* elem = toLLVMType(a.element);
        int sz = a.size > 0 ? a.size : 1;
        result = llvm::ArrayType::get(elem, sz);
        break;
    }
    case Type::Func: {
        auto& f = std::get<FuncType>(ty->data);
        llvm::Type* ret = toLLVMType(f.return_type);
        if (ret->isVoidTy()) { /* ok */ }
        std::vector<llvm::Type*> params;
        for (auto* pt : f.param_types)
            params.push_back(toLLVMType(pt));
        result = llvm::FunctionType::get(ret, params, f.is_vararg);
        break;
    }
    case Type::Class:
    case Type::Union:
        result = ptrTy_;
        break;
    case Type::Typeof:
        result = i64Ty_;
        break;
    default:
        result = i64Ty_;
        break;
    }
    llvmTypeCache_[ty] = result;
    return result;
}

bool LLVMCodegen::isSigned(const Type* ty) {
    return ty && ty->isSigned();
}

bool LLVMCodegen::isFloat(const Type* ty) {
    return ty && ty->isFloat();
}

bool LLVMCodegen::isPointer(const Type* ty) {
    return ty && ty->isPointer();
}

llvm::Type* LLVMCodegen::getExprLLVMType(Expr* e) {
    if (e && e->resolved_type)
        return toLLVMType(e->resolved_type);
    return i64Ty_;
}

llvm::AllocaInst* LLVMCodegen::createEntryAlloca(llvm::Function* fn,
                                                   const std::string& name,
                                                   llvm::Type* ty) {
    llvm::IRBuilder<> tmpB(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmpB.CreateAlloca(ty, nullptr, name);
}

llvm::Value* LLVMCodegen::toBool(llvm::Value* v) {
    if (v->getType()->isIntegerTy(1)) return v;
    if (v->getType()->isIntegerTy())
        return builder_.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0), "tobool");
    if (v->getType()->isFloatingPointTy())
        return builder_.CreateFCmpONE(v, llvm::ConstantFP::get(v->getType(), 0.0), "tobool");
    if (v->getType()->isPointerTy()) {
        auto* intVal = builder_.CreatePtrToInt(v, i64Ty_);
        return builder_.CreateICmpNE(intVal, llvm::ConstantInt::get(i64Ty_, 0), "tobool");
    }
    return v;
}

/**
 * @brief Module में सभी runtime builtin functions declare करता है।
 *
 * हर HolyC built-in के लिए external-linkage Function declarations create करता है,
 * functions_ map populate करता है, और frequently used functions को
 * dedicated member pointers (fn_holyc_print_, fn_va_store_, आदि) में cache करता है।
 */
void LLVMCodegen::declareBuiltins() {
    auto* i32Ty = i32Ty_;
    auto* i64Ty = i64Ty_;
    auto* f64Ty = f64Ty_;
    auto* voidTy = voidTy_;
    auto* ptrTy = ptrTy_;

    auto* printTy = llvm::FunctionType::get(i32Ty, {ptrTy}, /*isVarArg=*/true);
    auto* printFn = llvm::Function::Create(
        printTy, llvm::Function::ExternalLinkage, "__holyc_print", mod_.get());
    functions_["__holyc_print"] = printFn;
    functions_["Print"] = printFn;
    fn_holyc_print_ = printFn;

    auto* printfFn = llvm::Function::Create(
        printTy, llvm::Function::ExternalLinkage, "printf", mod_.get());
    functions_["printf"] = printfFn;

    auto* mallocTy = llvm::FunctionType::get(ptrTy, {i64Ty}, false);
    functions_["MAlloc"] = llvm::Function::Create(
        mallocTy, llvm::Function::ExternalLinkage, "MAlloc", mod_.get());
    functions_["CAlloc"] = llvm::Function::Create(
        mallocTy, llvm::Function::ExternalLinkage, "CAlloc", mod_.get());

    auto* freeTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    functions_["Free"] = llvm::Function::Create(
        freeTy, llvm::Function::ExternalLinkage, "Free", mod_.get());

    auto* memcpyTy = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy, i64Ty}, false);
    functions_["MemCpy"] = llvm::Function::Create(
        memcpyTy, llvm::Function::ExternalLinkage, "MemCpy", mod_.get());

    auto* memsetTy = llvm::FunctionType::get(ptrTy, {ptrTy, i64Ty, i64Ty}, false);
    functions_["MemSet"] = llvm::Function::Create(
        memsetTy, llvm::Function::ExternalLinkage, "MemSet", mod_.get());

    auto* memcmpTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy, i64Ty}, false);
    functions_["MemCmp"] = llvm::Function::Create(
        memcmpTy, llvm::Function::ExternalLinkage, "MemCmp", mod_.get());

    auto* strlenTy = llvm::FunctionType::get(i64Ty, {ptrTy}, false);
    functions_["StrLen"] = llvm::Function::Create(
        strlenTy, llvm::Function::ExternalLinkage, "StrLen", mod_.get());

    auto* strcpyTy = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false);
    functions_["StrCpy"] = llvm::Function::Create(
        strcpyTy, llvm::Function::ExternalLinkage, "StrCpy", mod_.get());

    auto* strcmpTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy}, false);
    functions_["StrCmp"] = llvm::Function::Create(
        strcmpTy, llvm::Function::ExternalLinkage, "StrCmp", mod_.get());
    functions_["StrNCmp"] = llvm::Function::Create(
        memcmpTy, llvm::Function::ExternalLinkage, "StrNCmp", mod_.get());
    functions_["StrFind"] = llvm::Function::Create(
        strcpyTy, llvm::Function::ExternalLinkage, "StrFind", mod_.get());

    auto* bitopTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    functions_["Bsf"] = llvm::Function::Create(
        bitopTy, llvm::Function::ExternalLinkage, "Bsf", mod_.get());
    functions_["Bsr"] = llvm::Function::Create(
        bitopTy, llvm::Function::ExternalLinkage, "Bsr", mod_.get());
    functions_["BCnt"] = llvm::Function::Create(
        bitopTy, llvm::Function::ExternalLinkage, "BCnt", mod_.get());

    auto* putcharsTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    functions_["PutChars"] = llvm::Function::Create(
        putcharsTy, llvm::Function::ExternalLinkage, "PutChars", mod_.get());

    auto* throwTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    {
        auto* fn = llvm::Function::Create(
            throwTy, llvm::Function::ExternalLinkage, "__holyc_throw", mod_.get());
        fn->addFnAttr(llvm::Attribute::NoReturn);
        functions_["__holyc_throw"] = fn;
    }

    // setjmp returns_twice है — इसे directly call करना होगा, कभी भी wrapper के through नहीं।
    auto* setjmpTy = llvm::FunctionType::get(i32Ty_, {ptrTy}, false);
    {
        auto* fn = llvm::Function::Create(
            setjmpTy, llvm::Function::ExternalLinkage, "setjmp", mod_.get());
        fn->addFnAttr(llvm::Attribute::ReturnsTwice);
        functions_["setjmp"] = fn;
    }

    auto* tryPushTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    functions_["__holyc_try_push"] = llvm::Function::Create(
        tryPushTy, llvm::Function::ExternalLinkage, "__holyc_try_push", mod_.get());

    auto* tryPopTy = llvm::FunctionType::get(voidTy, {}, false);
    functions_["__holyc_try_pop"] = llvm::Function::Create(
        tryPopTy, llvm::Function::ExternalLinkage, "__holyc_try_pop", mod_.get());

    auto* exceptCodeTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["__holyc_except_code"] = llvm::Function::Create(
        exceptCodeTy, llvm::Function::ExternalLinkage, "__holyc_except_code", mod_.get());

    auto* ipowTy = llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false);
    functions_["__holyc_ipow"] = llvm::Function::Create(
        ipowTy, llvm::Function::ExternalLinkage, "__holyc_ipow", mod_.get());

    auto* d2dTy = llvm::FunctionType::get(f64Ty, {f64Ty}, false);
    auto* dd2dTy = llvm::FunctionType::get(f64Ty, {f64Ty, f64Ty}, false);
    const char* d2dNames[] = {"__holyc_sqrt", "__holyc_abs_f64", "__holyc_sin",
                               "__holyc_cos", "__holyc_tan", "__holyc_log2",
                               "__holyc_log10", "__holyc_ceil", "__holyc_floor",
                               "__holyc_round"};
    for (const char* name : d2dNames) {
        functions_[name] = llvm::Function::Create(
            d2dTy, llvm::Function::ExternalLinkage, name, mod_.get());
    }
    functions_["__holyc_pow"] = llvm::Function::Create(
        dd2dTy, llvm::Function::ExternalLinkage, "__holyc_pow", mod_.get());

    functions_["Sqrt"]  = functions_["__holyc_sqrt"];
    functions_["Sin"]   = functions_["__holyc_sin"];
    functions_["Cos"]   = functions_["__holyc_cos"];
    functions_["Tan"]   = functions_["__holyc_tan"];
    functions_["Log2"]  = functions_["__holyc_log2"];
    functions_["Log10"] = functions_["__holyc_log10"];
    functions_["Ceil"]  = functions_["__holyc_ceil"];
    functions_["Floor"] = functions_["__holyc_floor"];
    functions_["Round"] = functions_["__holyc_round"];
    functions_["Pow"]   = functions_["__holyc_pow"];

    functions_["__holyc_abs_i64"] = llvm::Function::Create(
        bitopTy, llvm::Function::ExternalLinkage, "__holyc_abs_i64", mod_.get());
    functions_["Abs"] = functions_["__holyc_abs_i64"];

    auto* clampTy = llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty, i64Ty}, false);
    functions_["__holyc_clamp"] = llvm::Function::Create(
        clampTy, llvm::Function::ExternalLinkage, "__holyc_clamp", mod_.get());
    functions_["Clamp"] = functions_["__holyc_clamp"];

    functions_["__holyc_min"] = llvm::Function::Create(
        ipowTy, llvm::Function::ExternalLinkage, "__holyc_min", mod_.get());
    functions_["__holyc_max"] = llvm::Function::Create(
        ipowTy, llvm::Function::ExternalLinkage, "__holyc_max", mod_.get());
    functions_["__holyc_sign"] = llvm::Function::Create(
        bitopTy, llvm::Function::ExternalLinkage, "__holyc_sign", mod_.get());
    functions_["Min"]  = functions_["__holyc_min"];
    functions_["Max"]  = functions_["__holyc_max"];
    functions_["Sign"] = functions_["__holyc_sign"];

    auto* exitTy = llvm::FunctionType::get(voidTy, {i32Ty}, false);
    auto* exitFn = llvm::Function::Create(
        exitTy, llvm::Function::ExternalLinkage, "exit", mod_.get());
    functions_["exit"] = exitFn;
    functions_["Exit"] = exitFn;

    auto* putsTy = llvm::FunctionType::get(i32Ty, {ptrTy}, false);
    functions_["puts"] = llvm::Function::Create(
        putsTy, llvm::Function::ExternalLinkage, "puts", mod_.get());

    auto* mstrprintTy = llvm::FunctionType::get(ptrTy, {ptrTy}, /*isVarArg=*/true);
    functions_["MStrPrint"] = llvm::Function::Create(
        mstrprintTy, llvm::Function::ExternalLinkage, "MStrPrint", mod_.get());

    functions_["StrPrintf"] = llvm::Function::Create(
        mstrprintTy, llvm::Function::ExternalLinkage, "StrPrintf", mod_.get());

    auto* catprintTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy}, /*isVarArg=*/true);
    functions_["CatPrint"] = llvm::Function::Create(
        catprintTy, llvm::Function::ExternalLinkage, "CatPrint", mod_.get());

    auto* strcatTy = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false);
    functions_["StrCat"] = llvm::Function::Create(
        strcatTy, llvm::Function::ExternalLinkage, "StrCat", mod_.get());

    auto* strNTy = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy, i64Ty}, false);
    functions_["StrCpyN"] = llvm::Function::Create(
        strNTy, llvm::Function::ExternalLinkage, "StrCpyN", mod_.get());
    functions_["StrCatN"] = llvm::Function::Create(
        strNTy, llvm::Function::ExternalLinkage, "StrCatN", mod_.get());

    auto* stricmpTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy}, false);
    functions_["StrICmp"] = llvm::Function::Create(
        stricmpTy, llvm::Function::ExternalLinkage, "StrICmp", mod_.get());

    auto* str2i64Ty = llvm::FunctionType::get(i64Ty, {ptrTy}, false);
    functions_["Str2I64"] = llvm::Function::Create(
        str2i64Ty, llvm::Function::ExternalLinkage, "Str2I64", mod_.get());

    auto* str2f64Ty = llvm::FunctionType::get(f64Ty, {ptrTy}, false);
    functions_["Str2F64"] = llvm::Function::Create(
        str2f64Ty, llvm::Function::ExternalLinkage, "Str2F64", mod_.get());

    auto* wildmatchTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy}, false);
    functions_["WildMatch"] = llvm::Function::Create(
        wildmatchTy, llvm::Function::ExternalLinkage, "WildMatch", mod_.get());
    functions_["StrMatch"] = llvm::Function::Create(
        wildmatchTy, llvm::Function::ExternalLinkage, "StrMatch", mod_.get());

    auto* d2dTy2 = llvm::FunctionType::get(f64Ty, {f64Ty}, false);
    auto* dd2dTy2 = llvm::FunctionType::get(f64Ty, {f64Ty, f64Ty}, false);
    functions_["__holyc_atan"]  = llvm::Function::Create(
        d2dTy2, llvm::Function::ExternalLinkage, "__holyc_atan", mod_.get());
    functions_["ATan"] = functions_["__holyc_atan"];
    functions_["__holyc_atan2"] = llvm::Function::Create(
        dd2dTy2, llvm::Function::ExternalLinkage, "__holyc_atan2", mod_.get());
    functions_["ATan2"] = functions_["__holyc_atan2"];
    functions_["__holyc_exp"]   = llvm::Function::Create(
        d2dTy2, llvm::Function::ExternalLinkage, "__holyc_exp", mod_.get());
    functions_["Exp"] = functions_["__holyc_exp"];
    functions_["__holyc_log"]   = llvm::Function::Create(
        d2dTy2, llvm::Function::ExternalLinkage, "__holyc_log", mod_.get());
    functions_["Log"] = functions_["__holyc_log"];

    auto* randu64Ty = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["RandU64"] = llvm::Function::Create(
        randu64Ty, llvm::Function::ExternalLinkage, "RandU64", mod_.get());
    auto* seedrandTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    functions_["SeedRand"] = llvm::Function::Create(
        seedrandTy, llvm::Function::ExternalLinkage, "SeedRand", mod_.get());

    auto* toI64Ty = llvm::FunctionType::get(i64Ty, {f64Ty}, false);
    functions_["ToI64"] = llvm::Function::Create(
        toI64Ty, llvm::Function::ExternalLinkage, "ToI64", mod_.get());
    auto* toF64Ty = llvm::FunctionType::get(f64Ty, {i64Ty}, false);
    functions_["ToF64"] = llvm::Function::Create(
        toF64Ty, llvm::Function::ExternalLinkage, "ToF64", mod_.get());

    auto* bitopTy2 = llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false);
    functions_["Bt"] = llvm::Function::Create(
        bitopTy2, llvm::Function::ExternalLinkage, "Bt", mod_.get());
    auto* bitsetTy = llvm::FunctionType::get(i64Ty, {ptrTy, i64Ty}, false);
    functions_["Bts"] = llvm::Function::Create(
        bitsetTy, llvm::Function::ExternalLinkage, "Bts", mod_.get());
    functions_["Btr"] = llvm::Function::Create(
        bitsetTy, llvm::Function::ExternalLinkage, "Btr", mod_.get());
    functions_["Btc"] = llvm::Function::Create(
        bitsetTy, llvm::Function::ExternalLinkage, "Btc", mod_.get());
    auto* bfieldTy = llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty, i64Ty}, false);
    functions_["BFieldExtU32"] = llvm::Function::Create(
        bfieldTy, llvm::Function::ExternalLinkage, "BFieldExtU32", mod_.get());

    auto* reallocTy = llvm::FunctionType::get(ptrTy, {ptrTy, i64Ty}, false);
    functions_["ReAlloc"] = llvm::Function::Create(
        reallocTy, llvm::Function::ExternalLinkage, "ReAlloc", mod_.get());
    auto* msizeTy = llvm::FunctionType::get(i64Ty, {ptrTy}, false);
    functions_["MSize"] = llvm::Function::Create(
        msizeTy, llvm::Function::ExternalLinkage, "MSize", mod_.get());
    auto* mallocidentTy = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
    functions_["MAllocIdent"] = llvm::Function::Create(
        mallocidentTy, llvm::Function::ExternalLinkage, "MAllocIdent", mod_.get());

    auto* charOpTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    const char* charOps[] = {"ToUpper", "ToLower", "IsAlpha", "IsDigit",
                              "IsAlphaNum", "IsUpper", "IsLower"};
    for (const char* name : charOps) {
        functions_[name] = llvm::Function::Create(
            charOpTy, llvm::Function::ExternalLinkage, name, mod_.get());
    }

    auto* getcharTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["GetChar"] = llvm::Function::Create(
        getcharTy, llvm::Function::ExternalLinkage, "GetChar", mod_.get());
    auto* getstrTy = llvm::FunctionType::get(i64Ty, {ptrTy, i64Ty}, false);
    functions_["GetStr"] = llvm::Function::Create(
        getstrTy, llvm::Function::ExternalLinkage, "GetStr", mod_.get());

    auto* sleepTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    functions_["Sleep"] = llvm::Function::Create(
        sleepTy, llvm::Function::ExternalLinkage, "Sleep", mod_.get());
    auto* getTicksTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["GetTicks"] = llvm::Function::Create(
        getTicksTy, llvm::Function::ExternalLinkage, "GetTicks", mod_.get());
    auto* nowTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["Now"] = llvm::Function::Create(
        nowTy, llvm::Function::ExternalLinkage, "Now", mod_.get());
    auto* getTickCountTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["GetTickCount"] = llvm::Function::Create(
        getTickCountTy, llvm::Function::ExternalLinkage, "GetTickCount", mod_.get());
    auto* sysdbgTy = llvm::FunctionType::get(voidTy, {}, false);
    functions_["SysDbg"] = llvm::Function::Create(
        sysdbgTy, llvm::Function::ExternalLinkage, "SysDbg", mod_.get());

    auto* fopenTy = llvm::FunctionType::get(i64Ty, {ptrTy, i64Ty}, false);
    functions_["FileOpen"] = llvm::Function::Create(
        fopenTy, llvm::Function::ExternalLinkage, "FileOpen", mod_.get());
    auto* fcloseTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    functions_["FileClose"] = llvm::Function::Create(
        fcloseTy, llvm::Function::ExternalLinkage, "FileClose", mod_.get());
    auto* freadTy = llvm::FunctionType::get(i64Ty, {i64Ty, ptrTy, i64Ty}, false);
    functions_["FileRead"] = llvm::Function::Create(
        freadTy, llvm::Function::ExternalLinkage, "FileRead", mod_.get());
    functions_["FileWrite"] = llvm::Function::Create(
        freadTy, llvm::Function::ExternalLinkage, "FileWrite", mod_.get());
    auto* fsizeTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    functions_["FileSize"] = llvm::Function::Create(
        fsizeTy, llvm::Function::ExternalLinkage, "FileSize", mod_.get());
    auto* fseekTy = llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false);
    functions_["FileSeek"] = llvm::Function::Create(
        fseekTy, llvm::Function::ExternalLinkage, "FileSeek", mod_.get());
    auto* fexistsTy = llvm::FunctionType::get(i64Ty, {ptrTy}, false);
    functions_["FileExists"] = llvm::Function::Create(
        fexistsTy, llvm::Function::ExternalLinkage, "FileExists", mod_.get());
    auto* freadallTy = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false);
    functions_["FileReadAll"] = llvm::Function::Create(
        freadallTy, llvm::Function::ExternalLinkage, "FileReadAll", mod_.get());
    auto* fwriteallTy = llvm::FunctionType::get(voidTy, {ptrTy, ptrTy, i64Ty}, false);
    functions_["FileWriteAll"] = llvm::Function::Create(
        fwriteallTy, llvm::Function::ExternalLinkage, "FileWriteAll", mod_.get());
    functions_["FileDel"] = llvm::Function::Create(
        fexistsTy, llvm::Function::ExternalLinkage, "FileDel", mod_.get());

    auto* strNewTy = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
    functions_["StrNew"] = llvm::Function::Create(
        strNewTy, llvm::Function::ExternalLinkage, "StrNew", mod_.get());
    functions_["StrDup"] = llvm::Function::Create(
        strNewTy, llvm::Function::ExternalLinkage, "StrDup", mod_.get());
    functions_["StrUpr"] = llvm::Function::Create(
        strNewTy, llvm::Function::ExternalLinkage, "StrUpr", mod_.get());
    functions_["StrLwr"] = llvm::Function::Create(
        strNewTy, llvm::Function::ExternalLinkage, "StrLwr", mod_.get());
    auto* sprintTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy}, /*isVarArg=*/true);
    functions_["SPrint"] = llvm::Function::Create(
        sprintTy, llvm::Function::ExternalLinkage, "SPrint", mod_.get());
    auto* strnlenTy = llvm::FunctionType::get(i64Ty, {ptrTy, i64Ty}, false);
    functions_["StrNLen"] = llvm::Function::Create(
        strnlenTy, llvm::Function::ExternalLinkage, "StrNLen", mod_.get());

    auto* randI64Ty = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["RandI64"] = llvm::Function::Create(
        randI64Ty, llvm::Function::ExternalLinkage, "RandI64", mod_.get());

    auto* dirTy = llvm::FunctionType::get(i64Ty, {ptrTy}, false);
    functions_["DirExists"] = llvm::Function::Create(
        dirTy, llvm::Function::ExternalLinkage, "DirExists", mod_.get());
    functions_["DirMk"] = llvm::Function::Create(
        dirTy, llvm::Function::ExternalLinkage, "DirMk", mod_.get());
    auto* fileRenameTy = llvm::FunctionType::get(i64Ty, {ptrTy, ptrTy}, false);
    functions_["FileRename"] = llvm::Function::Create(
        fileRenameTy, llvm::Function::ExternalLinkage, "FileRename", mod_.get());

    auto* memmoveTy = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy, i64Ty}, false);
    functions_["MemMove"] = llvm::Function::Create(
        memmoveTy, llvm::Function::ExternalLinkage, "MemMove", mod_.get());

    auto* argcTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["ArgC"] = llvm::Function::Create(
        argcTy, llvm::Function::ExternalLinkage, "ArgC", mod_.get());
    auto* argvTy = llvm::FunctionType::get(ptrTy, {}, false);
    functions_["ArgV"] = llvm::Function::Create(
        argvTy, llvm::Function::ExternalLinkage, "ArgV", mod_.get());

    const char* charOpsExt[] = {"IsSpace", "IsPunct", "IsCtrl", "IsXDigit", "IsGraph", "IsPrint"};
    for (const char* name : charOpsExt) {
        functions_[name] = llvm::Function::Create(
            charOpTy, llvm::Function::ExternalLinkage, name, mod_.get());
    }

    auto* f64f64Ty = llvm::FunctionType::get(f64Ty, {f64Ty}, false);
    const char* mathOpsExt[] = {"ACos", "ASin", "Sinh", "Cosh", "Tanh", "Cbrt", "Trunc"};
    for (const char* name : mathOpsExt) {
        functions_[name] = llvm::Function::Create(
            f64f64Ty, llvm::Function::ExternalLinkage, name, mod_.get());
    }
    auto* fmodTy = llvm::FunctionType::get(f64Ty, {f64Ty, f64Ty}, false);
    functions_["FMod"] = llvm::Function::Create(
        fmodTy, llvm::Function::ExternalLinkage, "FMod", mod_.get());
    auto* abortTy = llvm::FunctionType::get(voidTy, {}, false);
    functions_["Abort"] = llvm::Function::Create(
        abortTy, llvm::Function::ExternalLinkage, "Abort", mod_.get());

    auto* strOccTy = llvm::FunctionType::get(i64Ty, {ptrTy, i64Ty}, false);
    functions_["StrOcc"] = llvm::Function::Create(
        strOccTy, llvm::Function::ExternalLinkage, "StrOcc", mod_.get());
    auto* strFindTy = llvm::FunctionType::get(ptrTy, {ptrTy, i64Ty}, false);
    functions_["StrFirst"] = llvm::Function::Create(
        strFindTy, llvm::Function::ExternalLinkage, "StrFirst", mod_.get());
    functions_["StrLast"] = llvm::Function::Create(
        strFindTy, llvm::Function::ExternalLinkage, "StrLast", mod_.get());

    auto* vaSetCountTy = llvm::FunctionType::get(voidTy, {i64Ty}, false);
    functions_["__holyc_va_set_count"] = llvm::Function::Create(
        vaSetCountTy, llvm::Function::ExternalLinkage, "__holyc_va_set_count", mod_.get());
    fn_va_count_ = functions_["__holyc_va_set_count"];
    auto* vaStoreTy = llvm::FunctionType::get(voidTy, {i64Ty, i64Ty}, false);
    functions_["__holyc_va_store"] = llvm::Function::Create(
        vaStoreTy, llvm::Function::ExternalLinkage, "__holyc_va_store", mod_.get());
    fn_va_store_ = functions_["__holyc_va_store"];
    auto* vaCountTy = llvm::FunctionType::get(i64Ty, {}, false);
    functions_["__holyc_vararg_count"] = llvm::Function::Create(
        vaCountTy, llvm::Function::ExternalLinkage, "__holyc_vararg_count", mod_.get());
    functions_["__vararg_count"] = functions_["__holyc_vararg_count"];
    auto* vaGetTy = llvm::FunctionType::get(i64Ty, {i64Ty}, false);
    functions_["__holyc_vararg_get"] = llvm::Function::Create(
        vaGetTy, llvm::Function::ExternalLinkage, "__holyc_vararg_get", mod_.get());
    functions_["__vararg_get"] = functions_["__holyc_vararg_get"];
    fn_va_get_ = functions_["__holyc_vararg_get"];
}

/**
 * @brief HolyC string-output statement को __holyc_print call पर lower करता है।
 *
 * 64 bits से narrower integer arguments और float arguments को double में promote
 * करता है, फिर __holyc_print को variadic arguments के रूप में pass करता है।
 *
 * @param s Lower करने वाला string-output statement।
 * @return __holyc_print call द्वारा return की गई CallInst*, या format न हो तो nullptr।
 */
llvm::Value* LLVMCodegen::lowerStringOutput(StringOutputStmt* s) {
    if (!s->format) return nullptr;

    std::string fmt = s->format->value;
    auto* strConst = builder_.CreateGlobalStringPtr(fmt, ".str");

    std::vector<llvm::Value*> args;
    args.push_back(strConst);
    for (auto* arg : s->args) {
        llvm::Value* v = lowerExpr(arg);
        if (!v) continue;
        if (v->getType()->isIntegerTy(1))
            v = builder_.CreateZExt(v, i64Ty_);
        else if (v->getType()->isIntegerTy() &&
                 v->getType()->getIntegerBitWidth() < 64)
            v = builder_.CreateSExt(v, i64Ty_);
        else if (v->getType()->isFloatTy())
            v = builder_.CreateFPExt(v, f64Ty_);
        args.push_back(v);
    }

    return builder_.CreateCall(fn_holyc_print_, args);
}

/**
 * @brief किसी lvalue expression का address (alloca/GEP) लौटाता है, या nullptr।
 *
 * @param e जिस lvalue expression का address चाहिए।
 * @return Load/store के लिए suitable pointer-typed Value*, या अगर addressable नहीं हो तो nullptr।
 */
llvm::Value* LLVMCodegen::lowerExprAddr(Expr* e) {
    if (!e) return nullptr;

    switch (e->nk) {
    case NodeKind::IdentifierExpr: {
        auto* id = static_cast<IdentifierExpr*>(e);
        auto lit = locals_.find(id->name);
        if (lit != locals_.end()) return lit->second;
        auto git = globals_.find(id->name);
        if (git != globals_.end()) return git->second;
        auto fit = functions_.find(id->name);
        if (fit != functions_.end()) return fit->second;
        if (currentMethodClass_) {
            auto thisIt = locals_.find("this");
            if (thisIt != locals_.end()) {
                ClassDecl* cd = currentMethodClass_;
                while (cd) {
                    for (auto* m : cd->members) {
                        if (m->nk == NodeKind::FieldDecl) {
                            auto* fd2 = static_cast<FieldDecl*>(m);
                            if (fd2->name == id->name) {
                                llvm::Value* thisPtr = builder_.CreateLoad(
                                    ptrTy_, thisIt->second, "this");
                                size_t off = computeFieldOffset(currentMethodClass_->name, id->name);
                                auto* offVal = llvm::ConstantInt::get(i64Ty_, off);
                                return builder_.CreateGEP(
                                    i8Ty_, thisPtr, {offVal}, id->name + "_addr");
                            }
                        }
                    }
                    if (!cd->base_name.empty()) {
                        auto bit = classDecls_.find(cd->base_name);
                        cd = (bit != classDecls_.end()) ? bit->second : nullptr;
                    } else {
                        cd = nullptr;
                    }
                }
            }
        }
        return nullptr;
    }
    case NodeKind::DerefExpr: {
        auto* de = static_cast<DerefExpr*>(e);
        return lowerExpr(de->operand);
    }
    case NodeKind::ArrayIndexExpr: {
        auto* ai = static_cast<ArrayIndexExpr*>(e);
        llvm::Value* idx = lowerExpr(ai->index);
        if (!idx) return nullptr;
        if (!idx->getType()->isIntegerTy(64))
            idx = builder_.CreateSExt(idx, i64Ty_);

        llvm::Type* elemTy = ai->resolved_type ? toLLVMType(ai->resolved_type)
                                               : i64Ty_;
        if (elemTy->isVoidTy()) elemTy = i8Ty_;

        // Intrinsic union sub-element access: val.u8[3], val.i32[1], आदि।
        if (auto* fa = dynamic_cast<FieldAccessExpr*>(ai->base)) {
            Type* objTy = fa->object->resolved_type;
            if (objTy && objTy->kind == Type::IntrinsicUnion) {
                const std::string& fn = fa->field;
                size_t esz = 8;
                if (fn == "u8"  || fn == "i8")  esz = 1;
                else if (fn == "u16" || fn == "i16") esz = 2;
                else if (fn == "u32" || fn == "i32") esz = 4;
                llvm::Value* iuAddr = lowerExprAddr(fa->object);
                if (iuAddr) {
                    llvm::Value* byteIdx = idx;
                    if (esz > 1) {
                        auto* eszConst = llvm::ConstantInt::get(i64Ty_, esz);
                        byteIdx = builder_.CreateMul(idx, eszConst, "iu_byte_off");
                    }
                    return builder_.CreateGEP(i8Ty_, iuAddr, {byteIdx}, "iu_elem");
                }
            }
        }

        llvm::Value* baseAddr = lowerExprAddr(ai->base);
        if (baseAddr) {
            llvm::Type* basePointeeTy = nullptr;
            if (auto* ai2 = llvm::dyn_cast<llvm::AllocaInst>(baseAddr))
                basePointeeTy = ai2->getAllocatedType();
            else if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(baseAddr))
                basePointeeTy = gv->getValueType();
            else if (ai->base->resolved_type && ai->base->resolved_type->kind == Type::Array) {
                    basePointeeTy = toLLVMType(ai->base->resolved_type);
            }

            if (basePointeeTy && basePointeeTy->isArrayTy()) {
                auto* zero = llvm::ConstantInt::get(i64Ty_, 0);
                return builder_.CreateGEP(basePointeeTy, baseAddr, {zero, idx}, "arridx");
            }
            llvm::Value* ptr = builder_.CreateLoad(ptrTy_, baseAddr, "loadptr");
            return builder_.CreateGEP(elemTy, ptr, {idx}, "arridx");
        }

        llvm::Value* base = lowerExpr(ai->base);
        if (!base) return nullptr;
        if (base->getType()->isIntegerTy())
            base = builder_.CreateIntToPtr(base, ptrTy_);
        return builder_.CreateGEP(elemTy, base, {idx}, "arridx");
    }
    case NodeKind::FieldAccessExpr: {
        auto* fa = static_cast<FieldAccessExpr*>(e);

        // Intrinsic union field access: IU variable का address लौटाओ
        if (fa->object->resolved_type && fa->object->resolved_type->kind == Type::IntrinsicUnion) {
            return lowerExprAddr(fa->object);
        }

        std::string struct_name;
        bool obj_is_ptr_type = false;
        if (fa->object->resolved_type) {
            Type* objTy = fa->object->resolved_type;
            if (objTy->kind == Type::Pointer) {
                obj_is_ptr_type = true;
                auto& pt = std::get<PointerType>(objTy->data);
                if (pt.pointee && pt.pointee->kind == Type::Class) {
                    auto& ct = std::get<ClassType>(pt.pointee->data);
                    if (ct.decl) struct_name = ct.decl->name;
                }
            } else if (objTy->kind == Type::Class) {
                auto& ct = std::get<ClassType>(objTy->data);
                if (ct.decl) struct_name = ct.decl->name;
            }
        }

        llvm::Value* obj_ptr = nullptr;
        if (fa->is_arrow || obj_is_ptr_type) {
            obj_ptr = lowerExpr(fa->object);
        } else {
            obj_ptr = lowerExprAddr(fa->object);
        }
        if (!obj_ptr) return nullptr;
        if (obj_ptr->getType()->isIntegerTy())
            obj_ptr = builder_.CreateIntToPtr(obj_ptr, ptrTy_);

        size_t field_offset = computeFieldOffset(struct_name, fa->field);
        auto* offsetVal = llvm::ConstantInt::get(i64Ty_, field_offset);
        return builder_.CreateGEP(i8Ty_, obj_ptr, {offsetVal}, "field_addr");
    }
    default:
        return nullptr;
    }
}

/**
 * @brief Class के own fields को walk करता है और bit-field packing के साथ offsets compute करता है।
 *
 * सभी fields के बाद final byte offset लौटाता है; जब field_name non-empty हो और
 * मिल जाए, तो field का byte offset और bit-field info out-parameters में store करके
 * sentinel के रूप में SIZE_MAX लौटाता है।
 *
 * @param cd वो class declaration जिसके fields walk होंगे।
 * @param start_off वो byte offset जहाँ से इस class के fields शुरू होते हैं (base class के बाद)।
 * @param field_name ढूंढने वाला field, या "" सिर्फ total size compute के लिए।
 * @param out_byte_offset Output: मिले field का byte offset (सिर्फ sentinel return होने पर)।
 * @param out_bf Output: मिले field की bit-field layout info (सिर्फ sentinel return होने पर)।
 * @return सभी fields के बाद total byte offset, या field_name मिल जाए तो SIZE_MAX।
 */
static size_t walkClassFields(ClassDecl* cd, size_t start_off,
                               const std::string& field_name,
                               size_t* out_byte_offset,
                               BitFieldInfo* out_bf) {
    size_t off = start_off;
    size_t current_word_byte_offset = 0;
    int current_bit_offset = 0;
    bool in_bit_field_run = false;

    std::unordered_map<int, size_t> union_group_start;
    std::unordered_map<int, size_t> union_group_max;
    {
        size_t tmp = start_off;
        for (auto* m : cd->members) {
            if (m->nk != NodeKind::FieldDecl) continue;
            auto* fld = static_cast<FieldDecl*>(m);
            if (fld->is_union_member && fld->union_group >= 0) {
                int grp = fld->union_group;
                size_t sz = fld->type ? static_cast<size_t>(fld->type->sizeInBytes()) : 8;
                if (sz == 0) sz = 8;
                if (union_group_start.find(grp) == union_group_start.end()) {
                    size_t align = sz > 8 ? 8 : sz;
                    size_t grp_off = (tmp + align - 1) & ~(align - 1);
                    union_group_start[grp] = grp_off;
                    union_group_max[grp] = sz;
                } else {
                    if (sz > union_group_max[grp]) union_group_max[grp] = sz;
                }
            } else if (!fld->is_union_member) {
                size_t sz = fld->type ? static_cast<size_t>(fld->type->sizeInBytes()) : 8;
                if (sz == 0) sz = 8;
                size_t align = sz > 8 ? 8 : sz;
                tmp = (tmp + align - 1) & ~(align - 1);
                tmp += sz;
            }
        }
    }

    for (auto* m : cd->members) {
        if (m->nk != NodeKind::FieldDecl) continue;
        auto* fld = static_cast<FieldDecl*>(m);

        if (fld->is_union_member && fld->union_group >= 0) {
            int grp = fld->union_group;
            auto gs_it = union_group_start.find(grp);
            size_t grp_start = (gs_it != union_group_start.end()) ? gs_it->second : off;
            if (!field_name.empty() && fld->name == field_name && out_byte_offset && out_bf) {
                *out_byte_offset = grp_start;
                out_bf->bit_start = 0;
                out_bf->bit_width = -1;
                return static_cast<size_t>(-1); // sentinel: found
            }
            auto gm_it = union_group_max.find(grp);
            size_t grp_end = grp_start + ((gm_it != union_group_max.end()) ? gm_it->second : 8);
            if (grp_end > off) off = grp_end;
            continue;
        }

        if (fld->bit_width >= 0) {
            int width = fld->bit_width;
            if (!in_bit_field_run) {
                size_t align = 8;
                off = (off + align - 1) & ~(align - 1);
                current_word_byte_offset = off;
                current_bit_offset = 0;
                off += 8;
                in_bit_field_run = true;
            } else if (current_bit_offset + width > 64) {
                size_t align = 8;
                off = (off + align - 1) & ~(align - 1);
                current_word_byte_offset = off;
                current_bit_offset = 0;
                off += 8;
            }
            if (!field_name.empty() && fld->name == field_name && out_byte_offset && out_bf) {
                *out_byte_offset = current_word_byte_offset;
                out_bf->bit_start = current_bit_offset;
                out_bf->bit_width = width;
                return static_cast<size_t>(-1); // sentinel: found
            }
            current_bit_offset += width;
        } else {
            in_bit_field_run = false;
            current_bit_offset = 0;

            size_t sz = fld->type ? static_cast<size_t>(fld->type->sizeInBytes()) : 8;
            if (sz == 0) sz = 8;
            size_t align = sz > 8 ? 8 : sz;
            off = (off + align - 1) & ~(align - 1);
            if (!field_name.empty() && fld->name == field_name && out_byte_offset && out_bf) {
                *out_byte_offset = off;
                out_bf->bit_start = 0;
                out_bf->bit_width = -1;
                return static_cast<size_t>(-1); // sentinel: found
            }
            off += sz;
        }
    }
    return off;
}

/**
 * @brief किसी class का total byte size लौटाता है, inherited base fields सहित।
 *
 * Results structSizeCache_ में class name से keyed होकर cache होते हैं।
 *
 * @param class_name जिस class का size चाहिए उसका नाम।
 * @return Bytes में total size, या class न मिले तो 8।
 */
size_t LLVMCodegen::computeStructSize(const std::string& class_name) {
    auto cit = structSizeCache_.find(class_name);
    if (cit != structSizeCache_.end()) return cit->second;

    auto ccit = classDecls_.find(class_name);
    if (ccit == classDecls_.end()) {
        structSizeCache_[class_name] = 8;
        return 8;
    }
    ClassDecl* cd = ccit->second;

    size_t off = 0;
    if (!cd->base_name.empty())
        off = computeStructSize(cd->base_name);

    off = walkClassFields(cd, off, "", nullptr, nullptr);
    size_t result = off > 0 ? off : 8;
    structSizeCache_[class_name] = result;
    return result;
}

/**
 * @brief किसी class के अंदर named field का byte offset लौटाता है, inheritance handle करते हुए।
 *
 * पहले base class में ढूंढता है, फिर derived class में। Results fieldOffsetCache_ में
 * "ClassName::fieldName" से keyed होकर cache होते हैं।
 *
 * @param class_name Field contain करने वाले class का नाम।
 * @param field_name Locate करने वाले field का नाम।
 * @return Struct की शुरुआत से byte offset, या न मिले तो 0।
 */
size_t LLVMCodegen::computeFieldOffset(const std::string& class_name,
                                        const std::string& field_name) {
    std::string key;
    key.reserve(class_name.size() + 2 + field_name.size());
    key = class_name;
    key += "::";
    key += field_name;
    auto cit = fieldOffsetCache_.find(key);
    if (cit != fieldOffsetCache_.end()) return cit->second;

    auto ccit = classDecls_.find(class_name);
    if (ccit == classDecls_.end()) {
        fieldOffsetCache_[key] = 0;
        return 0;
    }
    ClassDecl* cd = ccit->second;

    if (!cd->base_name.empty()) {
        auto bit = classDecls_.find(cd->base_name);
        if (bit != classDecls_.end()) {
            size_t base_start = bit->second->base_name.empty() ? 0
                : computeStructSize(bit->second->base_name);
            size_t byte_off = 0;
            BitFieldInfo bf;
            size_t r = walkClassFields(bit->second, base_start, field_name, &byte_off, &bf);
            if (r == static_cast<size_t>(-1)) {
                fieldOffsetCache_[key] = byte_off;
                return byte_off;
            }
        }
    }

    size_t start_off = cd->base_name.empty() ? 0 : computeStructSize(cd->base_name);
    size_t byte_off = 0;
    BitFieldInfo bf;
    size_t r = walkClassFields(cd, start_off, field_name, &byte_off, &bf);
    if (r == static_cast<size_t>(-1)) {
        fieldOffsetCache_[key] = byte_off;
        return byte_off;
    }
    fieldOffsetCache_[key] = 0;
    return 0;
}

/**
 * @brief Named field की bit-field layout info लौटाता है (read/write masking के लिए)।
 *
 * @param class_name Field contain करने वाले class का नाम।
 * @param field_name Locate करने वाले bit-field का नाम।
 * @return bit_start और bit_width के साथ BitFieldInfo; bit_width == -1 अगर bit-field नहीं है।
 */
BitFieldInfo LLVMCodegen::getBitFieldInfo(const std::string& class_name,
                                                        const std::string& field_name) {
    auto cit = classDecls_.find(class_name);
    if (cit == classDecls_.end()) return {};
    ClassDecl* cd = cit->second;

    if (!cd->base_name.empty()) {
        auto bit = classDecls_.find(cd->base_name);
        if (bit != classDecls_.end()) {
            size_t base_start = bit->second->base_name.empty() ? 0
                : computeStructSize(bit->second->base_name);
            size_t byte_off = 0;
            BitFieldInfo bf;
            size_t r = walkClassFields(bit->second, base_start, field_name, &byte_off, &bf);
            if (r == static_cast<size_t>(-1)) return bf;
        }
    }

    size_t start_off = cd->base_name.empty() ? 0 : computeStructSize(cd->base_name);
    size_t byte_off = 0;
    BitFieldInfo bf;
    walkClassFields(cd, start_off, field_name, &byte_off, &bf);
    return bf;
}

/**
 * @brief Pre/post increment/decrement का shared implementation।
 *
 * Integer arithmetic और pointer GEP-based stepping दोनों handle करता है।
 *
 * @param isInc Increment के लिए true, decrement के लिए false।
 * @param isPre नई value return करने के लिए true, पुरानी value के लिए false।
 * @param un Operand contain करने वाला unary expression node।
 * @return Resulting LLVM Value* (isPre के हिसाब से old या new)।
 */
llvm::Value* LLVMCodegen::emitIncDec(bool isInc, bool isPre, UnaryExpr* un) {
    llvm::Value* addr = lowerExprAddr(un->operand);
    if (!addr) return llvm::ConstantInt::get(i64Ty_, 0);
    llvm::Type* loadTy = i64Ty_;
    if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(addr))
        loadTy = ai->getAllocatedType();
    llvm::Value* v = builder_.CreateLoad(loadTy, addr);
    llvm::Value* res;
    if (v->getType()->isPointerTy()) {
        int64_t delta = isInc ? 1 : -1;
        res = builder_.CreateGEP(i8Ty_, v, llvm::ConstantInt::get(i64Ty_, delta));
    } else {
        llvm::Value* one = llvm::ConstantInt::get(v->getType(), 1);
        res = isInc ? builder_.CreateAdd(v, one) : builder_.CreateSub(v, one);
    }
    builder_.CreateStore(res, addr);
    return isPre ? res : v;
}

/**
 * @brief किसी expression को LLVM Value पर lower करता है।
 *
 * @param e Lower करने वाला expression। Null हो तो constant zero i64 लौटाता है।
 * @return Expression का rvalue represent करने वाला LLVM Value*।
 */
llvm::Value* LLVMCodegen::lowerExpr(Expr* e) {
    if (!e) return llvm::ConstantInt::get(i64Ty_, 0);

    switch (e->nk) {
    case NodeKind::IntLiteralExpr: {
        auto* lit = static_cast<IntLiteralExpr*>(e);
        llvm::Type* ty = primToLLVM(lit->type_hint);
        if (ty->isVoidTy()) ty = i64Ty_;
        return llvm::ConstantInt::get(ty, lit->value, /*isSigned=*/true);
    }

    case NodeKind::FloatLiteralExpr: {
        auto* lit = static_cast<FloatLiteralExpr*>(e);
        return llvm::ConstantFP::get(*ctx_, llvm::APFloat(lit->value));
    }

    case NodeKind::StringLiteralExpr: {
        auto* lit = static_cast<StringLiteralExpr*>(e);
        return builder_.CreateGlobalStringPtr(lit->value, ".str");
    }

    case NodeKind::CharLiteralExpr: {
        auto* lit = static_cast<CharLiteralExpr*>(e);
        if (lit->byte_count > 1)
            return llvm::ConstantInt::get(i64Ty_, lit->value);
        return llvm::ConstantInt::get(i8Ty_, lit->value);
    }

    case NodeKind::BoolLiteralExpr: {
        auto* lit = static_cast<BoolLiteralExpr*>(e);
        return llvm::ConstantInt::get(i64Ty_, lit->value ? 1 : 0);
    }

    case NodeKind::IdentifierExpr: {
        auto* id = static_cast<IdentifierExpr*>(e);
        auto lit = locals_.find(id->name);
        if (lit != locals_.end()) {
            llvm::Type* loadTy = nullptr;
            if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(lit->second))
                loadTy = ai->getAllocatedType();
            else if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(lit->second))
                loadTy = gv->getValueType();
            else
                loadTy = i64Ty_;
            if (loadTy && loadTy->isArrayTy()) {
                auto* zero = llvm::ConstantInt::get(i64Ty_, 0);
                return builder_.CreateGEP(loadTy, lit->second, {zero, zero}, id->name + "_ptr");
            }
            return builder_.CreateLoad(loadTy, lit->second, id->name);
        }
        auto git = globals_.find(id->name);
        if (git != globals_.end()) {
            llvm::Type* loadTy = git->second->getValueType();
            if (loadTy && loadTy->isArrayTy()) {
                auto* zero = llvm::ConstantInt::get(i64Ty_, 0);
                return builder_.CreateGEP(loadTy, git->second, {zero, zero}, id->name + "_ptr");
            }
            return builder_.CreateLoad(loadTy, git->second, id->name);
        }
        auto fit = functions_.find(id->name);
        if (fit != functions_.end())
            return fit->second;
        if (currentMethodClass_) {
            auto thisIt = locals_.find("this");
            if (thisIt != locals_.end()) {
                ClassDecl* cd = currentMethodClass_;
                while (cd) {
                    for (auto* m : cd->members) {
                        if (m->nk == NodeKind::FieldDecl) {
                            auto* fd2 = static_cast<FieldDecl*>(m);
                            if (fd2->name == id->name) {
                                llvm::Value* thisPtr = builder_.CreateLoad(
                                    ptrTy_, thisIt->second, "this");
                                size_t off = computeFieldOffset(currentMethodClass_->name, id->name);
                                auto* i64ty = i64Ty_;
                                auto* offVal = llvm::ConstantInt::get(i64ty, off);
                                auto* fieldPtr = builder_.CreateGEP(
                                    i8Ty_, thisPtr, {offVal}, id->name + "_ptr");
                                llvm::Type* loadTy = fd2->type ? toLLVMType(fd2->type) : i64ty;
                                if (!loadTy || loadTy->isVoidTy()) loadTy = i64ty;
                                return builder_.CreateLoad(loadTy, fieldPtr, id->name);
                            }
                        }
                    }
                    if (!cd->base_name.empty()) {
                        auto bit = classDecls_.find(cd->base_name);
                        cd = (bit != classDecls_.end()) ? bit->second : nullptr;
                    } else {
                        cd = nullptr;
                    }
                }
            }
        }
        auto eit = enumConsts_.find(id->name);
        if (eit != enumConsts_.end())
            return llvm::ConstantInt::get(i64Ty_, eit->second);
        return llvm::ConstantInt::get(i64Ty_, 0);
    }

    case NodeKind::BinaryExpr: {
        auto* bin = static_cast<BinaryExpr*>(e);
        BinOpKind op = bin->op;

        bool is_assign = (op == BinOpKind::Assign || op == BinOpKind::AddAssign ||
                          op == BinOpKind::SubAssign || op == BinOpKind::MulAssign ||
                          op == BinOpKind::DivAssign || op == BinOpKind::ModAssign ||
                          op == BinOpKind::BitAndAssign || op == BinOpKind::BitOrAssign ||
                          op == BinOpKind::BitXorAssign || op == BinOpKind::ShlAssign ||
                          op == BinOpKind::ShrAssign || op == BinOpKind::PPAssign ||
                          op == BinOpKind::MMAssign);

        if (is_assign) {
            BitFieldInfo lhs_bfi;
            std::string lhs_struct_name;
            if (auto* fa_lhs = dynamic_cast<FieldAccessExpr*>(bin->lhs)) {
                if (fa_lhs->object->resolved_type) {
                    Type* objTy = fa_lhs->object->resolved_type;
                    if (objTy->kind == Type::Pointer) {
                        auto& pt = std::get<PointerType>(objTy->data);
                        if (pt.pointee && pt.pointee->kind == Type::Class) {
                            auto& ct = std::get<ClassType>(pt.pointee->data);
                            if (ct.decl) lhs_struct_name = ct.decl->name;
                        }
                    } else if (objTy->kind == Type::Class) {
                        auto& ct = std::get<ClassType>(objTy->data);
                        if (ct.decl) lhs_struct_name = ct.decl->name;
                    }
                }
                if (!lhs_struct_name.empty())
                    lhs_bfi = getBitFieldInfo(lhs_struct_name, fa_lhs->field);
            }

            llvm::Value* addr = lowerExprAddr(bin->lhs);
            if (!addr) return llvm::ConstantInt::get(i64Ty_, 0);

            if (op == BinOpKind::Assign) {
                llvm::Value* rhs = lowerExpr(bin->rhs);

                if (lhs_bfi.bit_width >= 0) {
                    if (lhs_bfi.bit_width <= 0 || lhs_bfi.bit_width > 64) {
                        diag_.error(bin->loc, "bit-field width must be in range [1, 64]");
                        return llvm::ConstantInt::get(i64Ty_, 0);
                    }
                    llvm::Type* i64 = i64Ty_;
                    if (!rhs->getType()->isIntegerTy(64))
                        rhs = builder_.CreateZExtOrTrunc(rhs, i64);
                    uint64_t mask = (1ULL << lhs_bfi.bit_width) - 1;
                    llvm::Value* maskVal = llvm::ConstantInt::get(i64, mask);
                    llvm::Value* clearMask = llvm::ConstantInt::get(i64, ~(mask << lhs_bfi.bit_start));
                    llvm::Value* word = builder_.CreateLoad(i64, addr, "bf_word");
                    word = builder_.CreateAnd(word, clearMask, "bf_clear");
                    llvm::Value* newBits = builder_.CreateAnd(rhs, maskVal, "bf_mask");
                    if (lhs_bfi.bit_start > 0) {
                        llvm::Value* shift = llvm::ConstantInt::get(i64, lhs_bfi.bit_start);
                        newBits = builder_.CreateShl(newBits, shift, "bf_shl");
                    }
                    word = builder_.CreateOr(word, newBits, "bf_new");
                    builder_.CreateStore(word, addr);
                    return rhs;
                }

                if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                    llvm::Type* allocTy = ai->getAllocatedType();
                    if (rhs->getType() != allocTy) {
                        if (allocTy->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                            unsigned dstBits = allocTy->getIntegerBitWidth();
                            unsigned srcBits = rhs->getType()->getIntegerBitWidth();
                            if (dstBits > srcBits)
                                rhs = builder_.CreateSExt(rhs, allocTy);
                            else if (dstBits < srcBits)
                                rhs = builder_.CreateTrunc(rhs, allocTy);
                        } else if (allocTy->isFloatingPointTy() && rhs->getType()->isIntegerTy()) {
                            rhs = builder_.CreateSIToFP(rhs, allocTy);
                        } else if (allocTy->isIntegerTy() && rhs->getType()->isFloatingPointTy()) {
                            rhs = builder_.CreateFPToSI(rhs, allocTy);
                        } else if (allocTy->isFloatTy() && rhs->getType()->isDoubleTy()) {
                            rhs = builder_.CreateFPTrunc(rhs, allocTy);
                        } else if (allocTy->isDoubleTy() && rhs->getType()->isFloatTy()) {
                            rhs = builder_.CreateFPExt(rhs, allocTy);
                        } else if (allocTy->isIntegerTy() && rhs->getType()->isPointerTy()) {
                            rhs = builder_.CreatePtrToInt(rhs, allocTy);
                        } else if (allocTy->isPointerTy() && rhs->getType()->isIntegerTy()) {
                            rhs = builder_.CreateIntToPtr(rhs, allocTy);
                        }
                    }
                } else if (bin->lhs->resolved_type) {
                    llvm::Type* dstTy = toLLVMType(bin->lhs->resolved_type);
                    if (dstTy && !dstTy->isVoidTy() && !dstTy->isPointerTy()
                              && rhs->getType() != dstTy) {
                        if (dstTy->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                            unsigned dstBits = dstTy->getIntegerBitWidth();
                            unsigned srcBits = rhs->getType()->getIntegerBitWidth();
                            if (dstBits < srcBits) rhs = builder_.CreateTrunc(rhs, dstTy);
                            else if (dstBits > srcBits) rhs = builder_.CreateZExt(rhs, dstTy);
                        } else if (dstTy->isFloatingPointTy() && rhs->getType()->isIntegerTy()) {
                            rhs = builder_.CreateSIToFP(rhs, dstTy);
                        } else if (dstTy->isIntegerTy() && rhs->getType()->isFloatingPointTy()) {
                            rhs = builder_.CreateFPToSI(rhs, dstTy);
                        } else if (dstTy->isFloatTy() && rhs->getType()->isDoubleTy()) {
                            rhs = builder_.CreateFPTrunc(rhs, dstTy);
                        } else if (dstTy->isDoubleTy() && rhs->getType()->isFloatTy()) {
                            rhs = builder_.CreateFPExt(rhs, dstTy);
                        }
                    }
                }
                builder_.CreateStore(rhs, addr);
                return rhs;
            }

            llvm::Type* loadTy = i64Ty_;
            if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(addr))
                loadTy = ai->getAllocatedType();
            else if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(addr))
                loadTy = gv->getValueType();
            llvm::Value* lhs = builder_.CreateLoad(loadTy, addr, "lhs");
            if (op == BinOpKind::PPAssign) {
                llvm::Value* one = llvm::ConstantInt::get(lhs->getType(), 1);
                llvm::Value* res = builder_.CreateAdd(lhs, one, "pp");
                builder_.CreateStore(res, addr);
                return res;
            }
            if (op == BinOpKind::MMAssign) {
                llvm::Value* one = llvm::ConstantInt::get(lhs->getType(), 1);
                llvm::Value* res = builder_.CreateSub(lhs, one, "mm");
                builder_.CreateStore(res, addr);
                return res;
            }

            llvm::Value* rhs = lowerExpr(bin->rhs);
            if (rhs->getType() != lhs->getType()) {
                if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                    unsigned dstBits = lhs->getType()->getIntegerBitWidth();
                    unsigned srcBits = rhs->getType()->getIntegerBitWidth();
                    if (dstBits > srcBits)
                        rhs = builder_.CreateSExt(rhs, lhs->getType());
                    else
                        rhs = builder_.CreateTrunc(rhs, lhs->getType());
                }
            }

            llvm::Value* res = nullptr;
            bool isF = lhs->getType()->isFloatingPointTy();
            switch (op) {
            case BinOpKind::AddAssign: res = isF ? builder_.CreateFAdd(lhs, rhs) : builder_.CreateAdd(lhs, rhs); break;
            case BinOpKind::SubAssign: res = isF ? builder_.CreateFSub(lhs, rhs) : builder_.CreateSub(lhs, rhs); break;
            case BinOpKind::MulAssign: res = isF ? builder_.CreateFMul(lhs, rhs) : builder_.CreateMul(lhs, rhs); break;
            case BinOpKind::DivAssign: res = isF ? builder_.CreateFDiv(lhs, rhs) : builder_.CreateSDiv(lhs, rhs); break;
            case BinOpKind::ModAssign: res = builder_.CreateSRem(lhs, rhs); break;
            case BinOpKind::BitAndAssign: res = builder_.CreateAnd(lhs, rhs); break;
            case BinOpKind::BitOrAssign: res = builder_.CreateOr(lhs, rhs); break;
            case BinOpKind::BitXorAssign: res = builder_.CreateXor(lhs, rhs); break;
            case BinOpKind::ShlAssign: res = builder_.CreateShl(lhs, rhs); break;
            case BinOpKind::ShrAssign: res = builder_.CreateAShr(lhs, rhs); break;
            default: res = lhs; break;
            }
            builder_.CreateStore(res, addr);
            return res;
        }

        if (op == BinOpKind::LogAnd) {
            llvm::Value* lhsVal = lowerExpr(bin->lhs);
            if (!lhsVal) lhsVal = llvm::ConstantInt::get(builder_.getInt1Ty(), 0);
            lhsVal = toBool(lhsVal);
            llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(*ctx_, "and.rhs", currentFunc_);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*ctx_, "and.merge", currentFunc_);
            llvm::BasicBlock* curBB = builder_.GetInsertBlock();
            builder_.CreateCondBr(lhsVal, rhsBB, mergeBB);

            builder_.SetInsertPoint(rhsBB);
            llvm::Value* rhsVal = lowerExpr(bin->rhs);
            if (!rhsVal) rhsVal = llvm::ConstantInt::get(builder_.getInt1Ty(), 0);
            rhsVal = toBool(rhsVal);
            llvm::BasicBlock* rhsEnd = builder_.GetInsertBlock();
            builder_.CreateBr(mergeBB);

            builder_.SetInsertPoint(mergeBB);
            llvm::PHINode* phi = builder_.CreatePHI(i1Ty_, 2, "and.result");
            phi->addIncoming(llvm::ConstantInt::getFalse(*ctx_), curBB);
            phi->addIncoming(rhsVal, rhsEnd);
            return builder_.CreateZExt(phi, i64Ty_, "and.i64");
        }

        if (op == BinOpKind::LogOr) {
            llvm::Value* lhsVal = lowerExpr(bin->lhs);
            if (!lhsVal) lhsVal = llvm::ConstantInt::get(builder_.getInt1Ty(), 0);
            lhsVal = toBool(lhsVal);
            llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(*ctx_, "or.rhs", currentFunc_);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*ctx_, "or.merge", currentFunc_);
            llvm::BasicBlock* curBB = builder_.GetInsertBlock();
            builder_.CreateCondBr(lhsVal, mergeBB, rhsBB);

            builder_.SetInsertPoint(rhsBB);
            llvm::Value* rhsVal = lowerExpr(bin->rhs);
            if (!rhsVal) rhsVal = llvm::ConstantInt::get(builder_.getInt1Ty(), 0);
            rhsVal = toBool(rhsVal);
            llvm::BasicBlock* rhsEnd = builder_.GetInsertBlock();
            builder_.CreateBr(mergeBB);

            builder_.SetInsertPoint(mergeBB);
            llvm::PHINode* phi = builder_.CreatePHI(i1Ty_, 2, "or.result");
            phi->addIncoming(llvm::ConstantInt::getTrue(*ctx_), curBB);
            phi->addIncoming(rhsVal, rhsEnd);
            return builder_.CreateZExt(phi, i64Ty_, "or.i64");
        }

        llvm::Value* lhs = lowerExpr(bin->lhs);
        llvm::Value* rhs = lowerExpr(bin->rhs);

        if (lhs->getType() != rhs->getType()) {
            if (lhs->getType()->isFloatingPointTy() && rhs->getType()->isIntegerTy()) {
                rhs = isSigned(bin->rhs->resolved_type)
                    ? builder_.CreateSIToFP(rhs, lhs->getType())
                    : builder_.CreateUIToFP(rhs, lhs->getType());
            } else if (rhs->getType()->isFloatingPointTy() && lhs->getType()->isIntegerTy()) {
                lhs = isSigned(bin->lhs->resolved_type)
                    ? builder_.CreateSIToFP(lhs, rhs->getType())
                    : builder_.CreateUIToFP(lhs, rhs->getType());
            } else if (lhs->getType()->isDoubleTy() && rhs->getType()->isFloatTy()) {
                rhs = builder_.CreateFPExt(rhs, f64Ty_);
            } else if (rhs->getType()->isDoubleTy() && lhs->getType()->isFloatTy()) {
                lhs = builder_.CreateFPExt(lhs, f64Ty_);
            } else if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                unsigned lBits = lhs->getType()->getIntegerBitWidth();
                unsigned rBits = rhs->getType()->getIntegerBitWidth();
                if (lBits > rBits)
                    rhs = builder_.CreateSExt(rhs, lhs->getType());
                else if (rBits > lBits)
                    lhs = builder_.CreateSExt(lhs, rhs->getType());
            } else if (lhs->getType()->isPointerTy() && rhs->getType()->isIntegerTy()) {
                if (op == BinOpKind::Eq || op == BinOpKind::Ne ||
                    op == BinOpKind::Lt || op == BinOpKind::Le ||
                    op == BinOpKind::Gt || op == BinOpKind::Ge) {
                    lhs = builder_.CreatePtrToInt(lhs, i64Ty_);
                    if (rhs->getType()->getIntegerBitWidth() < 64)
                        rhs = builder_.CreateSExt(rhs, i64Ty_);
                }
            } else if (rhs->getType()->isPointerTy() && lhs->getType()->isIntegerTy()) {
                if (op == BinOpKind::Eq || op == BinOpKind::Ne ||
                    op == BinOpKind::Lt || op == BinOpKind::Le ||
                    op == BinOpKind::Gt || op == BinOpKind::Ge) {
                    rhs = builder_.CreatePtrToInt(rhs, i64Ty_);
                    if (lhs->getType()->getIntegerBitWidth() < 64)
                        lhs = builder_.CreateSExt(lhs, i64Ty_);
                }
            }
        }

        bool floatOp = lhs->getType()->isFloatingPointTy();
        bool signedOp = isSigned(bin->lhs ? bin->lhs->resolved_type : nullptr) ||
                        isSigned(bin->rhs ? bin->rhs->resolved_type : nullptr);

        switch (op) {
        case BinOpKind::Add: {
            if (floatOp) return builder_.CreateFAdd(lhs, rhs);
            if (lhs->getType()->isPointerTy()) {
                llvm::Value* offset = rhs->getType()->isIntegerTy(64) ? rhs :
                    builder_.CreateSExt(rhs, i64Ty_);
                return builder_.CreateGEP(i8Ty_, lhs, {offset}, "ptradd");
            }
            if (rhs->getType()->isPointerTy()) {
                llvm::Value* offset = lhs->getType()->isIntegerTy(64) ? lhs :
                    builder_.CreateSExt(lhs, i64Ty_);
                return builder_.CreateGEP(i8Ty_, rhs, {offset}, "ptradd");
            }
            return builder_.CreateAdd(lhs, rhs);
        }
        case BinOpKind::Sub: {
            if (floatOp) return builder_.CreateFSub(lhs, rhs);
            if (lhs->getType()->isPointerTy() && !rhs->getType()->isPointerTy()) {
                llvm::Value* neg = builder_.CreateNeg(rhs, "neg_off");
                llvm::Value* offset = neg->getType()->isIntegerTy(64) ? neg :
                    builder_.CreateSExt(neg, i64Ty_);
                return builder_.CreateGEP(i8Ty_, lhs, {offset}, "ptrsub");
            }
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto* i64ty = i64Ty_;
                llvm::Value* lhsInt = builder_.CreatePtrToInt(lhs, i64ty, "lptr");
                llvm::Value* rhsInt = builder_.CreatePtrToInt(rhs, i64ty, "rptr");
                llvm::Value* diff = builder_.CreateSub(lhsInt, rhsInt, "ptrdiff");
                int64_t elemSize = 1;
                if (bin->lhs->resolved_type && bin->lhs->resolved_type->isPointer()) {
                    auto& pt = std::get<PointerType>(bin->lhs->resolved_type->data);
                    if (pt.pointee) {
                        int64_t s = pt.pointee->sizeInBytes();
                        if (s > 1) elemSize = s;
                    }
                }
                if (elemSize > 1) {
                    llvm::Value* sz = llvm::ConstantInt::get(i64ty, elemSize);
                    diff = builder_.CreateSDiv(diff, sz, "ptrdiff_elem");
                }
                return diff;
            }
            return builder_.CreateSub(lhs, rhs);
        }
        case BinOpKind::Mul: return floatOp ? builder_.CreateFMul(lhs, rhs) : builder_.CreateMul(lhs, rhs);
        case BinOpKind::Div:
            if (floatOp) return builder_.CreateFDiv(lhs, rhs);
            return signedOp ? builder_.CreateSDiv(lhs, rhs) : builder_.CreateUDiv(lhs, rhs);
        case BinOpKind::Mod:
            return signedOp ? builder_.CreateSRem(lhs, rhs) : builder_.CreateURem(lhs, rhs);
        case BinOpKind::BitAnd: return builder_.CreateAnd(lhs, rhs);
        case BinOpKind::BitOr: return builder_.CreateOr(lhs, rhs);
        case BinOpKind::BitXor: return builder_.CreateXor(lhs, rhs);
        case BinOpKind::Shl: return builder_.CreateShl(lhs, rhs);
        case BinOpKind::Shr: return signedOp ? builder_.CreateAShr(lhs, rhs) : builder_.CreateLShr(lhs, rhs);

        case BinOpKind::Eq: {
            llvm::Value* r = floatOp ? builder_.CreateFCmpOEQ(lhs, rhs) : builder_.CreateICmpEQ(lhs, rhs);
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }
        case BinOpKind::Ne: {
            llvm::Value* r = floatOp ? builder_.CreateFCmpONE(lhs, rhs) : builder_.CreateICmpNE(lhs, rhs);
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }
        case BinOpKind::Lt: {
            llvm::Value* r;
            if (floatOp) r = builder_.CreateFCmpOLT(lhs, rhs);
            else r = signedOp ? builder_.CreateICmpSLT(lhs, rhs) : builder_.CreateICmpULT(lhs, rhs);
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }
        case BinOpKind::Le: {
            llvm::Value* r;
            if (floatOp) r = builder_.CreateFCmpOLE(lhs, rhs);
            else r = signedOp ? builder_.CreateICmpSLE(lhs, rhs) : builder_.CreateICmpULE(lhs, rhs);
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }
        case BinOpKind::Gt: {
            llvm::Value* r;
            if (floatOp) r = builder_.CreateFCmpOGT(lhs, rhs);
            else r = signedOp ? builder_.CreateICmpSGT(lhs, rhs) : builder_.CreateICmpUGT(lhs, rhs);
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }
        case BinOpKind::Ge: {
            llvm::Value* r;
            if (floatOp) r = builder_.CreateFCmpOGE(lhs, rhs);
            else r = signedOp ? builder_.CreateICmpSGE(lhs, rhs) : builder_.CreateICmpUGE(lhs, rhs);
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }

        case BinOpKind::LogXor: {
            llvm::Value* lb = toBool(lhs);
            llvm::Value* rb = toBool(rhs);
            llvm::Value* r = builder_.CreateXor(lb, rb, "logxor");
            return builder_.CreateZExt(r, i64Ty_, "cmp");
        }

        default:
            return llvm::ConstantInt::get(i64Ty_, 0);
        }
    }

    case NodeKind::UnaryExpr: {
        auto* un = static_cast<UnaryExpr*>(e);
        switch (un->op) {
        case UnOpKind::Negate: {
            llvm::Value* v = lowerExpr(un->operand);
            if (v->getType()->isFloatingPointTy()) return builder_.CreateFNeg(v);
            return builder_.CreateNeg(v);
        }
        case UnOpKind::LogNot: {
            llvm::Value* v = lowerExpr(un->operand);
            v = toBool(v);
            return builder_.CreateNot(v, "lognot");
        }
        case UnOpKind::BitNot: {
            llvm::Value* v = lowerExpr(un->operand);
            return builder_.CreateNot(v, "bitnot");
        }
        case UnOpKind::PreInc:  return emitIncDec(true,  true,  un);
        case UnOpKind::PreDec:  return emitIncDec(false, true,  un);
        case UnOpKind::PostInc: return emitIncDec(true,  false, un);
        case UnOpKind::PostDec: return emitIncDec(false, false, un);
        case UnOpKind::AddrOf: {
            return lowerExprAddr(un->operand);
        }
        case UnOpKind::Deref: {
            llvm::Value* ptr = lowerExpr(un->operand);
            llvm::Type* loadTy = i64Ty_;
            if (e->resolved_type)
                loadTy = toLLVMType(e->resolved_type);
            if (loadTy->isVoidTy())
                loadTy = i8Ty_;
            return builder_.CreateLoad(loadTy, ptr, "deref");
        }
        }
        return llvm::ConstantInt::get(i64Ty_, 0);
    }

    case NodeKind::CallExpr: {
        auto* call = static_cast<CallExpr*>(e);
        std::string fname;
        llvm::Value* callee = nullptr;
        llvm::Value* this_arg = nullptr;

        if (auto* id = dynamic_cast<IdentifierExpr*>(call->callee)) {
            fname = id->name;
            auto fit = functions_.find(fname);
            if (fit != functions_.end()) {
                callee = fit->second;
            }
            if (fname.find('$') != std::string::npos && currentMethodClass_ && !this_arg) {
                auto thisIt = locals_.find("this");
                if (thisIt != locals_.end()) {
                    this_arg = builder_.CreateLoad(
                        ptrTy_, thisIt->second, "this");
                }
            }
        } else if (auto* fa = dynamic_cast<FieldAccessExpr*>(call->callee)) {
            std::string class_name;
            bool obj_is_ptr_type = false;
            if (fa->object->resolved_type) {
                Type* objTy = fa->object->resolved_type;
                if (objTy->kind == Type::Pointer) {
                    obj_is_ptr_type = true;
                    auto& pt = std::get<PointerType>(objTy->data);
                    if (pt.pointee && pt.pointee->kind == Type::Class) {
                        auto& ct = std::get<ClassType>(pt.pointee->data);
                        if (ct.decl) class_name = ct.decl->name;
                    }
                } else if (objTy->kind == Type::Class) {
                    auto& ct = std::get<ClassType>(objTy->data);
                    if (ct.decl) class_name = ct.decl->name;
                }
            }
            if (fa->is_arrow || obj_is_ptr_type) {
                this_arg = lowerExpr(fa->object);
            } else {
                this_arg = lowerExprAddr(fa->object);
            }
            fname = class_name + "$" + fa->field;
            if (functions_.find(fname) == functions_.end()) {
                std::string base = class_name;
                while (true) {
                    auto cit = classDecls_.find(base);
                    if (cit == classDecls_.end() || cit->second->base_name.empty()) break;
                    base = cit->second->base_name;
                    std::string try_mangled = base + "$" + fa->field;
                    if (functions_.count(try_mangled)) {
                        fname = try_mangled;
                        break;
                    }
                }
            }
            auto fit = functions_.find(fname);
            if (fit != functions_.end()) {
                callee = fit->second;
            }
        }

        if (!callee) {
            callee = lowerExpr(call->callee);
        }

        if (!callee) return llvm::ConstantInt::get(i64Ty_, 0);

        llvm::Function* fn = llvm::dyn_cast<llvm::Function>(callee);
        llvm::FunctionType* fnTy = nullptr;
        if (fn) {
            fnTy = fn->getFunctionType();
        } else {
            Type* calleeAstType = call->callee ? call->callee->resolved_type : nullptr;
            if (calleeAstType && calleeAstType->kind == Type::Func) {
                fnTy = llvm::dyn_cast<llvm::FunctionType>(toLLVMType(calleeAstType));
            } else if (calleeAstType && calleeAstType->kind == Type::Pointer) {
                auto& pt = std::get<PointerType>(calleeAstType->data);
                if (pt.pointee && pt.pointee->kind == Type::Func)
                    fnTy = llvm::dyn_cast<llvm::FunctionType>(toLLVMType(pt.pointee));
            }
            if (!fnTy) {
                std::vector<llvm::Type*> paramTys(call->args.size(),
                                                   i64Ty_);
                fnTy = llvm::FunctionType::get(i64Ty_, paramTys, false);
            }
            if (callee->getType()->isIntegerTy())
                callee = builder_.CreateIntToPtr(callee, ptrTy_);
        }
        if (!fnTy) return llvm::ConstantInt::get(i64Ty_, 0);

        std::vector<llvm::Value*> args;

        if (this_arg) {
            if (this_arg->getType()->isIntegerTy())
                this_arg = builder_.CreateIntToPtr(this_arg, ptrTy_);
            args.push_back(this_arg);
        }

        size_t param_offset = this_arg ? 1 : 0;

        for (size_t i = 0; i < call->args.size(); ++i) {
            llvm::Value* arg = lowerExpr(call->args[i]);
            if (!arg) continue;
            size_t param_idx = i + param_offset;
            if (param_idx < fnTy->getNumParams()) {
                llvm::Type* paramTy = fnTy->getParamType(param_idx);
                if (arg->getType() != paramTy) {
                    if (paramTy->isIntegerTy() && arg->getType()->isIntegerTy()) {
                        unsigned dst = paramTy->getIntegerBitWidth();
                        unsigned src = arg->getType()->getIntegerBitWidth();
                        if (dst > src)
                            arg = builder_.CreateSExt(arg, paramTy);
                        else if (dst < src)
                            arg = builder_.CreateTrunc(arg, paramTy);
                    } else if (paramTy->isFloatingPointTy() && arg->getType()->isIntegerTy()) {
                        arg = builder_.CreateSIToFP(arg, paramTy);
                    } else if (paramTy->isIntegerTy() && arg->getType()->isFloatingPointTy()) {
                        arg = builder_.CreateFPToSI(arg, paramTy);
                    } else if (paramTy->isFloatTy() && arg->getType()->isDoubleTy()) {
                        arg = builder_.CreateFPTrunc(arg, paramTy);
                    } else if (paramTy->isDoubleTy() && arg->getType()->isFloatTy()) {
                        arg = builder_.CreateFPExt(arg, paramTy);
                    } else if (paramTy->isPointerTy() && arg->getType()->isIntegerTy()) {
                        arg = builder_.CreateIntToPtr(arg, paramTy);
                    } else if (paramTy->isIntegerTy() && arg->getType()->isPointerTy()) {
                        arg = builder_.CreatePtrToInt(arg, paramTy);
                    }
                }
            } else if (fnTy->isVarArg()) {
                if (arg->getType()->isIntegerTy() &&
                    arg->getType()->getIntegerBitWidth() < 32) {
                    arg = builder_.CreateSExt(arg, i32Ty_);
                }
            }
            args.push_back(arg);
        }

        if (args.size() < fnTy->getNumParams() && !fname.empty()) {
            auto dit = funcDecls_.find(fname);
            if (dit != funcDecls_.end()) {
                FuncDecl* fd = dit->second;
                for (size_t i = args.size(); i < fnTy->getNumParams(); ++i) {
                    size_t fd_param_idx = i >= param_offset ? i - param_offset : i;
                    if (fd_param_idx >= fd->params.size()) break;
                    if (fd->params[fd_param_idx]->default_value) {
                        llvm::Value* defVal = lowerExpr(fd->params[fd_param_idx]->default_value);
                        if (defVal) {
                            llvm::Type* paramTy = fnTy->getParamType(i);
                            if (defVal->getType() != paramTy) {
                                if (paramTy->isIntegerTy() && defVal->getType()->isIntegerTy()) {
                                    unsigned dst = paramTy->getIntegerBitWidth();
                                    unsigned src = defVal->getType()->getIntegerBitWidth();
                                    if (dst > src) defVal = builder_.CreateSExt(defVal, paramTy);
                                    else if (dst < src) defVal = builder_.CreateTrunc(defVal, paramTy);
                                } else if (paramTy->isFloatingPointTy() && defVal->getType()->isIntegerTy()) {
                                    defVal = builder_.CreateSIToFP(defVal, paramTy);
                                }
                            }
                            args.push_back(defVal);
                        }
                    }
                }
            }
        }

        if (!fname.empty() && holyc_vararg_funcs_.count(fname)) {
            size_t n_fixed = fnTy->getNumParams();
            if (args.size() > n_fixed) {
                size_t n_extra = args.size() - n_fixed;
                auto* i64Ty2 = i64Ty_;
                if (fn_va_count_)
                    builder_.CreateCall(fn_va_count_,
                        {llvm::ConstantInt::get(i64Ty2, (int64_t)n_extra)});
                if (fn_va_store_) {
                    for (size_t i = 0; i < n_extra; ++i) {
                        llvm::Value* v = args[n_fixed + i];
                        if (v->getType()->isPointerTy())
                            v = builder_.CreatePtrToInt(v, i64Ty2);
                        else if (v->getType()->isFloatingPointTy())
                            v = builder_.CreateFPToSI(v, i64Ty2);
                        else if (v->getType() != i64Ty2) {
                            if (v->getType()->getIntegerBitWidth() < 64)
                                v = builder_.CreateSExt(v, i64Ty2);
                            else
                                v = builder_.CreateTrunc(v, i64Ty2);
                        }
                        builder_.CreateCall(fn_va_store_,
                            {llvm::ConstantInt::get(i64Ty2, (int64_t)i), v});
                    }
                }
                args.resize(n_fixed);
            }
        }

        llvm::Value* result;
        if (fn)
            result = builder_.CreateCall(fn, args);
        else
            result = builder_.CreateCall(fnTy, callee, args);
        return result;
    }

    case NodeKind::TernaryExpr: {
        auto* te = static_cast<TernaryExpr*>(e);
        llvm::Value* cond = lowerExpr(te->cond);
        cond = toBool(cond);

        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*ctx_, "tern.then", currentFunc_);
        llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*ctx_, "tern.else", currentFunc_);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*ctx_, "tern.merge", currentFunc_);

        builder_.CreateCondBr(cond, thenBB, elseBB);

        builder_.SetInsertPoint(thenBB);
        llvm::Value* thenVal = lowerExpr(te->then_expr);
        llvm::BasicBlock* thenEnd = builder_.GetInsertBlock();
        builder_.CreateBr(mergeBB);

        builder_.SetInsertPoint(elseBB);
        llvm::Value* elseVal = lowerExpr(te->else_expr);
        llvm::BasicBlock* elseEnd = builder_.GetInsertBlock();
        builder_.CreateBr(mergeBB);

        builder_.SetInsertPoint(mergeBB);

        if (thenVal->getType() != elseVal->getType()) {
            if (thenVal->getType()->isIntegerTy() && elseVal->getType()->isIntegerTy()) {
                unsigned tBits = thenVal->getType()->getIntegerBitWidth();
                unsigned eBits = elseVal->getType()->getIntegerBitWidth();
                llvm::Type* wider = tBits > eBits ? thenVal->getType() : elseVal->getType();
                if (tBits < eBits) {
                    builder_.SetInsertPoint(thenEnd->getTerminator());
                    thenVal = builder_.CreateSExt(thenVal, wider);
                    builder_.SetInsertPoint(mergeBB);
                } else {
                    builder_.SetInsertPoint(elseEnd->getTerminator());
                    elseVal = builder_.CreateSExt(elseVal, wider);
                    builder_.SetInsertPoint(mergeBB);
                }
            }
        }

        llvm::PHINode* phi = builder_.CreatePHI(thenVal->getType(), 2, "tern.val");
        phi->addIncoming(thenVal, thenEnd);
        phi->addIncoming(elseVal, elseEnd);
        return phi;
    }

    case NodeKind::PostfixCastExpr: {
        auto* ce = static_cast<PostfixCastExpr*>(e);
        llvm::Value* v = lowerExpr(ce->expr);
        if (!ce->target_type) return v;
        llvm::Type* dstTy = toLLVMType(ce->target_type);
        if (dstTy->isVoidTy()) return v;
        if (v->getType() == dstTy) return v;
        if (v->getType()->isIntegerTy() && dstTy->isIntegerTy()) {
            unsigned src = v->getType()->getIntegerBitWidth();
            unsigned dst = dstTy->getIntegerBitWidth();
            if (dst > src)
                return isSigned(ce->expr->resolved_type)
                    ? builder_.CreateSExt(v, dstTy) : builder_.CreateZExt(v, dstTy);
            return builder_.CreateTrunc(v, dstTy);
        }
        if (v->getType()->isIntegerTy() && dstTy->isFloatingPointTy())
            return builder_.CreateSIToFP(v, dstTy);
        if (v->getType()->isFloatingPointTy() && dstTy->isIntegerTy())
            return builder_.CreateFPToSI(v, dstTy);
        if (v->getType()->isFloatingPointTy() && dstTy->isFloatingPointTy()) {
            if (v->getType()->isDoubleTy() && dstTy->isFloatTy())
                return builder_.CreateFPTrunc(v, dstTy);
            if (v->getType()->isFloatTy() && dstTy->isDoubleTy())
                return builder_.CreateFPExt(v, dstTy);
        }
        if (v->getType()->isPointerTy() && dstTy->isIntegerTy())
            return builder_.CreatePtrToInt(v, dstTy);
        if (v->getType()->isIntegerTy() && dstTy->isPointerTy())
            return builder_.CreateIntToPtr(v, dstTy);
        return v;
    }

    case NodeKind::SizeofExpr: {
        auto* se = static_cast<SizeofExpr*>(e);
        auto sizeOfType = [this](Type* ty) -> int64_t {
            if (!ty) return 8;
            if (ty->kind == Type::Class) {
                auto& ct = std::get<ClassType>(ty->data);
                std::string sname = ct.decl ? ct.decl->name : "";
                if (!sname.empty())
                    return static_cast<int64_t>(computeStructSize(sname));
            }
            int64_t sz = ty->sizeInBytes();
            return sz > 0 ? sz : 8;
        };
        int64_t sz = 8;
        if (se->target_type) {
            sz = sizeOfType(se->target_type);
        } else if (se->target_expr && se->target_expr->resolved_type) {
            sz = sizeOfType(se->target_expr->resolved_type);
        }
        return llvm::ConstantInt::get(i64Ty_, sz);
    }

    case NodeKind::ArrayIndexExpr: {
        auto* ai = static_cast<ArrayIndexExpr*>(e);
        llvm::Value* addr = lowerExprAddr(ai);
        if (!addr) return llvm::ConstantInt::get(i64Ty_, 0);
        llvm::Type* elemTy = ai->resolved_type ? toLLVMType(ai->resolved_type)
                                               : i64Ty_;
        if (elemTy->isVoidTy() || elemTy->isArrayTy())
            elemTy = i64Ty_;
        return builder_.CreateLoad(elemTy, addr, "arrval");
    }

    case NodeKind::AddrOfExpr: {
        auto* ao = static_cast<AddrOfExpr*>(e);
        return lowerExprAddr(ao->operand);
    }

    case NodeKind::DerefExpr: {
        auto* de = static_cast<DerefExpr*>(e);
        llvm::Value* ptr = lowerExpr(de->operand);
        llvm::Type* loadTy = i64Ty_;
        if (e->resolved_type)
            loadTy = toLLVMType(e->resolved_type);
        if (loadTy->isVoidTy())
            loadTy = i8Ty_;
        return builder_.CreateLoad(loadTy, ptr, "deref");
    }

    case NodeKind::ChainedCmpExpr: {
        auto* cc = static_cast<ChainedCmpExpr*>(e);
        if (cc->operands.size() < 2)
            return llvm::ConstantInt::getTrue(*ctx_);
        llvm::Value* result = llvm::ConstantInt::getTrue(*ctx_);
        llvm::Value* prev = lowerExpr(cc->operands[0]);
        for (size_t i = 0; i < cc->ops.size(); ++i) {
            llvm::Value* next = lowerExpr(cc->operands[i + 1]);
            if (prev->getType() != next->getType()) {
                if (prev->getType()->isIntegerTy() && next->getType()->isIntegerTy()) {
                    unsigned pBits = prev->getType()->getIntegerBitWidth();
                    unsigned nBits = next->getType()->getIntegerBitWidth();
                    if (pBits > nBits) next = builder_.CreateSExt(next, prev->getType());
                    else if (nBits > pBits) prev = builder_.CreateSExt(prev, next->getType());
                }
            }
            llvm::Value* cmp;
            switch (cc->ops[i]) {
            case BinOpKind::Lt: cmp = builder_.CreateICmpSLT(prev, next); break;
            case BinOpKind::Le: cmp = builder_.CreateICmpSLE(prev, next); break;
            case BinOpKind::Gt: cmp = builder_.CreateICmpSGT(prev, next); break;
            case BinOpKind::Ge: cmp = builder_.CreateICmpSGE(prev, next); break;
            case BinOpKind::Eq: cmp = builder_.CreateICmpEQ(prev, next); break;
            case BinOpKind::Ne: cmp = builder_.CreateICmpNE(prev, next); break;
            default: cmp = builder_.CreateICmpSLT(prev, next); break;
            }
            result = builder_.CreateAnd(result, cmp);
            prev = next;
        }
        return builder_.CreateZExt(result, i64Ty_, "ccmp");
    }

    case NodeKind::PowerExpr: {
        auto* pe = static_cast<PowerExpr*>(e);
        llvm::Value* base = lowerExpr(pe->base);
        llvm::Value* exp = lowerExpr(pe->exp);
        if (!base->getType()->isDoubleTy()) {
            if (base->getType()->isFloatTy())
                base = builder_.CreateFPExt(base, f64Ty_);
            else
                base = builder_.CreateSIToFP(base, f64Ty_);
        }
        if (!exp->getType()->isDoubleTy()) {
            if (exp->getType()->isFloatTy())
                exp = builder_.CreateFPExt(exp, f64Ty_);
            else
                exp = builder_.CreateSIToFP(exp, f64Ty_);
        }

        llvm::Function* powFn = mod_->getFunction("llvm.pow.f64");
        if (!powFn) {
            auto* powTy = llvm::FunctionType::get(
                f64Ty_,
                {f64Ty_, f64Ty_}, false);
            powFn = llvm::Function::Create(powTy, llvm::Function::ExternalLinkage, "llvm.pow.f64", mod_.get());
        }
        return builder_.CreateCall(powFn, {base, exp}, "pow");
    }

    case NodeKind::ThrowExpr: {
        auto* te = static_cast<ThrowExpr*>(e);
        llvm::Value* code = te->code ? lowerExpr(te->code)
                                     : llvm::ConstantInt::get(i64Ty_, 1);
        if (!code->getType()->isIntegerTy(64))
            code = builder_.CreateSExt(code, i64Ty_);
        auto* throwFn = functions_["__holyc_throw"];
        builder_.CreateCall(throwFn, {code});
        builder_.CreateUnreachable();
        return llvm::ConstantInt::get(i64Ty_, 0);
    }

    case NodeKind::FieldAccessExpr: {
        auto* fa = static_cast<FieldAccessExpr*>(e);
        if (fa->object->resolved_type && fa->object->resolved_type->kind == Type::IntrinsicUnion) {
            llvm::Value* iuAddr = lowerExprAddr(fa->object);
            if (!iuAddr) return llvm::ConstantInt::get(i64Ty_, 0);
            const std::string& fn = fa->field;
            if (fn == "f64") {
                return builder_.CreateLoad(f64Ty_, iuAddr, "iu_f64");
            }
            return builder_.CreateLoad(i64Ty_, iuAddr, "iu_i64");
        }
        std::string struct_name_bf;
        if (fa->object->resolved_type) {
            Type* objTy = fa->object->resolved_type;
            if (objTy->kind == Type::Pointer) {
                auto& pt = std::get<PointerType>(objTy->data);
                if (pt.pointee && pt.pointee->kind == Type::Class) {
                    auto& ct = std::get<ClassType>(pt.pointee->data);
                    if (ct.decl) struct_name_bf = ct.decl->name;
                }
            } else if (objTy->kind == Type::Class) {
                auto& ct = std::get<ClassType>(objTy->data);
                if (ct.decl) struct_name_bf = ct.decl->name;
            }
        }
        BitFieldInfo bfi = struct_name_bf.empty() ? BitFieldInfo{} : getBitFieldInfo(struct_name_bf, fa->field);

        llvm::Value* addr = lowerExprAddr(fa);
        if (!addr) return llvm::ConstantInt::get(i64Ty_, 0);

        if (bfi.bit_width >= 0) {
            if (bfi.bit_width <= 0 || bfi.bit_width > 64) {
                diag_.error(fa->loc, "bit-field width must be in range [1, 64]");
                return llvm::ConstantInt::get(i64Ty_, 0);
            }
            llvm::Value* word = builder_.CreateLoad(i64Ty_, addr, "bf_word");
            if (bfi.bit_start > 0) {
                llvm::Value* shift = llvm::ConstantInt::get(i64Ty_, bfi.bit_start);
                word = builder_.CreateLShr(word, shift, "bf_shr");
            }
            uint64_t mask = (1ULL << bfi.bit_width) - 1;
            llvm::Value* maskVal = llvm::ConstantInt::get(i64Ty_, mask);
            return builder_.CreateAnd(word, maskVal, "bf_val");
        }

        llvm::Type* fieldTy = fa->resolved_type ? toLLVMType(fa->resolved_type)
                                                : i64Ty_;
        if (fieldTy->isVoidTy()) fieldTy = i64Ty_;
        return builder_.CreateLoad(fieldTy, addr, "field_val");
    }

    case NodeKind::OffsetExpr: {
        auto* oe = static_cast<OffsetExpr*>(e);
        std::string cls_name = oe->class_name;
        if (cls_name == "lastclass" && lastClassDecl_)
            cls_name = lastClassDecl_->name;
        size_t off = computeFieldOffset(cls_name, oe->member_name);
        return llvm::ConstantInt::get(i64Ty_, off);
    }

    default:
        return llvm::ConstantInt::get(i64Ty_, 0);
    }
}

/**
 * @brief किसी statement को LLVM IR पर lower करता है।
 *
 * @param s Lower करने वाला statement। Null हो या current block में पहले से
 *          terminator हो तो यह no-op है।
 */
void LLVMCodegen::lowerStmt(Stmt* s) {
    if (!s) return;

    if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator())
        return;

    emitLocation(s);

    switch (s->nk) {
    case NodeKind::ExprStmt: {
        auto* es = static_cast<ExprStmt*>(s);
        if (es->expr) lowerExpr(es->expr);
        break;
    }

    case NodeKind::CompoundStmt: {
        auto* cs = static_cast<CompoundStmt*>(s);
        for (auto* stmt : cs->stmts) {
            lowerStmt(stmt);
            if (builder_.GetInsertBlock() && builder_.GetInsertBlock()->getTerminator())
                break;
        }
        break;
    }

    case NodeKind::DeclStmt: {
        auto* ds = static_cast<DeclStmt*>(s);
        if (!ds->decl) break;
        if (ds->decl->nk == NodeKind::VarDecl) {
            auto* vd = static_cast<VarDecl*>(ds->decl);
            llvm::Type* ty = toLLVMType(vd->type);
            if (ty->isVoidTy()) ty = i64Ty_;
            if (vd->type && vd->type->kind == Type::Class) {
                auto& ct = std::get<ClassType>(vd->type->data);
                std::string class_name = ct.decl ? ct.decl->name : "";
                size_t struct_size = computeStructSize(class_name);
                ty = llvm::ArrayType::get(i8Ty_, struct_size);
            }

            if (vd->storage == Storage::Static) {
                std::string mangledName = (currentFunc_ ? currentFunc_->getName().str() : "")
                                         + "." + vd->name;
                    llvm::GlobalVariable* gv = mod_->getGlobalVariable(mangledName, true);
                if (!gv) {
                    llvm::Constant* initVal = llvm::Constant::getNullValue(ty);
                    if (vd->init) {
                        if (auto* il = dynamic_cast<IntLiteralExpr*>(vd->init))
                            initVal = llvm::ConstantInt::get(ty, il->value, true);
                        else if (auto* fl = dynamic_cast<FloatLiteralExpr*>(vd->init)) {
                            if (ty->isDoubleTy())
                                initVal = llvm::ConstantFP::get(ty, fl->value);
                            else if (ty->isFloatTy())
                                initVal = llvm::ConstantFP::get(ty, static_cast<float>(fl->value));
                        }
                    }
                    gv = new llvm::GlobalVariable(*mod_, ty, false,
                        llvm::GlobalValue::InternalLinkage, initVal, mangledName);
                }
                locals_[vd->name] = gv;
                break;
            }

            llvm::AllocaInst* alloca = createEntryAlloca(currentFunc_, vd->name, ty);
            locals_[vd->name] = alloca;

            if (debugInfo_ && DI_ && !diScopeStack_.empty()) {
                llvm::DIType* diTy = toDIType(vd->type);
                if (!diTy) diTy = DI_->createBasicType("I64", 64, llvm::dwarf::DW_ATE_signed);
                unsigned line = vd->loc.line ? vd->loc.line : 1;
                auto* diVar = DI_->createAutoVariable(
                    diScopeStack_.back(), vd->name, diFile_, line, diTy);
                DI_->insertDeclare(
                    alloca, diVar, DI_->createExpression(),
                    llvm::DILocation::get(*ctx_, line, vd->loc.col, diScopeStack_.back()),
                    builder_.GetInsertBlock());
            }

            if (vd->init && vd->init->nk == NodeKind::InitListExpr &&
                vd->type && vd->type->kind == Type::Class) {
                auto* il = static_cast<InitListExpr*>(vd->init);
                auto& ct = std::get<ClassType>(vd->type->data);
                builder_.CreateStore(llvm::Constant::getNullValue(ty), alloca);
                if (ct.decl) {
                    size_t fi = 0;
                    std::function<void(ClassDecl*, size_t)> storeFields;
                    storeFields = [&](ClassDecl* cd, size_t base_off) {
                        if (!cd->base_name.empty()) {
                            auto bit = classDecls_.find(cd->base_name);
                            if (bit != classDecls_.end())
                                storeFields(bit->second, 0);
                        }
                        size_t off = base_off;
                        if (!cd->base_name.empty())
                            off = computeStructSize(cd->base_name);
                        for (auto* m : cd->members) {
                            if (m->nk == NodeKind::FieldDecl) {
                                auto* fld = static_cast<FieldDecl*>(m);
                                if (fi >= il->values.size()) break;
                                size_t sz = fld->type ? static_cast<size_t>(fld->type->sizeInBytes()) : 8;
                                if (sz == 0) sz = 8;
                                size_t align = sz > 8 ? 8 : sz;
                                off = (off + align - 1) & ~(align - 1);
                                llvm::Value* fval = lowerExpr(il->values[fi++]);
                                llvm::Value* offsetVal = llvm::ConstantInt::get(
                                    i64Ty_, off);
                                llvm::Value* fieldPtr = builder_.CreateGEP(
                                    i8Ty_, alloca, {offsetVal}, "sinit_field");
                                llvm::Type* storeTy = nullptr;
                                if (fld->type && fld->type->isFloat())
                                    storeTy = f64Ty_;
                                else if (sz == 1) storeTy = i8Ty_;
                                else if (sz == 2) storeTy = i16Ty_;
                                else if (sz == 4) storeTy = i32Ty_;
                                else storeTy = i64Ty_;
                                if (fval->getType() != storeTy) {
                                    if (storeTy->isFloatingPointTy() && fval->getType()->isIntegerTy())
                                        fval = builder_.CreateSIToFP(fval, storeTy);
                                    else if (storeTy->isIntegerTy() && fval->getType()->isFloatingPointTy())
                                        fval = builder_.CreateFPToSI(fval, storeTy);
                                    else if (storeTy->isIntegerTy() && fval->getType()->isIntegerTy()) {
                                        unsigned dst = storeTy->getIntegerBitWidth();
                                        unsigned src = fval->getType()->getIntegerBitWidth();
                                        if (dst > src) fval = builder_.CreateSExt(fval, storeTy);
                                        else if (dst < src) fval = builder_.CreateTrunc(fval, storeTy);
                                    }
                                }
                                builder_.CreateStore(fval, fieldPtr);
                                off += sz;
                            }
                        }
                    };
                    storeFields(ct.decl, 0);
                }
            } else if (vd->init && vd->init->nk == NodeKind::InitListExpr) {
                auto* il = static_cast<InitListExpr*>(vd->init);
                llvm::Type* arrTy = ty;
                llvm::Type* elemTy = i64Ty_;
                size_t arrCount = 1;
                if (vd->type && vd->type->kind == Type::Array) {
                    Type* innerTy = vd->type;
                    while (innerTy && innerTy->kind == Type::Array) {
                        auto& at = std::get<ArrayType>(innerTy->data);
                        if (at.size > 0) arrCount *= static_cast<size_t>(at.size);
                        innerTy = at.element;
                    }
                    if (innerTy) elemTy = toLLVMType(innerTy);
                }
                builder_.CreateStore(llvm::Constant::getNullValue(arrTy), alloca);
                llvm::Type* i64 = i64Ty_;
                for (size_t i = 0; i < il->values.size() && i < arrCount; ++i) {
                    llvm::Value* elem = lowerExpr(il->values[i]);
                    if (elem->getType() != elemTy) {
                        if (elemTy->isIntegerTy() && elem->getType()->isIntegerTy()) {
                            if (elemTy->getIntegerBitWidth() > elem->getType()->getIntegerBitWidth())
                                elem = builder_.CreateSExt(elem, elemTy);
                            else
                                elem = builder_.CreateTrunc(elem, elemTy);
                        } else if (elemTy->isFloatingPointTy() && elem->getType()->isIntegerTy()) {
                            elem = builder_.CreateSIToFP(elem, elemTy);
                        } else if (elemTy->isIntegerTy() && elem->getType()->isFloatingPointTy()) {
                            elem = builder_.CreateFPToSI(elem, elemTy);
                        } else if (elemTy->isFloatTy() && elem->getType()->isDoubleTy()) {
                            elem = builder_.CreateFPTrunc(elem, elemTy);
                        } else if (elemTy->isDoubleTy() && elem->getType()->isFloatTy()) {
                            elem = builder_.CreateFPExt(elem, elemTy);
                        }
                    }
                    llvm::Value* idx = llvm::ConstantInt::get(i64, i);
                    llvm::Value* gep = builder_.CreateInBoundsGEP(arrTy, alloca,
                        {llvm::ConstantInt::get(i64, 0), idx});
                    builder_.CreateStore(elem, gep);
                }
            } else if (vd->init && vd->init->nk == NodeKind::StringLiteralExpr &&
                       vd->type && vd->type->kind == Type::Array) {
                auto* sl = static_cast<StringLiteralExpr*>(vd->init);
                auto& at = std::get<ArrayType>(vd->type->data);
                int arrSz = at.size > 0 ? at.size : static_cast<int>(sl->value.size() + 1);
                builder_.CreateStore(llvm::Constant::getNullValue(ty), alloca);
                llvm::Type* i8 = i8Ty_;
                llvm::Type* i64 = i64Ty_;
                for (int i = 0; i < arrSz && (size_t)i < sl->value.size(); ++i) {
                    llvm::Value* bv = llvm::ConstantInt::get(i8, (uint8_t)sl->value[i]);
                    llvm::Value* gep = builder_.CreateInBoundsGEP(ty, alloca,
                        {llvm::ConstantInt::get(i64, 0), llvm::ConstantInt::get(i64, i)});
                    builder_.CreateStore(bv, gep);
                }
            } else if (vd->init) {
                llvm::Value* init = lowerExpr(vd->init);
                if (init->getType() != ty) {
                    if (ty->isIntegerTy() && init->getType()->isIntegerTy()) {
                        unsigned dst = ty->getIntegerBitWidth();
                        unsigned src = init->getType()->getIntegerBitWidth();
                        if (dst > src)
                            init = builder_.CreateSExt(init, ty);
                        else if (dst < src)
                            init = builder_.CreateTrunc(init, ty);
                    } else if (ty->isFloatingPointTy() && init->getType()->isIntegerTy()) {
                        init = builder_.CreateSIToFP(init, ty);
                    } else if (ty->isIntegerTy() && init->getType()->isFloatingPointTy()) {
                        init = builder_.CreateFPToSI(init, ty);
                    } else if (ty->isFloatTy() && init->getType()->isDoubleTy()) {
                        init = builder_.CreateFPTrunc(init, ty);
                    } else if (ty->isDoubleTy() && init->getType()->isFloatTy()) {
                        init = builder_.CreateFPExt(init, ty);
                    } else if (ty->isPointerTy() && init->getType()->isIntegerTy()) {
                        init = builder_.CreateIntToPtr(init, ty);
                    } else if (ty->isIntegerTy() && init->getType()->isPointerTy()) {
                        init = builder_.CreatePtrToInt(init, ty);
                    }
                }
                builder_.CreateStore(init, alloca);
            } else {
                builder_.CreateStore(llvm::Constant::getNullValue(ty), alloca);
            }
        }
        break;
    }

    case NodeKind::ReturnStmt: {
        auto* rs = static_cast<ReturnStmt*>(s);
        if (rs->value) {
            llvm::Value* v = lowerExpr(rs->value);
            llvm::Type* retTy = currentFunc_->getReturnType();
            if (v->getType() != retTy) {
                if (retTy->isIntegerTy() && v->getType()->isIntegerTy()) {
                    unsigned dst = retTy->getIntegerBitWidth();
                    unsigned src = v->getType()->getIntegerBitWidth();
                    if (dst > src)
                        v = builder_.CreateSExt(v, retTy);
                    else if (dst < src)
                        v = builder_.CreateTrunc(v, retTy);
                } else if (retTy->isFloatingPointTy() && v->getType()->isIntegerTy()) {
                    v = builder_.CreateSIToFP(v, retTy);
                } else if (retTy->isIntegerTy() && v->getType()->isFloatingPointTy()) {
                    v = builder_.CreateFPToSI(v, retTy);
                } else if (retTy->isFloatTy() && v->getType()->isDoubleTy()) {
                    v = builder_.CreateFPTrunc(v, retTy);
                } else if (retTy->isDoubleTy() && v->getType()->isFloatTy()) {
                    v = builder_.CreateFPExt(v, retTy);
                } else if (retTy->isIntegerTy(1) && v->getType()->isIntegerTy()) {
                    v = builder_.CreateTrunc(v, retTy);
                } else if (retTy->isPointerTy() && v->getType()->isIntegerTy()) {
                    v = builder_.CreateIntToPtr(v, retTy);
                } else if (retTy->isIntegerTy() && v->getType()->isPointerTy()) {
                    v = builder_.CreatePtrToInt(v, retTy);
                }
            }
            builder_.CreateRet(v);
        } else {
            if (currentFunc_->getReturnType()->isVoidTy())
                builder_.CreateRetVoid();
            else
                builder_.CreateRet(llvm::Constant::getNullValue(currentFunc_->getReturnType()));
        }
        // Return के बाद dead code: fresh block पर switch करो ताकि बाद के statements
        // (LabelStmts सहित) return block के terminator state को corrupt न करें।
        {
            llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*ctx_, "ret.dead", currentFunc_);
            builder_.SetInsertPoint(deadBB);
        }
        break;
    }

    case NodeKind::IfStmt: {
        auto* is_ = static_cast<IfStmt*>(s);
        llvm::Value* cond = lowerExpr(is_->cond);
        cond = toBool(cond);

        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*ctx_, "if.then", currentFunc_);
        llvm::BasicBlock* elseBB = is_->else_body
            ? llvm::BasicBlock::Create(*ctx_, "if.else", currentFunc_) : nullptr;
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*ctx_, "if.merge", currentFunc_);

        builder_.CreateCondBr(cond, thenBB, elseBB ? elseBB : mergeBB);

        builder_.SetInsertPoint(thenBB);
        lowerStmt(is_->then_body);
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateBr(mergeBB);

        if (elseBB) {
            builder_.SetInsertPoint(elseBB);
            lowerStmt(is_->else_body);
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(mergeBB);
        }

        builder_.SetInsertPoint(mergeBB);
        break;
    }

    case NodeKind::ForStmt: {
        auto* fs = static_cast<ForStmt*>(s);

        if (fs->init) lowerStmt(fs->init);

        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*ctx_, "for.cond", currentFunc_);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*ctx_, "for.body", currentFunc_);
        llvm::BasicBlock* latchBB = llvm::BasicBlock::Create(*ctx_, "for.latch", currentFunc_);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*ctx_, "for.exit", currentFunc_);

        breakTargets_.push_back(exitBB);
        continueTargets_.push_back(latchBB);

        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        if (fs->cond) {
            llvm::Value* cond = lowerExpr(fs->cond);
            cond = toBool(cond);
            builder_.CreateCondBr(cond, bodyBB, exitBB);
        } else {
            builder_.CreateBr(bodyBB);
        }

        builder_.SetInsertPoint(bodyBB);
        if (fs->body) lowerStmt(fs->body);
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateBr(latchBB);

        builder_.SetInsertPoint(latchBB);
        if (fs->post) lowerExpr(fs->post);
        builder_.CreateBr(condBB);

        breakTargets_.pop_back();
        continueTargets_.pop_back();

        builder_.SetInsertPoint(exitBB);
        break;
    }

    case NodeKind::WhileStmt: {
        auto* ws = static_cast<WhileStmt*>(s);
        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*ctx_, "while.cond", currentFunc_);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*ctx_, "while.body", currentFunc_);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*ctx_, "while.exit", currentFunc_);

        breakTargets_.push_back(exitBB);
        continueTargets_.push_back(condBB);

        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value* cond = lowerExpr(ws->cond);
        cond = toBool(cond);
        builder_.CreateCondBr(cond, bodyBB, exitBB);

        builder_.SetInsertPoint(bodyBB);
        if (ws->body) lowerStmt(ws->body);
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateBr(condBB);

        breakTargets_.pop_back();
        continueTargets_.pop_back();

        builder_.SetInsertPoint(exitBB);
        break;
    }

    case NodeKind::DoWhileStmt: {
        auto* dw = static_cast<DoWhileStmt*>(s);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*ctx_, "do.body", currentFunc_);
        llvm::BasicBlock* latchBB = llvm::BasicBlock::Create(*ctx_, "do.latch", currentFunc_);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*ctx_, "do.exit", currentFunc_);

        breakTargets_.push_back(exitBB);
        continueTargets_.push_back(latchBB);

        builder_.CreateBr(bodyBB);

        builder_.SetInsertPoint(bodyBB);
        if (dw->body) lowerStmt(dw->body);
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateBr(latchBB);

        builder_.SetInsertPoint(latchBB);
        llvm::Value* cond = lowerExpr(dw->cond);
        cond = toBool(cond);
        builder_.CreateCondBr(cond, bodyBB, exitBB);

        breakTargets_.pop_back();
        continueTargets_.pop_back();

        builder_.SetInsertPoint(exitBB);
        break;
    }

    case NodeKind::BreakStmt: {
        if (!breakTargets_.empty())
            builder_.CreateBr(breakTargets_.back());
        break;
    }

    case NodeKind::ContinueStmt: {
        if (!continueTargets_.empty())
            builder_.CreateBr(continueTargets_.back());
        break;
    }

    case NodeKind::SwitchStmt: {
        auto* sw = static_cast<SwitchStmt*>(s);
        llvm::Value* expr = lowerExpr(sw->expr);
        if (expr->getType()->isIntegerTy(1))
            expr = builder_.CreateZExt(expr, i64Ty_);

        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*ctx_, "sw.exit", currentFunc_);
        llvm::BasicBlock* defaultBB = exitBB;

        breakTargets_.push_back(exitBB);

        std::vector<llvm::BasicBlock*> caseBBs;
        for (size_t i = 0; i < sw->cases.size(); ++i) {
            caseBBs.push_back(llvm::BasicBlock::Create(*ctx_, "sw.case", currentFunc_));
            if (!sw->cases[i]->value) defaultBB = caseBBs.back();
        }

        auto* switchInst = builder_.CreateSwitch(expr, defaultBB, sw->cases.size());

        for (size_t i = 0; i < sw->cases.size(); ++i) {
            CaseStmt* cs = sw->cases[i];
            if (cs->value) {
                llvm::Value* cv = lowerExpr(cs->value);
                auto* intTy = llvm::dyn_cast<llvm::IntegerType>(expr->getType());
                if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(cv)) {
                    if (ci->getType() != expr->getType())
                        ci = llvm::ConstantInt::get(intTy, ci->getSExtValue());
                    if (cs->range_end) {
                        llvm::Value* cv2 = lowerExpr(cs->range_end);
                        if (auto* ci2 = llvm::dyn_cast<llvm::ConstantInt>(cv2)) {
                            int64_t lo = ci->getSExtValue();
                            int64_t hi = ci2->getSExtValue();
                            if (lo > hi) {
                                diag_.error(cs->loc, "case range start exceeds end value");
                                continue;
                            }
                            int64_t count = hi - lo + 1;
                            static constexpr int64_t kMaxCaseRange = 65536;
                            if (count > kMaxCaseRange) {
                                diag_.error(cs->loc, "case range too wide (max 65536 values)");
                                continue;
                            }
                            for (int64_t v = lo; v <= lo + count - 1; ++v)
                                switchInst->addCase(llvm::ConstantInt::get(intTy, v), caseBBs[i]);
                        }
                    } else {
                        switchInst->addCase(ci, caseBBs[i]);
                    }
                }
            }

            builder_.SetInsertPoint(caseBBs[i]);
            for (auto* stmt : cs->stmts)
                lowerStmt(stmt);
            if (!builder_.GetInsertBlock()->getTerminator()) {
                if (i + 1 < caseBBs.size())
                    builder_.CreateBr(caseBBs[i + 1]);
                else
                    builder_.CreateBr(exitBB);
            }
        }

        breakTargets_.pop_back();
        builder_.SetInsertPoint(exitBB);
        break;
    }

    case NodeKind::StringOutputStmt: {
        lowerStringOutput(static_cast<StringOutputStmt*>(s));
        break;
    }

    case NodeKind::GotoStmt: {
        auto* gs = static_cast<GotoStmt*>(s);
        auto it = labelBlocks_.find(gs->label);
        if (it != labelBlocks_.end()) {
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateBr(it->second);
        }
        // Goto के बाद dead code: fresh block पर switch करो ताकि बाद के statements
        // (LabelStmts सहित) goto block के terminator state को corrupt न करें।
        llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*ctx_, "goto.dead", currentFunc_);
        builder_.SetInsertPoint(deadBB);
        break;
    }

    case NodeKind::LabelStmt: {
        auto* ls = static_cast<LabelStmt*>(s);
        llvm::BasicBlock* labelBB = labelBlocks_[ls->name];
        if (!labelBB) {
            labelBB = llvm::BasicBlock::Create(*ctx_, "label." + ls->name, currentFunc_);
            labelBlocks_[ls->name] = labelBB;
        }
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateBr(labelBB);
        builder_.SetInsertPoint(labelBB);
        if (ls->stmt) lowerStmt(ls->stmt);
        break;
    }

    case NodeKind::ExeBlockStmt:
        // exe {} interpreter mode में source inject करने के लिए Emit() run करता है; JIT/AOT में skip करो।
        break;

    case NodeKind::TryCatchStmt: {
        auto* tc = static_cast<TryCatchStmt*>(s);
        llvm::Function* curFn = builder_.GetInsertBlock()->getParent();
        llvm::LLVMContext& ctx = *ctx_;

        // alloca entry block में होना जरूरी है ताकि jmp_buf setjmp call से ज़्यादा जिए।
        auto* jmpBufTy = llvm::ArrayType::get(llvm::Type::getInt64Ty(ctx), 25);
        llvm::IRBuilder<> entryB(&curFn->getEntryBlock(),
                                  curFn->getEntryBlock().begin());
        llvm::AllocaInst* jmpBuf = entryB.CreateAlloca(jmpBufTy, nullptr, "jmpbuf");
        jmpBuf->setAlignment(llvm::Align(16));

        auto* setjmpFn = functions_["setjmp"];
        llvm::Value* ret = builder_.CreateCall(setjmpFn, {jmpBuf}, "sjlj.ret");

        llvm::Value* isExcept = builder_.CreateICmpNE(
            ret, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0), "is_except");

        llvm::BasicBlock* trySetupBB = llvm::BasicBlock::Create(ctx, "try.setup", curFn);
        llvm::BasicBlock* tryBB      = llvm::BasicBlock::Create(ctx, "try.body",  curFn);
        llvm::BasicBlock* catchBB    = llvm::BasicBlock::Create(ctx, "catch.body", curFn);
        llvm::BasicBlock* mergeBB    = llvm::BasicBlock::Create(ctx, "try.merge",  curFn);

        builder_.CreateCondBr(isExcept, catchBB, trySetupBB);

        builder_.SetInsertPoint(trySetupBB);
        builder_.CreateCall(functions_["__holyc_try_push"], {jmpBuf});
        builder_.CreateBr(tryBB);

        builder_.SetInsertPoint(tryBB);
        if (tc->try_body) lowerStmt(tc->try_body);
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateCall(functions_["__holyc_try_pop"]);
            builder_.CreateBr(mergeBB);
        }

        builder_.SetInsertPoint(catchBB);
        if (tc->catch_body) {
            llvm::Value* excCode = builder_.CreateCall(
                functions_["__holyc_except_code"], {}, "except.code");
            auto* exceptAlloca = createEntryAlloca(curFn, "__except_code",
                                                    llvm::Type::getInt64Ty(ctx));
            builder_.CreateStore(excCode, exceptAlloca);
            locals_["__except_code"] = exceptAlloca;
            lowerStmt(tc->catch_body);
            locals_.erase("__except_code");
        }
        if (!builder_.GetInsertBlock()->getTerminator())
            builder_.CreateBr(mergeBB);

        builder_.SetInsertPoint(mergeBB);
        break;
    }

    case NodeKind::AsmStmt: {
        auto* as = static_cast<AsmStmt*>(s);
        std::string asm_str = as->raw_text;
        while (!asm_str.empty() && (asm_str.back() == ' ' || asm_str.back() == '\n'))
            asm_str.pop_back();
        if (asm_str.empty()) break;

        if (as->outputs.empty() && as->inputs.empty()) {
            auto* asmTy = llvm::FunctionType::get(voidTy_, false);
            auto* ia = llvm::InlineAsm::get(
                asmTy, asm_str, /*constraints=*/"",
                /*hasSideEffects=*/true, /*isAlignStack=*/false,
                llvm::InlineAsm::AD_Intel);
            builder_.CreateCall(ia);
        } else {
            std::string constraints;
            auto addC = [&](const std::string& c) {
                if (!constraints.empty()) constraints += ",";
                constraints += c;
            };
            for (auto& c : as->outputs) addC(c.constraint);
            for (auto& c : as->inputs)  addC(c.constraint);
            for (auto& cl : as->clobbers) addC("~{" + cl + "}");

            std::vector<llvm::Value*> asmArgs;
            for (auto& c : as->inputs) {
                if (c.expr) asmArgs.push_back(lowerExpr(c.expr));
                else asmArgs.push_back(llvm::ConstantInt::get(
                    i64Ty_, 0));
            }

            llvm::Type* i64 = i64Ty_;

            if (as->outputs.size() == 1) {
                std::vector<llvm::Type*> inTys(asmArgs.size(), i64);
                auto* asmFnTy = llvm::FunctionType::get(i64, inTys, false);
                auto* ia = llvm::InlineAsm::get(
                    asmFnTy, asm_str, constraints, /*hasSideEffects=*/true);
                llvm::Value* result = builder_.CreateCall(ia, asmArgs);
                if (as->outputs[0].expr) {
                    llvm::Value* addr = lowerExprAddr(as->outputs[0].expr);
                    if (addr) builder_.CreateStore(result, addr);
                }
            } else if (as->outputs.empty()) {
                std::vector<llvm::Type*> inTys(asmArgs.size(), i64);
                auto* asmFnTy = llvm::FunctionType::get(
                    voidTy_, inTys, false);
                auto* ia = llvm::InlineAsm::get(
                    asmFnTy, asm_str, constraints, /*hasSideEffects=*/true);
                builder_.CreateCall(ia, asmArgs);
            } else {
                std::vector<llvm::Type*> inTys(asmArgs.size(), i64);
                auto* asmFnTy = llvm::FunctionType::get(
                    voidTy_, inTys, false);
                auto* ia = llvm::InlineAsm::get(
                    asmFnTy, asm_str, constraints, /*hasSideEffects=*/true);
                builder_.CreateCall(ia, asmArgs);
            }
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @brief Function body के सभी labels के लिए BasicBlocks create करने का pre-pass।
 *
 * Statements को recursively walk करके हर LabelStmt ढूंढता है और IR lowering शुरू होने से
 * पहले उसका BasicBlock pre-create करता है, जिससे forward goto resolution हो सके।
 *
 * @param s Scan करने वाला statement subtree।
 * @param labelBlocks Label name से उसके pre-created BasicBlock का map।
 * @param fn Compile हो रहा function, नए blocks के parent के रूप में use होता है।
 * @param ctx BasicBlocks create करने के लिए LLVM context।
 */
static void scanLabelsInStmt(Stmt* s, std::unordered_map<std::string, llvm::BasicBlock*>& labelBlocks,
                              llvm::Function* fn, llvm::LLVMContext& ctx) {
    if (!s) return;
    if (s->nk == NodeKind::LabelStmt) {
        auto* ls = static_cast<LabelStmt*>(s);
        if (labelBlocks.find(ls->name) == labelBlocks.end())
            labelBlocks[ls->name] = llvm::BasicBlock::Create(ctx, "label." + ls->name, fn);
        scanLabelsInStmt(ls->stmt, labelBlocks, fn, ctx);
    } else if (s->nk == NodeKind::CompoundStmt) {
        auto* cs = static_cast<CompoundStmt*>(s);
        for (auto* stmt : cs->stmts)
            scanLabelsInStmt(stmt, labelBlocks, fn, ctx);
    } else if (s->nk == NodeKind::IfStmt) {
        auto* is_ = static_cast<IfStmt*>(s);
        scanLabelsInStmt(is_->then_body, labelBlocks, fn, ctx);
        scanLabelsInStmt(is_->else_body, labelBlocks, fn, ctx);
    } else if (s->nk == NodeKind::ForStmt) {
        auto* fs = static_cast<ForStmt*>(s);
        scanLabelsInStmt(fs->body, labelBlocks, fn, ctx);
    } else if (s->nk == NodeKind::WhileStmt) {
        scanLabelsInStmt(static_cast<WhileStmt*>(s)->body, labelBlocks, fn, ctx);
    } else if (s->nk == NodeKind::DoWhileStmt) {
        scanLabelsInStmt(static_cast<DoWhileStmt*>(s)->body, labelBlocks, fn, ctx);
    }
}

/**
 * @brief Top-level declaration (function, global variable, enum, class) को lower करता है।
 *
 * @param node Lower करने वाला top-level AST node।
 */
void LLVMCodegen::lowerTopLevel(Node* node) {
    if (!node) return;

    switch (node->nk) {
    case NodeKind::FuncDecl: {
        auto* fd = static_cast<FuncDecl*>(node);
        if (!fd->body) break;

        auto fit = functions_.find(fd->name);
        if (fit == functions_.end()) break;
        llvm::Function* fn = fit->second;

        if (fn->getName() != fd->name) break;
        if (!fn->empty()) break;

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(*ctx_, "entry", fn);
        builder_.SetInsertPoint(entry);

        decltype(locals_) savedLocals; std::swap(locals_, savedLocals);
        decltype(labelBlocks_) savedLabels; std::swap(labelBlocks_, savedLabels);
        auto savedFunc = currentFunc_;
        currentFunc_ = fn;

        llvm::DISubprogram* diSP = nullptr;
        if (debugInfo_ && DI_) {
            unsigned line = fd->loc.line ? fd->loc.line : 1;
            llvm::DISubroutineType* diST = DI_->createSubroutineType(
                DI_->getOrCreateTypeArray({}));
            diSP = DI_->createFunction(
                diFile_, fd->name, llvm::StringRef(), diFile_, line,
                diST, line, llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            fn->setSubprogram(diSP);
            diScopeStack_.push_back(diSP);
        }

        scanLabelsInStmt(fd->body, labelBlocks_, fn, *ctx_);

        unsigned idx = 0;
        for (auto& arg : fn->args()) {
            ParamDecl* param = fd->params[idx];
            llvm::AllocaInst* alloca = createEntryAlloca(fn, param->name, arg.getType());
            builder_.CreateStore(&arg, alloca);
            locals_[param->name] = alloca;
            arg.setName(param->name);
            ++idx;
        }

        lowerStmt(fd->body);

        if (!builder_.GetInsertBlock()->getTerminator()) {
            if (fn->getReturnType()->isVoidTy())
                builder_.CreateRetVoid();
            else
                builder_.CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
        }

        if (debugInfo_ && diSP)
            diScopeStack_.pop_back();

        std::swap(locals_, savedLocals);
        std::swap(labelBlocks_, savedLabels);
        currentFunc_ = savedFunc;
        break;
    }

    case NodeKind::VarDecl: {
        auto* vd = static_cast<VarDecl*>(node);
        llvm::Type* ty = toLLVMType(vd->type);
        if (ty->isVoidTy()) ty = i64Ty_;

        llvm::Constant* init = llvm::Constant::getNullValue(ty);
        if (vd->init) {
            if (auto* intLit = dynamic_cast<IntLiteralExpr*>(vd->init)) {
                init = llvm::ConstantInt::get(ty, intLit->value, /*isSigned=*/false);
            } else if (auto* charLit = dynamic_cast<CharLiteralExpr*>(vd->init)) {
                init = llvm::ConstantInt::get(ty, charLit->value, /*isSigned=*/false);
            } else if (auto* boolLit = dynamic_cast<BoolLiteralExpr*>(vd->init)) {
                init = llvm::ConstantInt::get(ty, boolLit->value ? 1 : 0, false);
            } else if (auto* floatLit = dynamic_cast<FloatLiteralExpr*>(vd->init)) {
                if (ty->isDoubleTy())
                    init = llvm::ConstantFP::get(ty, floatLit->value);
                else if (ty->isFloatTy())
                    init = llvm::ConstantFP::get(ty, static_cast<float>(floatLit->value));
            } else if (auto* unary = dynamic_cast<UnaryExpr*>(vd->init)) {
                if (unary->op == UnOpKind::Negate && unary->operand) {
                    if (auto* il = dynamic_cast<IntLiteralExpr*>(unary->operand))
                        init = llvm::ConstantInt::get(ty, -(int64_t)il->value, true);
                    else if (auto* fl = dynamic_cast<FloatLiteralExpr*>(unary->operand)) {
                        if (ty->isDoubleTy()) init = llvm::ConstantFP::get(ty, -fl->value);
                        else if (ty->isFloatTy()) init = llvm::ConstantFP::get(ty, -(float)fl->value);
                    }
                }
            }
        }

        auto* gv = new llvm::GlobalVariable(
            *mod_, ty, false,
            llvm::GlobalValue::ExternalLinkage,
            init, vd->name);
        globals_[vd->name] = gv;
        break;
    }

    case NodeKind::EnumDecl: {
        auto* ed = static_cast<EnumDecl*>(node);
        int64_t counter = 0;
        for (auto& m : ed->members) {
            if (m.value) {
                if (m.value->nk == NodeKind::IntLiteralExpr) {
                    counter = static_cast<int64_t>(
                        static_cast<IntLiteralExpr*>(m.value)->value);
                } else if (m.value->nk == NodeKind::UnaryExpr) {
                    auto* ue = static_cast<UnaryExpr*>(m.value);
                    if (ue->op == UnOpKind::Negate && ue->operand->nk == NodeKind::IntLiteralExpr) {
                        counter = -static_cast<int64_t>(
                            static_cast<IntLiteralExpr*>(ue->operand)->value);
                    }
                }
            }
            enumConsts_[m.name] = counter;
            counter++;
        }
        break;
    }

    case NodeKind::ClassDecl:
    case NodeKind::UnionDecl:
    case NodeKind::TypedefDecl:
    case NodeKind::ExternDecl:
        break;

    default:
        break;
    }
}

/**
 * @brief दिए गए translation unit के लिए LLVM module generate करता है।
 *
 * Internal caches clear करता है, सभी runtime builtins declare करता है,
 * forward pass में top-level declarations register करता है, फिर हर node को lower करता है।
 *
 * @param tu Lower करने वाला translation unit। Null नहीं होना चाहिए।
 * @param filename Debug info के लिए source file name; default "input.HC" है।
 * @return Completed LLVM module, JIT या AOT compilation के लिए तैयार।
 */
std::unique_ptr<llvm::Module> LLVMCodegen::generate(TranslationUnit* tu, const std::string& filename) {
    structSizeCache_.clear();
    fieldOffsetCache_.clear();
    llvmTypeCache_.clear();

    mod_ = std::make_unique<llvm::Module>("holyc", *ctx_);
    mod_->setTargetTriple("x86_64-pc-linux-gnu");

    if (debugInfo_) {
        DI_ = std::make_unique<llvm::DIBuilder>(*mod_);
        std::string file = filename.empty() ? "input.HC" : filename;
        std::string dir;
        try {
            auto p = std::filesystem::absolute(file);
            file = p.filename().string();
            dir = p.parent_path().string();
        } catch (...) {
            dir = ".";
        }
        diFile_ = DI_->createFile(file, dir);
        diCU_ = DI_->createCompileUnit(
            llvm::dwarf::DW_LANG_C, diFile_, "holyc", false, "", 0);
        mod_->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                            llvm::DEBUG_METADATA_VERSION);
        mod_->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
    }

    declareBuiltins();

    for (auto* node : tu->decls) {
        if (node->nk == NodeKind::FuncDecl) {
            auto* fd = static_cast<FuncDecl*>(node);
            if (functions_.count(fd->name)) continue;

            llvm::Type* retTy = toLLVMType(fd->return_type);
            std::vector<llvm::Type*> paramTys;
            for (auto* p : fd->params) {
                llvm::Type* pt = toLLVMType(p->type);
                if (pt->isVoidTy()) pt = i64Ty_;
                paramTys.push_back(pt);
            }
            bool llvmVarArg = fd->is_vararg;
            if (fd->is_vararg && fd->body != nullptr) {
                llvmVarArg = false;
                holyc_vararg_funcs_.insert(fd->name);
            }
            auto* fnTy = llvm::FunctionType::get(retTy, paramTys, llvmVarArg);
            auto linkage = (fd->linkage == Linkage::Intern)
                ? llvm::Function::InternalLinkage
                : llvm::Function::ExternalLinkage;
            auto* fn = llvm::Function::Create(fnTy, linkage, fd->name, mod_.get());
            if (fd->linkage == Linkage::Public)
                fn->setVisibility(llvm::GlobalValue::DefaultVisibility);
            else if (fd->linkage != Linkage::Extern && fd->linkage != Linkage::ExternC
                     && fd->linkage != Linkage::ExternAsm && fd->linkage != Linkage::Intern)
                fn->setVisibility(llvm::GlobalValue::HiddenVisibility);
            functions_[fd->name] = fn;
            funcDecls_[fd->name] = fd;
        } else if (node->nk == NodeKind::ClassDecl) {
            auto* cd = static_cast<ClassDecl*>(node);
            classDecls_[cd->name] = cd;
            lastClassDecl_ = cd;
            for (auto* m : cd->members) {
                if (m->nk == NodeKind::FuncDecl) {
                    auto* fd = static_cast<FuncDecl*>(m);
                    std::string mangled = cd->name + "$" + fd->name;
                    if (functions_.count(mangled)) continue;
                    llvm::Type* retTy = toLLVMType(fd->return_type);
                    std::vector<llvm::Type*> paramTys;
                    paramTys.push_back(ptrTy_);
                    for (auto* p : fd->params) {
                        llvm::Type* pt = toLLVMType(p->type);
                        if (pt->isVoidTy()) pt = i64Ty_;
                        paramTys.push_back(pt);
                    }
                    auto* fnTy = llvm::FunctionType::get(retTy, paramTys, fd->is_vararg);
                    auto* fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage, mangled, mod_.get());
                    functions_[mangled] = fn;
                    funcDecls_[mangled] = fd;
                }
            }
        }
    }

    for (auto* node : tu->decls) {
        if (node->nk == NodeKind::EnumDecl)
            lowerTopLevel(node);
    }

    std::vector<Node*> topLevelStmts;
    for (auto* node : tu->decls) {
        if (node->nk == NodeKind::FuncDecl || node->nk == NodeKind::ClassDecl ||
            node->nk == NodeKind::UnionDecl || node->nk == NodeKind::TypedefDecl ||
            node->nk == NodeKind::ExternDecl || node->nk == NodeKind::EnumDecl)
            continue;
        if (node->nk == NodeKind::VarDecl) {
            lowerTopLevel(node);
            continue;
        }
        topLevelStmts.push_back(node);
    }

    for (auto* node : tu->decls) {
        if (node->nk == NodeKind::FuncDecl) {
            lowerTopLevel(node);
        } else if (node->nk == NodeKind::ClassDecl) {
            auto* cd = static_cast<ClassDecl*>(node);
            for (auto* m : cd->members) {
                if (m->nk == NodeKind::FuncDecl) {
                    auto* fd = static_cast<FuncDecl*>(m);
                    if (!fd->body) continue;
                    std::string mangled = cd->name + "$" + fd->name;
                    auto fit = functions_.find(mangled);
                    if (fit == functions_.end()) continue;
                    llvm::Function* fn = fit->second;

                    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*ctx_, "entry", fn);
                    builder_.SetInsertPoint(entry);

                    decltype(locals_) savedLocals; std::swap(locals_, savedLocals);
                    decltype(labelBlocks_) savedLabels; std::swap(labelBlocks_, savedLabels);
                    auto savedFunc = currentFunc_;
                    currentFunc_ = fn;

                    scanLabelsInStmt(fd->body, labelBlocks_, fn, *ctx_);

                    auto argIt = fn->arg_begin();
                    {
                        llvm::AllocaInst* alloca = createEntryAlloca(fn, "this", argIt->getType());
                        builder_.CreateStore(&*argIt, alloca);
                        locals_["this"] = alloca;
                        argIt->setName("this");
                        ++argIt;
                    }
                    unsigned idx = 0;
                    for (; argIt != fn->arg_end() && idx < fd->params.size(); ++argIt, ++idx) {
                        ParamDecl* param = fd->params[idx];
                        llvm::AllocaInst* alloca = createEntryAlloca(fn, param->name, argIt->getType());
                        builder_.CreateStore(&*argIt, alloca);
                        locals_[param->name] = alloca;
                        argIt->setName(param->name);
                    }

                    ClassDecl* savedMethodClass = currentMethodClass_;
                    currentMethodClass_ = cd;
                    lowerStmt(fd->body);
                    currentMethodClass_ = savedMethodClass;

                    if (!builder_.GetInsertBlock()->getTerminator()) {
                        if (fn->getReturnType()->isVoidTy())
                            builder_.CreateRetVoid();
                        else
                            builder_.CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                    }

                    std::swap(locals_, savedLocals);
                    std::swap(labelBlocks_, savedLabels);
                    currentFunc_ = savedFunc;
                }
            }
        }
    }

    bool hasMain = functions_.count("Main") > 0;

    if (hasMain || !topLevelStmts.empty()) {
        auto* mainTy = llvm::FunctionType::get(i32Ty_, false);
        auto* mainFn = llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main", mod_.get());
        auto* entry = llvm::BasicBlock::Create(*ctx_, "entry", mainFn);
        builder_.SetInsertPoint(entry);
        currentFunc_ = mainFn;
        if (debugInfo_ && DI_) {
            auto* diST = DI_->createSubroutineType(DI_->getOrCreateTypeArray({}));
            auto* diSP = DI_->createFunction(
                diFile_, "main", llvm::StringRef(), diFile_, 1,
                diST, 1, llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition);
            mainFn->setSubprogram(diSP);
            diScopeStack_.push_back(diSP);
        }

        for (auto* node : topLevelStmts) {
            if (auto* stmt = dynamic_cast<Stmt*>(node))
                lowerStmt(stmt);
        }

        if (hasMain) {
            if (debugInfo_ && !diScopeStack_.empty())
                builder_.SetCurrentDebugLocation(
                    llvm::DILocation::get(*ctx_, 1, 0, diScopeStack_.back()));
            llvm::Function* holyMain = functions_["Main"];
            llvm::Value* retVal = builder_.CreateCall(holyMain);
            if (holyMain->getReturnType()->isVoidTy()) {
                builder_.CreateRet(llvm::ConstantInt::get(i32Ty_, 0));
            } else {
                if (retVal->getType()->isIntegerTy(64))
                    retVal = builder_.CreateTrunc(retVal, i32Ty_);
                else if (retVal->getType()->isIntegerTy(32))
                    ; // fine
                else
                    retVal = llvm::ConstantInt::get(i32Ty_, 0);
                builder_.CreateRet(retVal);
            }
        } else {
            if (!builder_.GetInsertBlock()->getTerminator())
                builder_.CreateRet(llvm::ConstantInt::get(i32Ty_, 0));
        }

        if (debugInfo_ && !diScopeStack_.empty())
            diScopeStack_.pop_back();
    }

    if (debugInfo_ && DI_)
        DI_->finalize();

#ifndef NDEBUG
    std::string errStr;
    llvm::raw_string_ostream errStream(errStr);
    if (llvm::verifyModule(*mod_, &errStream)) {
        errStream.flush();
        std::fprintf(stderr, "LLVM verification failed:\n%s\n", errStr.c_str());
        return nullptr;
    }
#endif

    return std::move(mod_);
}

} // namespace holyc

#endif // HCC_HAS_LLVM
