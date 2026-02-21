#pragma once

#include <string>
#include <variant>
#include <vector>

namespace holyc {

/**
 * @brief HolyC के primitive type kinds — integers, floats, और Bool।
 */
enum class PrimKind {
    U0, I0, U8, I8, U16, I16, U32, I32, U64, I64, F64, F32, Bool
};

enum class IntrinsicUnionKind {
    U8i, I8i, U16i, I16i, U32i, I32i, U64i, I64i, F64i
};

// Forward declarations — आगे use होने वाले types के forward declarations
struct Type;
struct ClassDecl;
struct UnionDecl;
struct Expr;

struct PrimitiveType { PrimKind kind; };                                     //!< Built-in scalar type।
struct IntrinsicUnionType { IntrinsicUnionKind kind; };                      //!< Intrinsic union type (जैसे U8i)।
struct PointerType { Type* pointee; int stars; };                            //!< Star depth वाला pointer।
struct ArrayType { Type* element; int size; /* -1 अगर unsized हो */ };           //!< Fixed-size या unsized array।
struct FuncType { Type* return_type; std::vector<Type*> param_types; bool is_vararg; }; //!< Function pointer type।
struct ClassType { ClassDecl* decl; };                                       //!< User-defined class type।
struct UnionType { UnionDecl* decl; };                                       //!< User-defined union type।
struct TypeofData { Expr* expr; };                                           //!< typeof(expr) type specifier।

using TypeData = std::variant<
    PrimitiveType,
    IntrinsicUnionType,
    PointerType,
    ArrayType,
    FuncType,
    ClassType,
    UnionType,
    TypeofData
>;

/**
 * @brief किसी भी HolyC type को represent करता है; primitive, pointer, array, func, class, union, typeof पर variant।
 */
struct Type {
    enum Kind { Prim, IntrinsicUnion, Pointer, Array, Func, Class, Union, Typeof };
    Kind kind;
    TypeData data;

    /**
     * @brief True लौटाओ अगर यह U0 (void type) है।
     *
     * @return True जब kind Prim हो और PrimKind U0 हो।
     */
    bool isVoid() const {
        return kind == Prim && std::get<PrimitiveType>(data).kind == PrimKind::U0;
    }

    /**
     * @brief True लौटाओ अगर यह कोई भी integer primitive है (void, float, bool को छोड़कर)।
     *
     * @return U8/I8/U16/I16/U32/I32/U64/I64 के लिए True।
     */
    bool isInteger() const {
        if (kind != Prim) return false;
        auto k = std::get<PrimitiveType>(data).kind;
        return k != PrimKind::U0 && k != PrimKind::I0 &&
               k != PrimKind::F64 && k != PrimKind::F32 && k != PrimKind::Bool;
    }

    /**
     * @brief True लौटाओ अगर यह F32 या F64 है।
     *
     * @return True जब kind Prim हो और PrimKind F32 या F64 हो।
     */
    bool isFloat() const {
        if (kind != Prim) return false;
        auto k = std::get<PrimitiveType>(data).kind;
        return k == PrimKind::F64 || k == PrimKind::F32;
    }

    /**
     * @brief True लौटाओ अगर यह signed integer kind है (I8/I16/I32/I64/I0)।
     *
     * @return Signed-integer PrimKinds के लिए True।
     */
    bool isSigned() const {
        if (kind != Prim) return false;
        auto k = std::get<PrimitiveType>(data).kind;
        return k == PrimKind::I8 || k == PrimKind::I16 ||
               k == PrimKind::I32 || k == PrimKind::I64 ||
               k == PrimKind::I0;
    }

    /**
     * @brief True लौटाओ अगर यह pointer type है।
     *
     * @return True जब kind == Pointer हो।
     */
    bool isPointer() const { return kind == Pointer; }

    /**
     * @brief Type का byte size लौटाओ; void, class, func आदि के लिए 0।
     *
     * @return Bytes में size, या opaque/unsized types के लिए 0।
     */
    int sizeInBytes() const {
        switch (kind) {
        case Prim: {
            auto k = std::get<PrimitiveType>(data).kind;
            switch (k) {
            case PrimKind::U0: case PrimKind::I0: return 0;
            case PrimKind::U8: case PrimKind::I8: case PrimKind::Bool: return 1;
            case PrimKind::U16: case PrimKind::I16: return 2;
            case PrimKind::U32: case PrimKind::I32: return 4;
            case PrimKind::U64: case PrimKind::I64: return 8;
            case PrimKind::F64: return 8;
            case PrimKind::F32: return 4;
            }
            return 0;
        }
        case Pointer: return 8;
        case Array: {
            auto& a = std::get<ArrayType>(data);
            if (a.size < 0 || !a.element) return 0;
            return a.element->sizeInBytes() * a.size;
        }
        default: return 0;
        }
    }

    /**
     * @brief Type की human-readable spelling लौटाओ (जैसे "I64*", "U8[10]")।
     *
     * @return String representation जैसे "I64*" या "<func>"।
     */
    std::string toString() const {
        switch (kind) {
        case Prim: {
            auto k = std::get<PrimitiveType>(data).kind;
            switch (k) {
            case PrimKind::U0: return "U0"; case PrimKind::I0: return "I0";
            case PrimKind::U8: return "U8"; case PrimKind::I8: return "I8";
            case PrimKind::U16: return "U16"; case PrimKind::I16: return "I16";
            case PrimKind::U32: return "U32"; case PrimKind::I32: return "I32";
            case PrimKind::U64: return "U64"; case PrimKind::I64: return "I64";
            case PrimKind::F64: return "F64"; case PrimKind::F32: return "F32";
            case PrimKind::Bool: return "Bool";
            }
            return "?";
        }
        case IntrinsicUnion: {
            auto k = std::get<IntrinsicUnionType>(data).kind;
            switch (k) {
            case IntrinsicUnionKind::U8i: return "U8i"; case IntrinsicUnionKind::I8i: return "I8i";
            case IntrinsicUnionKind::U16i: return "U16i"; case IntrinsicUnionKind::I16i: return "I16i";
            case IntrinsicUnionKind::U32i: return "U32i"; case IntrinsicUnionKind::I32i: return "I32i";
            case IntrinsicUnionKind::U64i: return "U64i"; case IntrinsicUnionKind::I64i: return "I64i";
            case IntrinsicUnionKind::F64i: return "F64i";
            }
            return "?";
        }
        case Pointer: {
            auto& p = std::get<PointerType>(data);
            std::string s = p.pointee ? p.pointee->toString() : "?";
            for (int i = 0; i < p.stars; ++i) s += "*";
            return s;
        }
        case Array: {
            auto& a = std::get<ArrayType>(data);
            std::string s = a.element ? a.element->toString() : "?";
            s += "[";
            if (a.size >= 0) s += std::to_string(a.size);
            s += "]";
            return s;
        }
        case Func: return "<func>";
        case Class: return "<class>";
        case Union: return "<union>";
        case Typeof: return "<typeof>";
        }
        return "?";
    }
};

} // namespace holyc
