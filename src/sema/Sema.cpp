#include "sema/Sema.h"

#include <cassert>

namespace holyc {

/**
 * @brief सभी built-in primitive type singletons initialize करो और built-in functions register करो।
 *
 * @param diag Error और note messages के लिए diagnostics sink।
 * @param arena सभी AST और type node allocations के लिए arena allocator।
 */
Sema::Sema(Diagnostics& diag, Arena& arena)
    : diag_(diag), arena_(arena)
{
    ty_u0_   = makePrimType(PrimKind::U0);
    ty_u8_   = makePrimType(PrimKind::U8);
    ty_i8_   = makePrimType(PrimKind::I8);
    ty_u16_  = makePrimType(PrimKind::U16);
    ty_i16_  = makePrimType(PrimKind::I16);
    ty_u32_  = makePrimType(PrimKind::U32);
    ty_i32_  = makePrimType(PrimKind::I32);
    ty_u64_  = makePrimType(PrimKind::U64);
    ty_i64_  = makePrimType(PrimKind::I64);
    ty_f64_  = makePrimType(PrimKind::F64);
    ty_f32_  = makePrimType(PrimKind::F32);
    ty_bool_ = makePrimType(PrimKind::Bool);
    ty_u8ptr_ = makePointerType(ty_u8_);
    registerBuiltins();
}

/**
 * @brief दिए गए kind के लिए cached singleton primitive type लौटाओ।
 *
 * @param kind Lookup या create करने वाला primitive kind।
 * @return Canonical Type object का pointer; Arena का owner है।
 */
Type* Sema::makePrimType(PrimKind kind) {
    int idx = static_cast<int>(kind);
    if (prim_type_cache_[idx]) return prim_type_cache_[idx];
    auto* t = arena_.alloc<Type>();
    t->kind = Type::Prim;
    t->data = PrimitiveType{kind};
    prim_type_cache_[idx] = t;
    return t;
}

Type* Sema::makePointerType(Type* pointee) {
    auto* t = arena_.alloc<Type>();
    t->kind = Type::Pointer;
    t->data = PointerType{pointee, 1};
    return t;
}

Type* Sema::makePointerType(Type* pointee, int stars) {
    auto* t = arena_.alloc<Type>();
    t->kind = Type::Pointer;
    t->data = PointerType{pointee, stars > 0 ? stars : 1};
    return t;
}

void Sema::registerBuiltins() {
    auto makeBuiltin = [&](const std::string& name, Type* ret, bool vararg = false) {
        auto* fd = arena_.alloc<FuncDecl>();
        fd->name = name;
        fd->return_type = ret;
        fd->is_vararg = vararg;
        fd->body = nullptr;
        env_.defineGlobal(name, fd);
    };

    auto makeParam = [&](const std::string& name, Type* ty) -> ParamDecl* {
        auto* p = arena_.alloc<ParamDecl>();
        p->name = name;
        p->type = ty;
        p->default_value = nullptr;
        return p;
    };

    auto makeBuiltinP = [&](const std::string& name, Type* ret,
                            std::vector<ParamDecl*> params, bool vararg = false) {
        auto* fd = arena_.alloc<FuncDecl>();
        fd->name = name;
        fd->return_type = ret;
        fd->params = std::move(params);
        fd->is_vararg = vararg;
        fd->body = nullptr;
        env_.defineGlobal(name, fd);
    };

    makeBuiltinP("Print",    ty_u0_,     {makeParam("fmt", ty_u8ptr_)}, true);
    makeBuiltinP("PutChars", ty_u0_,     {makeParam("ch", ty_i64_)});
    makeBuiltinP("GetChar",  ty_i64_,    {});
    makeBuiltinP("GetStr",   ty_u8ptr_,  {makeParam("buf", ty_u8ptr_), makeParam("max", ty_i64_)});
    makeBuiltinP("MAlloc",   ty_u8ptr_,  {makeParam("size", ty_i64_)});
    makeBuiltinP("CAlloc",   ty_u8ptr_,  {makeParam("size", ty_i64_)});
    makeBuiltinP("Free",     ty_u0_,     {makeParam("ptr", ty_u8ptr_)});
    makeBuiltinP("ReAlloc",  ty_u8ptr_,  {makeParam("ptr", ty_u8ptr_), makeParam("size", ty_i64_)});
    makeBuiltinP("MemSet",   ty_u8ptr_,  {makeParam("dst", ty_u8ptr_), makeParam("val", ty_i64_), makeParam("n", ty_i64_)});
    makeBuiltinP("MemCpy",   ty_u8ptr_,  {makeParam("dst", ty_u8ptr_), makeParam("src", ty_u8ptr_), makeParam("n", ty_i64_)});
    makeBuiltinP("MemCmp",   ty_i64_,    {makeParam("a", ty_u8ptr_), makeParam("b", ty_u8ptr_), makeParam("n", ty_i64_)});
    makeBuiltinP("StrLen",   ty_i64_,    {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("StrCmp",   ty_i64_,    {makeParam("a", ty_u8ptr_), makeParam("b", ty_u8ptr_)});
    makeBuiltinP("StrCpy",   ty_u8ptr_,  {makeParam("dst", ty_u8ptr_), makeParam("src", ty_u8ptr_)});
    makeBuiltinP("StrCat",   ty_u8ptr_,  {makeParam("dst", ty_u8ptr_), makeParam("src", ty_u8ptr_)});
    makeBuiltinP("StrCpyN",  ty_u8ptr_,  {makeParam("dst", ty_u8ptr_), makeParam("src", ty_u8ptr_),
                                           makeParam("n", ty_i64_)});
    makeBuiltinP("StrCatN",  ty_u8ptr_,  {makeParam("dst", ty_u8ptr_), makeParam("src", ty_u8ptr_),
                                           makeParam("n", ty_i64_)});
    makeBuiltinP("StrNCmp",  ty_i64_,    {makeParam("a", ty_u8ptr_), makeParam("b", ty_u8ptr_), makeParam("n", ty_i64_)});
    makeBuiltinP("StrFind",  ty_u8ptr_,  {makeParam("needle", ty_u8ptr_), makeParam("haystack", ty_u8ptr_)});
    makeBuiltinP("Sleep",    ty_u0_,     {makeParam("ms", ty_i64_)});
    makeBuiltinP("Yield",    ty_u0_,     {});
    makeBuiltinP("Exit",     ty_u0_,     {makeParam("code", ty_i64_)});
    makeBuiltinP("Abs",      ty_i64_,    {makeParam("x", ty_i64_)});
    makeBuiltinP("Sqrt",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Sin",      ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Cos",      ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Tan",      ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("ToUpper",  ty_u8_,     {makeParam("ch", ty_u8_)});
    makeBuiltinP("ToLower",  ty_u8_,     {makeParam("ch", ty_u8_)});
    makeBuiltinP("RandU64",  ty_u64_,    {});
    makeBuiltinP("Clamp",    ty_i64_,    {makeParam("val", ty_i64_), makeParam("lo", ty_i64_), makeParam("hi", ty_i64_)});
    makeBuiltinP("Min",      ty_i64_,    {makeParam("a", ty_i64_), makeParam("b", ty_i64_)});
    makeBuiltinP("Max",      ty_i64_,    {makeParam("a", ty_i64_), makeParam("b", ty_i64_)});
    makeBuiltinP("Sign",     ty_i64_,    {makeParam("x", ty_i64_)});
    makeBuiltinP("Bsf",      ty_i64_,    {makeParam("x", ty_i64_)});
    makeBuiltinP("Bsr",      ty_i64_,    {makeParam("x", ty_i64_)});
    makeBuiltinP("BCnt",     ty_i64_,    {makeParam("x", ty_i64_)});
    makeBuiltinP("SysDbg",   ty_u0_,     {});
    makeBuiltinP("Emit",     ty_u0_,     {makeParam("text", ty_u8ptr_)});
    makeBuiltinP("DocClear", ty_u0_,     {});
    makeBuiltinP("DocPut",   ty_u0_,     {}, true);
    makeBuiltinP("GrPrint",  ty_u0_,     {makeParam("fmt", ty_u8ptr_)}, true);

    makeBuiltinP("ATan",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("ATan2",    ty_f64_,    {makeParam("y", ty_f64_), makeParam("x", ty_f64_)});
    makeBuiltinP("Exp",      ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Log",      ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Log2",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Log10",    ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Pow",      ty_f64_,    {makeParam("b", ty_f64_), makeParam("e", ty_f64_)});
    makeBuiltinP("Ceil",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Floor",    ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Round",    ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("ACos",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("ASin",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Sinh",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Cosh",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Tanh",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("FMod",     ty_f64_,    {makeParam("x", ty_f64_), makeParam("y", ty_f64_)});
    makeBuiltinP("Cbrt",     ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Trunc",    ty_f64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("Abort",    ty_u0_,     {});
    makeBuiltinP("SeedRand", ty_u0_,     {makeParam("seed", ty_i64_)});
    makeBuiltinP("ToI64",    ty_i64_,    {makeParam("x", ty_f64_)});
    makeBuiltinP("ToF64",    ty_f64_,    {makeParam("x", ty_i64_)});

    makeBuiltinP("StrICmp",  ty_i64_,    {makeParam("a", ty_u8ptr_), makeParam("b", ty_u8ptr_)});
    makeBuiltinP("Str2I64",  ty_i64_,    {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("Str2F64",  ty_f64_,    {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("WildMatch",ty_i64_,    {makeParam("pattern", ty_u8ptr_), makeParam("str", ty_u8ptr_)});
    makeBuiltinP("StrMatch", ty_i64_,    {makeParam("pattern", ty_u8ptr_), makeParam("str", ty_u8ptr_)});
    makeBuiltinP("MStrPrint",ty_u8ptr_,  {makeParam("fmt", ty_u8ptr_)}, true);
    makeBuiltinP("CatPrint", ty_i64_,    {makeParam("dst", ty_u8ptr_), makeParam("fmt", ty_u8ptr_)}, true);

    makeBuiltinP("Bt",           ty_i64_, {makeParam("val", ty_i64_), makeParam("bit", ty_i64_)});
    makeBuiltinP("Bts",          ty_i64_, {makeParam("val", ty_u8ptr_), makeParam("bit", ty_i64_)});
    makeBuiltinP("Btr",          ty_i64_, {makeParam("val", ty_u8ptr_), makeParam("bit", ty_i64_)});
    makeBuiltinP("Btc",          ty_i64_, {makeParam("val", ty_u8ptr_), makeParam("bit", ty_i64_)});
    makeBuiltinP("BFieldExtU32", ty_i64_, {makeParam("val", ty_i64_), makeParam("bit", ty_i64_), makeParam("count", ty_i64_)});

    makeBuiltinP("MSize",      ty_i64_,   {makeParam("ptr", ty_u8ptr_)});
    makeBuiltinP("MAllocIdent",ty_u8ptr_, {makeParam("src", ty_u8ptr_)});

    makeBuiltinP("IsAlpha",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsDigit",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsAlphaNum", ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsUpper",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsLower",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsSpace",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsPunct",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsCtrl",     ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsXDigit",   ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsGraph",    ty_i64_,   {makeParam("ch", ty_i64_)});
    makeBuiltinP("IsPrint",    ty_i64_,   {makeParam("ch", ty_i64_)});

    makeBuiltinP("GetTicks",   ty_i64_,   {});
    makeBuiltinP("Now",        ty_i64_,   {});
    makeBuiltinP("GetTickCount", ty_i64_, {});
    makeBuiltinP("__vararg_count", ty_i64_, {});
    makeBuiltinP("__vararg_get",   ty_i64_, {makeParam("idx", ty_i64_)});

    makeBuiltinP("FileOpen",     ty_i64_,   {makeParam("path", ty_u8ptr_), makeParam("mode", ty_i64_)});
    makeBuiltinP("FileClose",    ty_u0_,    {makeParam("fd", ty_i64_)});
    makeBuiltinP("FileRead",     ty_i64_,   {makeParam("fd", ty_i64_), makeParam("buf", ty_u8ptr_), makeParam("count", ty_i64_)});
    makeBuiltinP("FileWrite",    ty_i64_,   {makeParam("fd", ty_i64_), makeParam("buf", ty_u8ptr_), makeParam("count", ty_i64_)});
    makeBuiltinP("FileSize",     ty_i64_,   {makeParam("fd", ty_i64_)});
    makeBuiltinP("FileSeek",     ty_i64_,   {makeParam("fd", ty_i64_), makeParam("offset", ty_i64_)});
    makeBuiltinP("FileExists",   ty_i64_,   {makeParam("path", ty_u8ptr_)});
    makeBuiltinP("FileReadAll",  ty_u8ptr_, {makeParam("path", ty_u8ptr_), makeParam("size_out", ty_u8ptr_)});
    makeBuiltinP("FileWriteAll", ty_u0_,    {makeParam("path", ty_u8ptr_), makeParam("data", ty_u8ptr_), makeParam("size", ty_i64_)});
    makeBuiltinP("FileDel",      ty_i64_,   {makeParam("path", ty_u8ptr_)});

    makeBuiltinP("StrNew",     ty_u8ptr_, {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("StrDup",     ty_u8ptr_, {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("StrUpr",     ty_u8ptr_, {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("StrLwr",     ty_u8ptr_, {makeParam("s", ty_u8ptr_)});
    makeBuiltinP("SPrint",     ty_i64_,   {makeParam("buf", ty_u8ptr_), makeParam("fmt", ty_u8ptr_)}, true);
    makeBuiltinP("StrNLen",    ty_i64_,   {makeParam("s", ty_u8ptr_), makeParam("max_len", ty_i64_)});

    makeBuiltinP("RandI64",    ty_i64_,   {});

    makeBuiltinP("DirExists",  ty_i64_,   {makeParam("path", ty_u8ptr_)});
    makeBuiltinP("DirMk",      ty_i64_,   {makeParam("path", ty_u8ptr_)});
    makeBuiltinP("FileRename", ty_i64_,   {makeParam("src", ty_u8ptr_), makeParam("dst", ty_u8ptr_)});

    makeBuiltinP("MemMove",    ty_u8ptr_, {makeParam("dst", ty_u8ptr_), makeParam("src", ty_u8ptr_), makeParam("n", ty_i64_)});

    makeBuiltinP("ArgC",       ty_i64_,   {});
    makeBuiltinP("ArgV",       ty_u8ptr_, {});

    makeBuiltinP("StrOcc",   ty_i64_,   {makeParam("s", ty_u8ptr_), makeParam("ch", ty_i64_)});
    makeBuiltinP("StrFirst", ty_u8ptr_, {makeParam("s", ty_u8ptr_), makeParam("ch", ty_i64_)});
    makeBuiltinP("StrLast",  ty_u8ptr_, {makeParam("s", ty_u8ptr_), makeParam("ch", ty_i64_)});
}

int Sema::intRank(PrimKind k) {
    switch (k) {
    case PrimKind::Bool: return 0;
    case PrimKind::U8:  case PrimKind::I8:  return 1;
    case PrimKind::U16: case PrimKind::I16: return 2;
    case PrimKind::U32: case PrimKind::I32: return 3;
    case PrimKind::U64: case PrimKind::I64: return 4;
    default: return -1;
    }
}

/**
 * @brief Usual arithmetic conversions के तहत दो types में से wider लौटाओ।
 *
 * Float integer को beat करता है; F64, F32 को beat करता है; equal rank पर unsigned जीतता है।
 *
 * @param a पहला type operand।
 * @param b दूसरा type operand।
 * @return Promoted result type।
 */
Type* Sema::promoteType(Type* a, Type* b) {
    if (!a || !b) return ty_i64_;
    // F64, F32 को beat करता है; दोनों F32 हों तो → F32
    if (a->isFloat() || b->isFloat()) {
        bool aIsF64 = a->kind == Type::Prim &&
                      std::get<PrimitiveType>(a->data).kind == PrimKind::F64;
        bool bIsF64 = b->kind == Type::Prim &&
                      std::get<PrimitiveType>(b->data).kind == PrimKind::F64;
        if (aIsF64 || bIsF64) return ty_f64_;
        return ty_f32_;
    }
    if (a->isPointer()) return a;
    if (b->isPointer()) return b;
    if (a->isInteger() && b->isInteger()) {
        auto ak = std::get<PrimitiveType>(a->data).kind;
        auto bk = std::get<PrimitiveType>(b->data).kind;
        int ra = intRank(ak), rb = intRank(bk);
        PrimKind wider = (ra >= rb) ? ak : bk;
        if (ra == rb && a->isSigned() != b->isSigned()) {
            switch (ra) {
            case 1: wider = PrimKind::U8;  break;
            case 2: wider = PrimKind::U16; break;
            case 3: wider = PrimKind::U32; break;
            case 4: wider = PrimKind::U64; break;
            default: wider = PrimKind::I64; break;
            }
        }
        return makePrimType(wider);
    }
    return ty_i64_;
}

bool Sema::isAssignable(Type* target, Type* source) {
    if (!target || !source) return true;
    return isImplicitlyConvertible(source, target);
}

bool Sema::isImplicitlyConvertible(Type* from, Type* to) {
    if (!from || !to) return true;
    if (from == to) return true;
    if (from->kind == Type::Prim && to->kind == Type::Prim) {
        auto fk = std::get<PrimitiveType>(from->data).kind;
        auto tk = std::get<PrimitiveType>(to->data).kind;
        if (fk == tk) return true;
        if (to->isInteger() || tk == PrimKind::Bool) return true;
        if (from->isInteger() && to->isFloat()) return true;
        if (from->isFloat() && to->isInteger()) return true;
        if (from->isFloat() && to->isFloat()) return true;
    }
    if (from->isPointer() && to->isPointer()) return true;
    if (from->isInteger() && to->isPointer()) return true;
    if (from->isPointer() && to->isInteger()) return true;
    if (from->kind == Type::Array && to->isPointer()) return true;
    return false;
}

/**
 * @brief First pass top-level declarations forward references के लिए register करता है, फिर सभी nodes walk करता है।
 *
 * @param tu Analyze करने वाला translation unit। Null हो तो no-op।
 */
void Sema::analyze(TranslationUnit* tu) {
    if (!tu) return;
    for (auto* n : tu->decls) {
        switch (n->nk) {
        case NodeKind::FuncDecl: {
            auto* fd = static_cast<FuncDecl*>(n);
            env_.define(fd->name, fd);
            break;
        }
        case NodeKind::ClassDecl: {
            auto* cd = static_cast<ClassDecl*>(n);
            // Qualified calls के लिए mangled "ClassName$Method" names से methods register करो।
            for (auto* m : cd->members) {
                if (m->nk == NodeKind::FuncDecl) {
                    auto* fd = static_cast<FuncDecl*>(m);
                    env_.define(cd->name + "$" + fd->name, fd);
                }
            }
            break;
        }
        case NodeKind::ExternDecl: {
            auto* ed = static_cast<ExternDecl*>(n);
            if (ed->inner && ed->inner->nk == NodeKind::FuncDecl) {
                auto* inner = static_cast<FuncDecl*>(ed->inner);
                env_.define(inner->name, inner);
            }
            break;
        }
        case NodeKind::EnumDecl: {
            auto* enumd = static_cast<EnumDecl*>(n);
            for (auto& m : enumd->members) {
                auto* vd = arena_.alloc<VarDecl>();
                vd->loc = enumd->loc;
                vd->name = m.name;
                vd->type = ty_i64_;
                vd->init = m.value;
                vd->storage = Storage::Default;
                vd->no_warn = false;
                env_.define(m.name, vd);
            }
            break;
        }
        default:
            break;
        }
    }
    for (auto* n : tu->decls) {
        analyzeNode(n);
    }
}

void Sema::analyzeNode(Node* n) {
    if (!n) return;
    if (n->nk >= NodeKind::VarDecl)
        analyzeDecl(static_cast<Decl*>(n));
    else if (n->nk >= NodeKind::CompoundStmt)
        analyzeStmt(static_cast<Stmt*>(n));
    else
        analyzeExpr(static_cast<Expr*>(n));
}

void Sema::analyzeDecl(Decl* d) {
    if (!d) return;
    switch (d->nk) {
    case NodeKind::VarDecl:     analyzeVarDecl(static_cast<VarDecl*>(d)); break;
    case NodeKind::FuncDecl:    analyzeFuncDecl(static_cast<FuncDecl*>(d)); break;
    case NodeKind::ClassDecl:   analyzeClassDecl(static_cast<ClassDecl*>(d)); break;
    case NodeKind::UnionDecl:   analyzeUnionDecl(static_cast<UnionDecl*>(d)); break;
    case NodeKind::TypedefDecl: analyzeTypedefDecl(static_cast<TypedefDecl*>(d)); break;
    case NodeKind::ExternDecl:  analyzeExternDecl(static_cast<ExternDecl*>(d)); break;
    case NodeKind::ParamDecl:   break;
    case NodeKind::FieldDecl:   break;
    case NodeKind::EnumDecl:    break;
    case NodeKind::CompoundDecl: {
        auto* cd = static_cast<CompoundDecl*>(d);
        for (auto* vd : cd->decls) analyzeVarDecl(vd);
        break;
    }
    default: break;
    }
}

void Sema::analyzeVarDecl(VarDecl* d) {
    // typeof(expr) type specifier resolve करो।
    if (d->type && d->type->kind == Type::Typeof) {
        auto& td = std::get<TypeofData>(d->type->data);
        if (td.expr) {
            if (!resolving_typeof_.insert(td.expr).second) {
                diag_.error(d->loc, "circular typeof() dependency");
                d->type = ty_i64_;
            } else {
                Type* resolved = analyzeExpr(td.expr);
                resolving_typeof_.erase(td.expr);
                d->type = resolved ? resolved : d->type;
            }
        }
    }
    // Placeholder ClassDecl को type environment से असली ClassDecl से resolve करो।
    if (d->type && d->type->kind == Type::Class) {
        auto& ct = std::get<ClassType>(d->type->data);
        if (ct.decl) {
            Type* registered = env_.lookupType(ct.decl->name);
            if (registered && registered->kind == Type::Class) {
                auto& rct = std::get<ClassType>(registered->data);
                if (rct.decl) ct.decl = rct.decl;
            }
        }
    }
    if (d->type && d->type->isVoid()) {
        diag_.error(d->loc, "cannot declare variable '" + d->name + "' with type U0");
    }
    if (d->init) {
        if (d->init->nk == NodeKind::InitListExpr) {
            auto* il = static_cast<InitListExpr*>(d->init);
            for (auto* v : il->values)
                analyzeExpr(v);
        } else {
            Type* initTy = analyzeExpr(d->init);
            bool isStrInit = (d->init->nk == NodeKind::StringLiteralExpr &&
                              d->type && d->type->kind == Type::Array);
            if (!isStrInit && d->type && initTy && !isAssignable(d->type, initTy)) {
                diag_.error(d->loc, "cannot initialize '" + d->name + "' of type '" +
                            d->type->toString() + "' with expression of type '" +
                            initTy->toString() + "'");
            }
        }
    }
    env_.define(d->name, d);
}

void Sema::analyzeFuncDecl(FuncDecl* d) {
    if (d->return_type && d->return_type->kind == Type::Typeof) {
        auto& td = std::get<TypeofData>(d->return_type->data);
        if (td.expr) {
            if (!resolving_typeof_.insert(td.expr).second) {
                diag_.error(d->loc, "circular typeof() dependency");
                d->return_type = ty_i64_;
            } else {
                Type* r = analyzeExpr(td.expr);
                resolving_typeof_.erase(td.expr);
                if (r) d->return_type = r;
            }
        }
    }
    for (auto* p : d->params) {
        if (p->type && p->type->kind == Type::Typeof) {
            auto& td = std::get<TypeofData>(p->type->data);
            if (td.expr) {
                if (!resolving_typeof_.insert(td.expr).second) {
                    diag_.error(p->loc, "circular typeof() dependency");
                    p->type = ty_i64_;
                } else {
                    Type* r = analyzeExpr(td.expr);
                    resolving_typeof_.erase(td.expr);
                    if (r) p->type = r;
                }
            }
        }
    }
    env_.define(d->name, d);
    if (current_class_) {
        env_.define(current_class_->name + "$" + d->name, d);
    }

    auto* prevFunc = current_func_;
    current_func_ = d;

    env_.pushScope();

    // Class methods के लिए implicit 'this' pointer inject करो।
    if (current_class_) {
        auto* classType = arena_.alloc<Type>();
        classType->kind = Type::Class;
        classType->data = ClassType{current_class_};
        auto* thisPtrType = arena_.alloc<Type>();
        thisPtrType->kind = Type::Pointer;
        thisPtrType->data = PointerType{classType, 1};
        auto* thisDecl = arena_.alloc<VarDecl>();
        thisDecl->name = "this";
        thisDecl->type = thisPtrType;
        env_.define("this", thisDecl);
    }

    for (auto* p : d->params) {
        if (p->type && p->type->isVoid() && !p->name.empty()) {
            diag_.error(p->loc, "parameter '" + p->name + "' cannot have type U0");
        }
        if (!p->name.empty()) {
            env_.define(p->name, p);
        }
        if (p->default_value) {
            analyzeExpr(p->default_value);
        }
    }

    if (d->body) {
        for (auto* s : d->body->stmts) {
            analyzeStmt(s);
        }
    }

    env_.popScope();
    current_func_ = prevFunc;
}

void Sema::analyzeClassDecl(ClassDecl* d) {
    auto* ct = arena_.alloc<Type>();
    ct->kind = Type::Class;
    ct->data = ClassType{d};
    env_.defineType(d->name, ct);
    env_.define(d->name, d);
    last_class_decl_ = d;

    auto* prevClass = current_class_;
    current_class_ = d;

    for (auto* m : d->members) {
        analyzeDecl(m);
    }
    current_class_ = prevClass;
}

void Sema::analyzeUnionDecl(UnionDecl* d) {
    auto* ut = arena_.alloc<Type>();
    ut->kind = Type::Union;
    ut->data = UnionType{d};
    env_.defineType(d->name, ut);
    env_.define(d->name, d);
}

void Sema::analyzeTypedefDecl(TypedefDecl* d) {
    if (d->type) {
        env_.defineType(d->name, d->type);
    }
}

void Sema::analyzeExternDecl(ExternDecl* d) {
    if (d->inner) {
        analyzeDecl(d->inner);
    }
}

void Sema::analyzeStmt(Stmt* s) {
    if (!s) return;
    switch (s->nk) {
    case NodeKind::CompoundStmt:    analyzeCompoundStmt(static_cast<CompoundStmt*>(s)); break;
    case NodeKind::DeclStmt:        analyzeDeclStmt(static_cast<DeclStmt*>(s)); break;
    case NodeKind::ExprStmt:        analyzeExprStmt(static_cast<ExprStmt*>(s)); break;
    case NodeKind::IfStmt:          analyzeIfStmt(static_cast<IfStmt*>(s)); break;
    case NodeKind::ForStmt:         analyzeForStmt(static_cast<ForStmt*>(s)); break;
    case NodeKind::WhileStmt:       analyzeWhileStmt(static_cast<WhileStmt*>(s)); break;
    case NodeKind::DoWhileStmt:     analyzeDoWhileStmt(static_cast<DoWhileStmt*>(s)); break;
    case NodeKind::SwitchStmt:      analyzeSwitchStmt(static_cast<SwitchStmt*>(s)); break;
    case NodeKind::ReturnStmt:      analyzeReturnStmt(static_cast<ReturnStmt*>(s)); break;
    case NodeKind::LabelStmt:       analyzeLabelStmt(static_cast<LabelStmt*>(s)); break;
    case NodeKind::TryCatchStmt:    analyzeTryCatchStmt(static_cast<TryCatchStmt*>(s)); break;
    case NodeKind::StringOutputStmt: analyzeStringOutputStmt(static_cast<StringOutputStmt*>(s)); break;
    case NodeKind::BreakStmt:
    case NodeKind::ContinueStmt:
    case NodeKind::GotoStmt:
    case NodeKind::AsmStmt:
    case NodeKind::ExeBlockStmt:
        break;
    default: break;
    }
}

void Sema::analyzeCompoundStmt(CompoundStmt* s) {
    env_.pushScope();
    for (auto* stmt : s->stmts)
        analyzeStmt(stmt);
    env_.popScope();
}

void Sema::analyzeDeclStmt(DeclStmt* s) {
    if (s->decl) analyzeDecl(s->decl);
}

void Sema::analyzeExprStmt(ExprStmt* s) {
    if (s->expr) analyzeExpr(s->expr);
}

void Sema::analyzeIfStmt(IfStmt* s) {
    if (s->cond) analyzeExpr(s->cond);
    if (s->then_body) analyzeStmt(s->then_body);
    if (s->else_body) analyzeStmt(s->else_body);
}

void Sema::analyzeForStmt(ForStmt* s) {
    env_.pushScope();
    if (s->init) analyzeStmt(s->init);
    if (s->cond) analyzeExpr(s->cond);
    if (s->post) analyzeExpr(s->post);
    if (s->body) analyzeStmt(s->body);
    env_.popScope();
}

void Sema::analyzeWhileStmt(WhileStmt* s) {
    if (s->cond) analyzeExpr(s->cond);
    if (s->body) analyzeStmt(s->body);
}

void Sema::analyzeDoWhileStmt(DoWhileStmt* s) {
    if (s->body) analyzeStmt(s->body);
    if (s->cond) analyzeExpr(s->cond);
}

void Sema::analyzeSwitchStmt(SwitchStmt* s) {
    if (s->expr) analyzeExpr(s->expr);
    for (auto* c : s->cases) {
        if (c->value) analyzeExpr(c->value);
        for (auto* st : c->stmts)
            analyzeStmt(st);
    }
}

void Sema::analyzeReturnStmt(ReturnStmt* s) {
    Type* retTy = nullptr;
    if (s->value) {
        retTy = analyzeExpr(s->value);
    }
    if (current_func_ && current_func_->return_type) {
        Type* expected = current_func_->return_type;
        if (s->value && retTy) {
            if (!isAssignable(expected, retTy)) {
                diag_.error(s->loc, "return type mismatch: expected '" +
                            expected->toString() + "', got '" + retTy->toString() + "'");
            }
        }
        // HolyC non-void function से बिना value return करने देता है (implicit 0)।
    }
}

void Sema::analyzeLabelStmt(LabelStmt* s) {
    if (s->stmt) analyzeStmt(s->stmt);
}

void Sema::analyzeTryCatchStmt(TryCatchStmt* s) {
    if (s->try_body) analyzeCompoundStmt(s->try_body);
    if (s->catch_body) {
        env_.pushScope();
        auto* ecDecl = arena_.alloc<VarDecl>();
        ecDecl->name = "__except_code";
        ecDecl->type = ty_i64_;
        env_.define("__except_code", ecDecl);
        analyzeCompoundStmt(s->catch_body);
        env_.popScope();
    }
}

void Sema::analyzeStringOutputStmt(StringOutputStmt* s) {
    if (s->format) analyzeExpr(s->format);
    for (auto* a : s->args) analyzeExpr(a);
}

/**
 * @brief Expression का type resolve करो, side effect के रूप में e->resolved_type set करो।
 *
 * @param e Analyze करने वाला expression।
 * @return Expression का resolved type, या e null हो तो nullptr।
 */
Type* Sema::analyzeExpr(Expr* e) {
    if (!e) return nullptr;
    Type* result = nullptr;

    switch (e->nk) {
    case NodeKind::IntLiteralExpr: {
        auto* lit = static_cast<IntLiteralExpr*>(e);
        result = makePrimType(lit->type_hint);
        break;
    }
    case NodeKind::FloatLiteralExpr:
        result = ty_f64_;
        break;
    case NodeKind::StringLiteralExpr:
        result = ty_u8ptr_;
        break;
    case NodeKind::CharLiteralExpr:
        result = ty_i64_; // HolyC में multi-byte chars I64 होते हैं
        break;
    case NodeKind::BoolLiteralExpr:
        result = ty_bool_;
        break;
    case NodeKind::IdentifierExpr: {
        auto* id = static_cast<IdentifierExpr*>(e);
        if (id->name == "lastclass") {
            if (last_class_decl_) {
                id->resolved = last_class_decl_;
                auto* ct = arena_.alloc<Type>();
                ct->kind = Type::Class;
                ct->data = ClassType{last_class_decl_};
                result = ct;
            } else {
                diag_.error(id->loc, "lastclass: no class declared yet");
                result = ty_i64_;
            }
            break;
        }
        Decl* d = env_.lookup(id->name);
        if (!d) {
            // Class method के अंदर implicit 'this' member access try करो।
            bool resolved_as_field = false;
            if (current_class_) {
                ClassDecl* cls = current_class_;
                while (cls && !resolved_as_field) {
                    for (auto* m : cls->members) {
                        if (m->nk == NodeKind::FieldDecl) {
                            auto* fd = static_cast<FieldDecl*>(m);
                            if (fd->name == id->name) {
                                id->resolved = fd;
                                result = fd->type ? fd->type : ty_i64_;
                                resolved_as_field = true;
                                break;
                            }
                        }
                    }
                    if (!resolved_as_field) {
                        if (!cls->base_name.empty()) {
                            Decl* baseDecl = env_.lookup(cls->base_name);
                            cls = (baseDecl && baseDecl->nk == NodeKind::ClassDecl) ? static_cast<ClassDecl*>(baseDecl) : nullptr;
                        } else {
                            cls = nullptr;
                        }
                    }
                }
            }
            if (!resolved_as_field) {
                diag_.error(id->loc, "undeclared identifier '" + id->name + "'");
                std::string suggestion = env_.suggest(id->name);
                if (!suggestion.empty())
                    diag_.note(id->loc, "did you mean '" + suggestion + "'?");
                result = ty_i64_;
            }
        } else {
            id->resolved = d;
            switch (d->nk) {
            case NodeKind::VarDecl:
                result = static_cast<VarDecl*>(d)->type;
                break;
            case NodeKind::ParamDecl:
                result = static_cast<ParamDecl*>(d)->type;
                break;
            case NodeKind::FuncDecl: {
                auto* fd = static_cast<FuncDecl*>(d);
                auto* ft = arena_.alloc<Type>();
                ft->kind = Type::Func;
                FuncType ftd;
                ftd.return_type = fd->return_type;
                ftd.is_vararg = fd->is_vararg;
                for (auto* p : fd->params)
                    ftd.param_types.push_back(p->type);
                ft->data = std::move(ftd);
                result = ft;
                break;
            }
            default:
                result = ty_i64_;
                break;
            }
        }
        break;
    }
    case NodeKind::BinaryExpr:
        result = checkBinaryOp(static_cast<BinaryExpr*>(e));
        break;
    case NodeKind::UnaryExpr:
        result = checkUnaryOp(static_cast<UnaryExpr*>(e));
        break;
    case NodeKind::TernaryExpr: {
        auto* te = static_cast<TernaryExpr*>(e);
        analyzeExpr(te->cond);
        Type* thenTy = analyzeExpr(te->then_expr);
        Type* elseTy = analyzeExpr(te->else_expr);
        result = promoteType(thenTy, elseTy);
        break;
    }
    case NodeKind::ChainedCmpExpr: {
        auto* cc = static_cast<ChainedCmpExpr*>(e);
        for (auto* op : cc->operands)
            analyzeExpr(op);
        result = ty_bool_;
        break;
    }
    case NodeKind::PowerExpr: {
        auto* pe = static_cast<PowerExpr*>(e);
        Type* bt = analyzeExpr(pe->base);
        analyzeExpr(pe->exp);
        result = (bt && bt->isFloat()) ? ty_f64_ : ty_i64_;
        break;
    }
    case NodeKind::CallExpr:
        result = checkCall(static_cast<CallExpr*>(e));
        break;
    case NodeKind::PostfixCastExpr: {
        auto* ce = static_cast<PostfixCastExpr*>(e);
        analyzeExpr(ce->expr);
        result = ce->target_type;
        break;
    }
    case NodeKind::SizeofExpr: {
        auto* se = static_cast<SizeofExpr*>(e);
        if (se->target_expr) analyzeExpr(se->target_expr);
        if (se->target_type && se->target_type->kind == Type::Typeof) {
            auto& td = std::get<TypeofData>(se->target_type->data);
            if (td.expr) {
                if (!resolving_typeof_.insert(td.expr).second) {
                    diag_.error(se->loc, "circular typeof() dependency");
                    se->target_type = ty_i64_;
                } else {
                    Type* r = analyzeExpr(td.expr);
                    resolving_typeof_.erase(td.expr);
                    if (r) se->target_type = r;
                }
            }
        }
        result = ty_i64_;
        break;
    }
    case NodeKind::OffsetExpr:
        result = ty_i64_;
        break;
    case NodeKind::ArrayIndexExpr: {
        auto* ai = static_cast<ArrayIndexExpr*>(e);
        Type* baseTy = analyzeExpr(ai->base);
        analyzeExpr(ai->index);
        if (baseTy && baseTy->isPointer()) {
            auto& pt = std::get<PointerType>(baseTy->data);
            if (pt.stars > 1) {
                result = makePointerType(pt.pointee, pt.stars - 1);
            } else {
                result = pt.pointee;
            }
        } else if (baseTy && baseTy->kind == Type::Array) {
            result = std::get<ArrayType>(baseTy->data).element;
        } else {
            result = ty_i64_;
        }
        break;
    }
    case NodeKind::FieldAccessExpr: {
        auto* fa = static_cast<FieldAccessExpr*>(e);
        Type* objTy = analyzeExpr(fa->object);
        if (!objTy) return ty_i64_;
        result = nullptr;
        Type* structTy = objTy;
        if (structTy && structTy->isPointer()) {
            structTy = std::get<PointerType>(structTy->data).pointee;
        }
        if (structTy && structTy->kind == Type::Class) {
            auto* cd = std::get<ClassType>(structTy->data).decl;
            // Placeholder ClassDecl को type environment से असली ClassDecl से resolve करो।
            if (cd) {
                Type* registeredTy = env_.lookupType(cd->name);
                if (registeredTy && registeredTy->kind == Type::Class) {
                    auto& rct = std::get<ClassType>(registeredTy->data);
                    if (rct.decl) cd = rct.decl;
                }
            }
            if (!cd) {
                diag_.error(fa->loc, "use of incomplete type in field access");
                return ty_i64_;
            }
            if (cd) {
                for (auto* m : cd->members) {
                    if (m->nk == NodeKind::FieldDecl) {
                        auto* fld = static_cast<FieldDecl*>(m);
                        if (fld->name == fa->field) { result = fld->type; break; }
                    } else if (m->nk == NodeKind::FuncDecl) {
                        auto* fd = static_cast<FuncDecl*>(m);
                        if (fd->name == fa->field) {
                            result = fd->return_type ? fd->return_type : ty_i64_;
                            break;
                        }
                    }
                }
            }
        } else if (structTy && structTy->kind == Type::Union) {
            auto* ud = std::get<UnionType>(structTy->data).decl;
            if (ud) {
                for (auto* m : ud->members) {
                    if (m->name == fa->field) {
                        result = m->type;
                        break;
                    }
                }
            }
        } else if (structTy && structTy->kind == Type::IntrinsicUnion) {
            // IU fields: sub-element access के लिए pointer-typed, full-width के लिए scalar।
            const std::string& fn = fa->field;
            if      (fn == "u8"  || fn == "i8")  result = makePointerType(fn[0]=='u' ? ty_u8_  : ty_i8_);
            else if (fn == "u16" || fn == "i16") result = makePointerType(fn[0]=='u' ? ty_u16_ : ty_i16_);
            else if (fn == "u32" || fn == "i32") result = makePointerType(fn[0]=='u' ? ty_u32_ : ty_i32_);
            else if (fn == "u64")                result = ty_u64_;
            else if (fn == "i64")                result = ty_i64_;
            else if (fn == "f64")                result = ty_f64_;
            else result = ty_i64_;
        }
        if (!result) result = ty_i64_;
        break;
    }
    case NodeKind::AddrOfExpr: {
        auto* ao = static_cast<AddrOfExpr*>(e);
        Type* inner = analyzeExpr(ao->operand);
        result = inner ? makePointerType(inner) : ty_u8ptr_;
        break;
    }
    case NodeKind::DerefExpr: {
        auto* de = static_cast<DerefExpr*>(e);
        Type* inner = analyzeExpr(de->operand);
        if (inner && inner->isPointer()) {
            auto& pt = std::get<PointerType>(inner->data);
            if (pt.stars > 1)
                result = makePointerType(pt.pointee, pt.stars - 1);
            else
                result = pt.pointee;
        } else {
            if (inner && !inner->isPointer()) {
                diag_.error(de->loc, "dereferencing non-pointer type '" + inner->toString() + "'");
            }
            result = ty_i64_;
        }
        break;
    }
    case NodeKind::ThrowExpr: {
        auto* te = static_cast<ThrowExpr*>(e);
        if (te->code) analyzeExpr(te->code);
        result = ty_u0_;
        break;
    }
    case NodeKind::InitListExpr: {
        auto* il = static_cast<InitListExpr*>(e);
        for (auto* v : il->values)
            analyzeExpr(v);
        result = ty_u0_;
        break;
    }
    default:
        result = ty_i64_;
        break;
    }

    e->resolved_type = result;
    return result;
}

/**
 * @brief Binary expression type-check करो और उसका result type लौटाओ।
 *
 * @param e Check करने वाला binary expression।
 * @return Binary operation का result type।
 */
Type* Sema::checkBinaryOp(BinaryExpr* e) {
    Type* lhs = analyzeExpr(e->lhs);
    Type* rhs = analyzeExpr(e->rhs);
    if (!lhs || !rhs) return ty_i64_;

    switch (e->op) {
    case BinOpKind::Eq: case BinOpKind::Ne:
    case BinOpKind::Lt: case BinOpKind::Le:
    case BinOpKind::Gt: case BinOpKind::Ge:
        return ty_bool_;

    case BinOpKind::LogAnd: case BinOpKind::LogOr: case BinOpKind::LogXor:
        return ty_bool_;

    case BinOpKind::Assign:
    case BinOpKind::AddAssign: case BinOpKind::SubAssign:
    case BinOpKind::MulAssign: case BinOpKind::DivAssign:
    case BinOpKind::ModAssign:
    case BinOpKind::BitAndAssign: case BinOpKind::BitOrAssign:
    case BinOpKind::BitXorAssign: case BinOpKind::ShlAssign:
    case BinOpKind::ShrAssign:
    case BinOpKind::PPAssign: case BinOpKind::MMAssign:
        if (e->op == BinOpKind::Assign && lhs && rhs && !isAssignable(lhs, rhs)) {
            diag_.error(e->loc, "cannot assign '" + rhs->toString() +
                        "' to '" + lhs->toString() + "'");
        }
        return lhs;

    default:
        return promoteType(lhs, rhs);
    }
}

/**
 * @brief Unary expression type-check करो और उसका result type लौटाओ।
 *
 * @param e Check करने वाला unary expression।
 * @return Unary operation का result type।
 */
Type* Sema::checkUnaryOp(UnaryExpr* e) {
    Type* inner = analyzeExpr(e->operand);
    if (!inner) return ty_i64_;

    switch (e->op) {
    case UnOpKind::AddrOf:
        return inner ? makePointerType(inner) : ty_u8ptr_;
    case UnOpKind::Deref:
        if (inner && inner->isPointer()) {
            auto& pt = std::get<PointerType>(inner->data);
            if (pt.stars > 1)
                return makePointerType(pt.pointee, pt.stars - 1);
            return pt.pointee;
        }
        if (inner)
            diag_.error(e->loc, "dereferencing non-pointer type '" + inner->toString() + "'");
        return ty_i64_;
    case UnOpKind::LogNot:
        return ty_bool_;
    case UnOpKind::Negate:
    case UnOpKind::BitNot:
        return inner ? inner : ty_i64_;
    case UnOpKind::PreInc: case UnOpKind::PreDec:
    case UnOpKind::PostInc: case UnOpKind::PostDec:
        return inner ? inner : ty_i64_;
    default:
        return inner ? inner : ty_i64_;
    }
}

/**
 * @brief Call के लिए argument count और types validate करो; callee का return type लौटाओ।
 *
 * @param e Check करने वाला call expression।
 * @return Called function का return type, या callee type unknown हो तो I64।
 */
Type* Sema::checkCall(CallExpr* e) {
    Type* calleeTy = analyzeExpr(e->callee);

    for (auto* arg : e->args)
        analyzeExpr(arg);

    if (!calleeTy) return ty_i64_;

    if (calleeTy->kind == Type::Func) {
        auto& ft = std::get<FuncType>(calleeTy->data);

        size_t nParams = ft.param_types.size();
        size_t nArgs = e->args.size();

        FuncDecl* fd = nullptr;
        if (e->callee && e->callee->nk == NodeKind::IdentifierExpr) {
            auto* id = static_cast<IdentifierExpr*>(e->callee);
            if (id->resolved && id->resolved->nk == NodeKind::FuncDecl)
                fd = static_cast<FuncDecl*>(id->resolved);
        }

        if (!ft.is_vararg && nArgs > nParams) {
            if (!fd || nArgs > nParams) {
                diag_.error(e->loc, "too many arguments in function call (expected " +
                            std::to_string(nParams) + ", got " + std::to_string(nArgs) + ")");
            }
        }

        if (nArgs < nParams) {
            size_t minArgs = nParams;
            if (fd) {
                minArgs = 0;
                for (auto* p : fd->params) {
                    if (!p->default_value) minArgs++;
                    else break;
                }
            }
            if (nArgs < minArgs) {
                diag_.error(e->loc, "too few arguments in function call (expected at least " +
                            std::to_string(minArgs) + ", got " + std::to_string(nArgs) + ")");
            }
        }

        for (size_t i = 0; i < nArgs && i < nParams; ++i) {
            Type* paramTy = ft.param_types[i];
            Type* argTy = e->args[i]->resolved_type;
            if (paramTy && argTy && !isAssignable(paramTy, argTy)) {
                diag_.error(e->args[i]->loc,
                            "argument type mismatch: expected '" + paramTy->toString() +
                            "', got '" + argTy->toString() + "'");
            }
        }

        return ft.return_type ? ft.return_type : ty_i64_;
    }

    if (calleeTy->isPointer()) {
        auto& pt = std::get<PointerType>(calleeTy->data);
        if (pt.pointee && pt.pointee->kind == Type::Func) {
            auto& ft = std::get<FuncType>(pt.pointee->data);
            return ft.return_type ? ft.return_type : ty_i64_;
        }
        return ty_i64_;
    }

    return ty_i64_;
}

} // namespace holyc
