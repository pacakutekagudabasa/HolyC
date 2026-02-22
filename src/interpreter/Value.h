#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "ast/Types.h"
#include "interpreter/SmallStr.h"

namespace holyc {

/**
 * @brief Runtime value; FuncPtr के लिए str_val function name भी रखता है।
 *
 * Exactly 32 bytes। Union में से एक रखता है: int64_t, uint64_t, double,
 * bool, या uint8_t* (pointer)। str_val string data या Str और FuncPtr kinds
 * के लिए function name carry करता है।
 */
struct Value {
    enum Kind { Void, Int, UInt, Float, Bool, Ptr, Str, FuncPtr };
    Kind kind = Void;
    union {
        int64_t i;
        uint64_t u;
        double f;
        bool b;
        uint8_t* ptr;
    };
    SmallStr str_val;

    Value() : kind(Void), u(0) {}

    static Value make_void() { Value v; v.kind = Void; v.u = 0; return v; }
    static Value make_int(int64_t val) { Value v; v.kind = Int; v.i = val; return v; }
    static Value make_uint(uint64_t val) { Value v; v.kind = UInt; v.u = val; return v; }
    static Value make_float(double val) { Value v; v.kind = Float; v.f = val; return v; }
    static Value make_bool(bool val) { Value v; v.kind = Bool; v.b = val; return v; }
    static Value make_str(const std::string& s) { Value v; v.kind = Str; v.u = 0; v.str_val = s; return v; }
    static Value make_ptr(uint8_t* p) { Value v; v.kind = Ptr; v.ptr = p; return v; }
    static Value make_funcptr(const std::string& name) { Value v; v.kind = FuncPtr; v.u = 0; v.str_val = name; return v; }

    /**
     * @brief Value को int64_t में coerce करो; pointer values integers की तरह reinterpret होते हैं।
     *
     * @return Value की integer representation।
     */
    int64_t as_int() const {
        switch (kind) {
        case Int:   return i;
        case UInt:  return static_cast<int64_t>(u);
        case Float: return static_cast<int64_t>(f);
        case Bool:  return b ? 1 : 0;
        case Ptr:   return reinterpret_cast<int64_t>(ptr);
        default:    return 0;
        }
    }

    /**
     * @brief Value को uint64_t में coerce करो।
     *
     * @return Value की unsigned integer representation।
     */
    uint64_t as_uint() const {
        switch (kind) {
        case Int:   return static_cast<uint64_t>(i);
        case UInt:  return u;
        case Float: return static_cast<uint64_t>(f);
        case Bool:  return b ? 1 : 0;
        case Ptr:   return reinterpret_cast<uint64_t>(ptr);
        default:    return 0;
        }
    }

    /**
     * @brief Value को double में coerce करो।
     *
     * @return Value की floating-point representation।
     */
    double as_float() const {
        switch (kind) {
        case Int:   return static_cast<double>(i);
        case UInt:  return static_cast<double>(u);
        case Float: return f;
        case Bool:  return b ? 1.0 : 0.0;
        default:    return 0.0;
        }
    }

    /**
     * @brief True लौटाओ अगर value non-zero / non-null / non-empty है।
     *
     * @return Value की boolean interpretation।
     */
    bool as_bool() const {
        switch (kind) {
        case Int:     return i != 0;
        case UInt:    return u != 0;
        case Float:   return f != 0.0;
        case Bool:    return b;
        case Ptr:     return ptr != nullptr;
        case Str:     return !str_val.empty();
        case FuncPtr: return !str_val.empty();
        default:      return false;
        }
    }

    /**
     * @brief Value को primitive kind target में convert करो, appropriate truncation/extension के साथ।
     *
     * @param target Destination primitive kind।
     * @return Requested kind का नया Value।
     */
    Value convert_to(PrimKind target) const {
        switch (target) {
        case PrimKind::U0:
        case PrimKind::I0:
            return make_void();
        case PrimKind::Bool:
            return make_bool(as_bool());
        case PrimKind::I8:  return make_int(static_cast<int8_t>(as_int()));
        case PrimKind::I16: return make_int(static_cast<int16_t>(as_int()));
        case PrimKind::I32: return make_int(static_cast<int32_t>(as_int()));
        case PrimKind::I64: return make_int(as_int());
        case PrimKind::U8:  return make_uint(static_cast<uint8_t>(as_uint()));
        case PrimKind::U16: return make_uint(static_cast<uint16_t>(as_uint()));
        case PrimKind::U32: return make_uint(static_cast<uint32_t>(as_uint()));
        case PrimKind::U64: return make_uint(as_uint());
        case PrimKind::F64: return make_float(as_float());
        case PrimKind::F32: return make_float(static_cast<double>(static_cast<float>(as_float())));
        }
        return make_void();
    }

    /**
     * @brief Value की human-readable representation लौटाओ (REPL और debug के लिए)।
     *
     * @return Value का string form।
     */
    std::string to_string() const {
        switch (kind) {
        case Void:  return "void";
        case Int:   return std::to_string(i);
        case UInt:  return std::to_string(u);
        case Float: return std::to_string(f);
        case Bool:  return b ? "TRUE" : "FALSE";
        case Ptr: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%lX", reinterpret_cast<unsigned long>(ptr));
            return buf;
        }
        case Str:     return str_val;
        case FuncPtr: return std::string("<func:") + str_val.c_str() + ">";
        }
        return "?";
    }
};

static_assert(sizeof(Value) == 32);

} // namespace holyc
