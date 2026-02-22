#include "interpreter/Interpreter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <strings.h>  // strcasecmp
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int    g_argc = 0;
char** g_argv = nullptr;

namespace holyc {

Interpreter::Interpreter(Diagnostics& diag) : diag_(diag) {
    call_stack_.reserve(64);
    static_initialized_.reserve(256);
    static_vars_.reserve(256);
    registerBuiltins();
}

/**
 * @brief Value से allocation किए बिना C-string view लौटाता है।
 *
 * Str-kind values के लिए str_val prefer करता है; Ptr-kind values के लिए heap pointer पर fallback।
 *
 * @param v वह value जिससे string pointer extract करना है।
 * @return null-terminated C string का pointer, या "" अगर कोई representation available नहीं।
 */
static const char* valueToStr(const Value& v) {
    if (v.kind == Value::Str) return v.str_val.c_str();
    if (v.kind == Value::Ptr && v.ptr) return reinterpret_cast<const char*>(v.ptr);
    return "";
}

/**
 * @brief Value को std::string में convert करता है।
 *
 * Inline str_val और heap pointer दोनों representations handle करता है।
 *
 * @param v Convert करने वाला value।
 * @return String content, या empty string अगर कोई representation available नहीं।
 */
static std::string valueToStdStr(const Value& v) {
    if (v.kind == Value::Str) return std::string(v.str_val);
    if (v.kind == Value::Ptr && v.ptr) return reinterpret_cast<const char*>(v.ptr);
    return "";
}

#define MATH1(hcname, fn) \
    builtins_[hcname] = [](const std::vector<Value>& args) -> Value { \
        if (args.empty()) return Value::make_float(0.0); \
        return Value::make_float(std::fn(args[0].as_float())); \
    }
#define MATH2(hcname, fn) \
    builtins_[hcname] = [](const std::vector<Value>& args) -> Value { \
        if (args.size() < 2) return Value::make_float(0.0); \
        return Value::make_float(std::fn(args[0].as_float(), args[1].as_float())); \
    }

void Interpreter::registerBuiltins() {
    builtins_["Print"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_void();
        std::string fmt = args[0].str_val;
        std::string out = formatPrint(fmt, args, 1);
        std::fputs(out.c_str(), stdout);
        return Value::make_void();
    };

    builtins_["MAlloc"] = [this](const std::vector<Value>& args) -> Value {
        size_t n = args.empty() ? 0 : static_cast<size_t>(args[0].as_uint());
        return Value::make_ptr(heap_.alloc(n));
    };

    builtins_["CAlloc"] = [this](const std::vector<Value>& args) -> Value {
        size_t n = args.empty() ? 0 : static_cast<size_t>(args[0].as_uint());
        return Value::make_ptr(heap_.calloc(n));
    };

    builtins_["Free"] = [this](const std::vector<Value>& args) -> Value {
        if (!args.empty()) heap_.free(args[0].ptr);
        return Value::make_void();
    };

    builtins_["StrLen"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        if (args[0].kind == Value::Str)
            return Value::make_int(static_cast<int64_t>(args[0].str_val.size()));
        if (args[0].kind == Value::Ptr && args[0].ptr)
            return Value::make_int(static_cast<int64_t>(std::strlen(reinterpret_cast<const char*>(args[0].ptr))));
        return Value::make_int(0);
    };

    builtins_["Abs"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        if (args[0].kind == Value::Float) return Value::make_float(std::fabs(args[0].f));
        int64_t v = args[0].as_int();
        if (v == INT64_MIN) return Value::make_int(INT64_MAX);
        return Value::make_int(v < 0 ? -v : v);
    };

    builtins_["AbsI64"] = builtins_["Abs"];

    MATH1("Sqrt", sqrt);
    MATH1("Sin",  sin);
    MATH1("Cos",  cos);

    builtins_["Exit"] = [](const std::vector<Value>& args) -> Value {
        int code = args.empty() ? 0 : static_cast<int>(args[0].as_int());
        std::exit(code);
        return Value::make_void();
    };

    builtins_["Emit"] = [this](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            std::string text;
            if (args[0].kind == Value::Str)
                text = args[0].str_val;
            else if (args[0].kind == Value::Ptr && args[0].ptr)
                text = reinterpret_cast<const char*>(args[0].ptr);
            else
                text = args[0].to_string();
            emit_buffer_ += text;
        }
        return Value::make_void();
    };

    MATH1("Tan",   tan);
    MATH1("ATan",  atan);
    MATH2("ATan2", atan2);
    builtins_["Exp"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_float(1.0);
        return Value::make_float(std::exp(args[0].as_float()));
    };
    MATH1("Log",   log);
    MATH1("Log2",  log2);
    MATH1("Log10", log10);
    builtins_["Pow"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_float(1.0);
        return Value::make_float(std::pow(args[0].as_float(), args[1].as_float()));
    };
    MATH1("Ceil",  ceil);
    MATH1("Floor", floor);
    MATH1("Round", round);
    MATH1("ACos",  acos);
    MATH1("ASin",  asin);
    MATH1("Sinh",  sinh);
    MATH1("Cosh",  cosh);
    MATH1("Tanh",  tanh);
    MATH2("FMod",  fmod);
    MATH1("Cbrt",  cbrt);
    MATH1("Trunc", trunc);
    builtins_["Abort"] = [](const std::vector<Value>&) -> Value {
        std::abort();
        return Value::make_void();
    };
    builtins_["RandU64"] = [](const std::vector<Value>&) -> Value {
        uint64_t r = ((uint64_t)(unsigned)std::rand() << 32) | (uint64_t)(unsigned)std::rand();
        return Value::make_int(static_cast<int64_t>(r));
    };
    builtins_["SeedRand"] = [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) std::srand((unsigned)args[0].as_int());
        return Value::make_void();
    };
    builtins_["ToI64"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(static_cast<int64_t>(args[0].as_float()));
    };
    builtins_["ToF64"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_float(0.0);
        return Value::make_float(static_cast<double>(args[0].as_int()));
    };
    builtins_["Clamp"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_int(0);
        int64_t v = args[0].as_int(), lo = args[1].as_int(), hi = args[2].as_int();
        return Value::make_int(v < lo ? lo : v > hi ? hi : v);
    };
    builtins_["Min"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        return Value::make_int(std::min(args[0].as_int(), args[1].as_int()));
    };
    builtins_["Max"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        return Value::make_int(std::max(args[0].as_int(), args[1].as_int()));
    };
    builtins_["Sign"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        int64_t v = args[0].as_int();
        return Value::make_int((v > 0) - (v < 0));
    };

    builtins_["StrCmp"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        return Value::make_int(std::strcmp(valueToStr(args[0]), valueToStr(args[1])));
    };
    builtins_["StrCpy"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_ptr(nullptr);
        if (args[0].kind == Value::Ptr && args[0].ptr) {
            std::string s = valueToStdStr(args[1]);
            std::memcpy(args[0].ptr, s.c_str(), s.size() + 1);
        }
        return args.empty() ? Value::make_ptr(nullptr) : args[0];
    };
    builtins_["StrCat"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_ptr(nullptr);
        if (args[0].kind == Value::Ptr && args[0].ptr) {
            char* dst = reinterpret_cast<char*>(args[0].ptr);
            std::string s = valueToStdStr(args[1]);
            size_t cap = heap_.size(args[0].ptr);
            if (cap > 0) {
                size_t dst_len = std::strlen(dst);
                size_t src_len = s.size();
                if (dst_len + src_len + 1 > cap)
                    src_len = cap > dst_len + 1 ? cap - dst_len - 1 : 0;
                std::memcpy(dst + dst_len, s.c_str(), src_len);
                dst[dst_len + src_len] = '\0';
            } else {
                std::strcat(dst, s.c_str());
            }
        }
        return args[0];
    };
    builtins_["StrCpyN"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return args.empty() ? Value::make_ptr(nullptr) : args[0];
        if (args[0].kind != Value::Ptr || !args[0].ptr) return args[0];
        char* dst = reinterpret_cast<char*>(args[0].ptr);
        int64_t n = args[2].as_int();
        if (n <= 0) return args[0];
        size_t cap = heap_.size(args[0].ptr);
        if (cap > 0 && (size_t)n > cap) n = (int64_t)cap;
        const char* src = nullptr;
        if (args[1].kind == Value::Str) src = args[1].str_val.c_str();
        else src = reinterpret_cast<const char*>(args[1].ptr);
        if (!src) return args[0];
        strncpy(dst, src, (size_t)n);
        dst[n - 1] = '\0';
        return args[0];
    };

    builtins_["StrCatN"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return args.empty() ? Value::make_ptr(nullptr) : args[0];
        if (args[0].kind != Value::Ptr || !args[0].ptr) return args[0];
        char* dst = reinterpret_cast<char*>(args[0].ptr);
        int64_t n = args[2].as_int();
        if (n <= 0) return args[0];
        size_t cap = heap_.size(args[0].ptr);
        size_t limit = (cap > 0 && (size_t)n > cap) ? cap : (size_t)n;
        const char* src = nullptr;
        if (args[1].kind == Value::Str) src = args[1].str_val.c_str();
        else src = reinterpret_cast<const char*>(args[1].ptr);
        if (!src) return args[0];
        size_t dst_len = strnlen(dst, limit);
        if (dst_len + 1 < limit)
            strncat(dst, src, limit - dst_len - 1);
        return args[0];
    };

    builtins_["StrICmp"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        return Value::make_int(strcasecmp(valueToStr(args[0]), valueToStr(args[1])));
    };
    builtins_["Str2I64"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        try {
            if (args[0].kind == Value::Str) return Value::make_int(std::stoll(args[0].str_val.c_str()));
            if (args[0].kind == Value::Ptr && args[0].ptr)
                return Value::make_int(std::stoll(reinterpret_cast<const char*>(args[0].ptr)));
        } catch (...) {}
        return Value::make_int(0);
    };
    builtins_["Str2F64"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_float(0.0);
        try {
            if (args[0].kind == Value::Str) return Value::make_float(std::stod(args[0].str_val.c_str()));
            if (args[0].kind == Value::Ptr && args[0].ptr)
                return Value::make_float(std::stod(reinterpret_cast<const char*>(args[0].ptr)));
        } catch (...) {}
        return Value::make_float(0.0);
    };
    builtins_["WildMatch"] = builtins_["StrMatch"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        std::string pat = valueToStdStr(args[0]);
        std::string str = valueToStdStr(args[1]);
        std::function<bool(const char*, const char*)> match =
            [&match](const char* p, const char* s) -> bool {
                while (*p && *s) {
                    if (*p == '*') {
                        while (*p == '*') p++;
                        if (!*p) return true;
                        while (*s) { if (match(p, s)) return true; s++; }
                        return false;
                    }
                    if (*p != '?' && *p != *s) return false;
                    p++; s++;
                }
                while (*p == '*') p++;
                return *p == '\0' && *s == '\0';
            };
        return Value::make_int(match(pat.c_str(), str.c_str()) ? 1 : 0);
    };
    builtins_["MStrPrint"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_ptr(nullptr);
        std::string fmt = valueToStdStr(args[0]);
        std::string out = formatPrint(fmt, args, 1);
        uint8_t* p = heap_.alloc(out.size() + 1);
        std::memcpy(p, out.c_str(), out.size() + 1);
        return Value::make_ptr(p);
    };
    builtins_["StrPrintf"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_ptr(nullptr);
        std::string fmt = valueToStdStr(args[0]);
        std::string out = formatPrint(fmt, args, 1);
        uint8_t* p = heap_.alloc(out.size() + 1);
        std::memcpy(p, out.c_str(), out.size() + 1);
        return Value::make_ptr(p);
    };
    builtins_["CatPrint"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        char* dst = nullptr;
        if (args[0].kind == Value::Ptr && args[0].ptr)
            dst = reinterpret_cast<char*>(args[0].ptr);
        std::string fmt = valueToStdStr(args[1]);
        std::string out = formatPrint(fmt, args, 2);
        if (dst) {
            size_t cap = heap_.size(args[0].ptr);
            if (cap > 0) {
                size_t dst_len = std::strlen(dst);
                size_t src_len = out.size();
                if (dst_len + src_len + 1 > cap)
                    src_len = cap > dst_len + 1 ? cap - dst_len - 1 : 0;
                std::memcpy(dst + dst_len, out.c_str(), src_len);
                dst[dst_len + src_len] = '\0';
            } else {
                std::strcat(dst, out.c_str());
            }
        }
        return Value::make_int(dst ? (int64_t)std::strlen(dst) : 0);
    };

    builtins_["Bsf"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(-1);
        uint64_t v = (uint64_t)args[0].as_int();
        if (v == 0) return Value::make_int(-1);
        return Value::make_int(__builtin_ctzll(v));
    };
    builtins_["Bsr"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(-1);
        uint64_t v = (uint64_t)args[0].as_int();
        if (v == 0) return Value::make_int(-1);
        return Value::make_int(63 - __builtin_clzll(v));
    };
    builtins_["BCnt"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(__builtin_popcountll((uint64_t)args[0].as_int()));
    };
    builtins_["Bt"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        int64_t v = args[0].as_int(), bit = args[1].as_int();
        if (bit < 0 || bit >= 64) return Value::make_int(0);
        return Value::make_int((v >> bit) & 1);
    };
    builtins_["Bts"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args[0].kind != Value::Ptr) return Value::make_int(0);
        auto* vp = reinterpret_cast<Value*>(args[0].ptr);
        int64_t bit = args[1].as_int();
        if (bit < 0 || bit >= 64) return Value::make_int(0);
        int64_t cur = vp->as_int();
        int64_t old = (cur >> bit) & 1;
        cur |= (int64_t(1) << bit);
        vp->i = cur; vp->kind = Value::Int;
        return Value::make_int(old);
    };
    builtins_["Btr"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args[0].kind != Value::Ptr) return Value::make_int(0);
        auto* vp = reinterpret_cast<Value*>(args[0].ptr);
        int64_t bit = args[1].as_int();
        if (bit < 0 || bit >= 64) return Value::make_int(0);
        int64_t cur = vp->as_int();
        int64_t old = (cur >> bit) & 1;
        cur &= ~(int64_t(1) << bit);
        vp->i = cur; vp->kind = Value::Int;
        return Value::make_int(old);
    };
    builtins_["Btc"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args[0].kind != Value::Ptr) return Value::make_int(0);
        auto* vp = reinterpret_cast<Value*>(args[0].ptr);
        int64_t bit = args[1].as_int();
        if (bit < 0 || bit >= 64) return Value::make_int(0);
        int64_t cur = vp->as_int();
        int64_t old = (cur >> bit) & 1;
        cur ^= (int64_t(1) << bit);
        vp->i = cur; vp->kind = Value::Int;
        return Value::make_int(old);
    };
    builtins_["BFieldExtU32"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_int(0);
        int64_t val = args[0].as_int(), bit = args[1].as_int(), count = args[2].as_int();
        if (bit < 0 || bit >= 64 || count <= 0 || count >= 64) return Value::make_int(0);
        return Value::make_int((val >> bit) & ((1LL << count) - 1));
    };

    builtins_["ToUpper"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::toupper((int)args[0].as_int()));
    };
    builtins_["ToLower"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::tolower((int)args[0].as_int()));
    };
    builtins_["IsAlpha"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isalpha((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsDigit"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isdigit((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsAlphaNum"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isalnum((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsUpper"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isupper((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsLower"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::islower((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsSpace"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isspace((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsPunct"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::ispunct((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsCtrl"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::iscntrl((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsXDigit"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isxdigit((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsGraph"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isgraph((int)args[0].as_int()) ? 1 : 0);
    };
    builtins_["IsPrint"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        return Value::make_int(std::isprint((int)args[0].as_int()) ? 1 : 0);
    };

    builtins_["MemSet"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_ptr(nullptr);
        int64_t n = args[2].as_int();
        if (n <= 0) return args[0];
        if (n > 500000000) return Value::make_ptr(nullptr);
        if (args[0].kind == Value::Ptr && args[0].ptr)
            std::memset(args[0].ptr, (int)args[1].as_int(), (size_t)n);
        return args[0];
    };
    builtins_["MemCpy"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_ptr(nullptr);
        int64_t n = args[2].as_int();
        if (n <= 0) return args[0];
        if (n > 500000000) return Value::make_ptr(nullptr);
        if (args[0].kind == Value::Ptr && args[0].ptr && args[1].kind == Value::Ptr && args[1].ptr)
            std::memcpy(args[0].ptr, args[1].ptr, (size_t)n);
        return args[0];
    };
    builtins_["MemCmp"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_int(0);
        int64_t n = args[2].as_int();
        if (n <= 0) return Value::make_int(0);
        if (args[0].kind == Value::Ptr && args[0].ptr && args[1].kind == Value::Ptr && args[1].ptr)
            return Value::make_int(std::memcmp(args[0].ptr, args[1].ptr, (size_t)n));
        return Value::make_int(0);
    };
    builtins_["ReAlloc"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_ptr(nullptr);
        int64_t new_sz_i = args[1].as_int();
        if (new_sz_i <= 0 || new_sz_i > (1LL << 30)) return Value::make_ptr(nullptr);
        void* old_ptr = (args[0].kind == Value::Ptr) ? args[0].ptr : nullptr;
        return Value::make_ptr(heap_.realloc(old_ptr, (size_t)new_sz_i));
    };
    builtins_["MSize"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty() || args[0].kind != Value::Ptr) return Value::make_int(0);
        return Value::make_int(static_cast<int64_t>(heap_.size(args[0].ptr)));
    };
    builtins_["MAllocIdent"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty() || args[0].kind != Value::Ptr || !args[0].ptr)
            return Value::make_ptr(nullptr);
        size_t sz = heap_.size(args[0].ptr);
        uint8_t* dst = heap_.alloc(sz);
        if (dst) std::memcpy(dst, args[0].ptr, sz);
        return Value::make_ptr(dst);
    };
    builtins_["StrFind"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_ptr(nullptr);
        auto getStr = [](const Value& v) -> const char* {
            if (v.kind == Value::Str) return v.str_val.c_str();
            if (v.kind == Value::Ptr && v.ptr) return reinterpret_cast<const char*>(v.ptr);
            return "";
        };
        const char* result = strstr(getStr(args[1]), getStr(args[0]));
        return result ? Value::make_ptr(reinterpret_cast<uint8_t*>(const_cast<char*>(result))) : Value::make_ptr(nullptr);
    };

    builtins_["PutChars"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_void();
        int64_t ch = args[0].as_int();
        for (int i = 0; i < 8; i++) {
            char c = (char)((ch >> (i * 8)) & 0xFF);
            if (c == 0) break;
            std::fputc(c, stdout);
        }
        std::fflush(stdout);
        return Value::make_void();
    };
    builtins_["GetChar"] = [](const std::vector<Value>&) -> Value {
        int c = std::fgetc(stdin);
        return Value::make_int(c == EOF ? -1 : c);
    };
    builtins_["GetStr"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        char* buf = (args[0].kind == Value::Ptr) ? reinterpret_cast<char*>(args[0].ptr) : nullptr;
        int64_t max = args[1].as_int();
        if (!buf || max <= 0) return Value::make_int(0);
        if (max > 0x40000000LL) max = 0x40000000LL;
        if (!std::fgets(buf, (int)max, stdin)) { buf[0] = '\0'; return Value::make_int(0); }
        int64_t len = (int64_t)std::strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
        return Value::make_int(len);
    };

    builtins_["Sleep"] = [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            struct timespec ts;
            int64_t ms = args[0].as_int();
            ts.tv_sec = ms / 1000;
            ts.tv_nsec = (ms % 1000) * 1000000L;
            nanosleep(&ts, nullptr);
        }
        return Value::make_void();
    };
    builtins_["GetTicks"] = [](const std::vector<Value>&) -> Value {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return Value::make_int((int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL);
    };
    builtins_["Now"] = [](const std::vector<Value>&) -> Value {
        time_t t = time(nullptr);
        struct tm tm_val;
        localtime_r(&t, &tm_val);
        uint64_t v = ((uint64_t)tm_val.tm_sec)
                   | ((uint64_t)tm_val.tm_min  <<  6)
                   | ((uint64_t)tm_val.tm_hour << 12)
                   | ((uint64_t)tm_val.tm_mday << 17)
                   | ((uint64_t)(tm_val.tm_mon+1) << 22)
                   | ((uint64_t)(tm_val.tm_year+1900) << 26);
        return Value::make_int((int64_t)v);
    };
    builtins_["GetTickCount"] = builtins_["GetTicks"];
    builtins_["__vararg_count"] = [this](const std::vector<Value>&) -> Value {
        if (call_stack_.empty()) return Value::make_int(0);
        return Value::make_int((int64_t)call_stack_.back().varargs.size());
    };
    builtins_["__vararg_get"] = [this](const std::vector<Value>& a) -> Value {
        if (call_stack_.empty() || a.empty()) return Value::make_int(0);
        int64_t idx = a[0].as_int();
        if (idx < 0 || idx >= static_cast<int64_t>(call_stack_.back().varargs.size()))
            return Value::make_int(0);
        return call_stack_.back().varargs[static_cast<size_t>(idx)];
    };
    builtins_["SysDbg"] = [](const std::vector<Value>&) -> Value {
        return Value::make_void();
    };
    builtins_["StrNCmp"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_int(0);
        auto getStr = [](const Value& v) -> const char* {
            if (v.kind == Value::Str) return v.str_val.c_str();
            if (v.kind == Value::Ptr && v.ptr) return reinterpret_cast<const char*>(v.ptr);
            return "";
        };
        return Value::make_int(std::strncmp(getStr(args[0]), getStr(args[1]), (size_t)args[2].as_int()));
    };

    builtins_["FileOpen"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(-1);
        std::string path = valueToStdStr(args[0]);
        int64_t mode = args[1].as_int();
        char flags[8];
        if (mode & 4) snprintf(flags, sizeof(flags), "a%s", (mode & 1) ? "+" : "");
        else if ((mode & 2) || (mode & 8)) snprintf(flags, sizeof(flags), "w%s", (mode & 1) ? "+" : "");
        else snprintf(flags, sizeof(flags), "r");
        FILE* f = std::fopen(path.c_str(), flags);
        return Value::make_int(f ? (int64_t)(intptr_t)f : -1);
    };
    builtins_["FileClose"] = [](const std::vector<Value>& args) -> Value {
        if (!args.empty() && args[0].as_int() > 0)
            std::fclose(reinterpret_cast<FILE*>((intptr_t)args[0].as_int()));
        return Value::make_void();
    };
    builtins_["FileRead"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_int(0);
        FILE* f = reinterpret_cast<FILE*>((intptr_t)args[0].as_int());
        void* buf = (args[1].kind == Value::Ptr) ? args[1].ptr : nullptr;
        if (!f || !buf) return Value::make_int(0);
        return Value::make_int((int64_t)std::fread(buf, 1, (size_t)args[2].as_int(), f));
    };
    builtins_["FileWrite"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_int(0);
        FILE* f = reinterpret_cast<FILE*>((intptr_t)args[0].as_int());
        const void* buf = nullptr;
        if (args[1].kind == Value::Ptr) buf = args[1].ptr;
        else if (args[1].kind == Value::Str) buf = args[1].str_val.c_str();
        if (!f || !buf) return Value::make_int(0);
        return Value::make_int((int64_t)std::fwrite(buf, 1, (size_t)args[2].as_int(), f));
    };
    builtins_["FileSize"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(-1);
        FILE* f = reinterpret_cast<FILE*>((intptr_t)args[0].as_int());
        if (!f) return Value::make_int(-1);
        long pos = std::ftell(f);
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, pos, SEEK_SET);
        return Value::make_int((int64_t)sz);
    };
    builtins_["FileSeek"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(-1);
        FILE* f = reinterpret_cast<FILE*>((intptr_t)args[0].as_int());
        if (!f) return Value::make_int(-1);
        return Value::make_int((int64_t)std::fseek(f, (long)args[1].as_int(), SEEK_SET));
    };
    builtins_["FileExists"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        std::string path = valueToStdStr(args[0]);
        FILE* f = std::fopen(path.c_str(), "r");
        if (f) { std::fclose(f); return Value::make_int(1); }
        return Value::make_int(0);
    };
    builtins_["FileReadAll"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_ptr(nullptr);
        std::string path = valueToStdStr(args[0]);
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return Value::make_ptr(nullptr);
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > 256L * 1024 * 1024) {
            std::fclose(f);
            return Value::make_ptr(nullptr);
        }
        uint8_t* buf = heap_.alloc((size_t)sz + 1);
        size_t rd = std::fread(buf, 1, (size_t)sz, f);
        std::fclose(f);
        buf[rd] = '\0';
        if (args.size() >= 2 && args[1].kind == Value::Ptr && args[1].ptr) {
            *reinterpret_cast<int64_t*>(args[1].ptr) = (int64_t)rd;
        }
        return Value::make_ptr(buf);
    };
    builtins_["FileWriteAll"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_void();
        std::string path = valueToStdStr(args[0]);
        const void* data = nullptr;
        if (args[1].kind == Value::Ptr) data = args[1].ptr;
        else if (args[1].kind == Value::Str) data = args[1].str_val.c_str();
        if (!data) return Value::make_void();
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return Value::make_void();
        int64_t wsz = args[2].as_int();
        if (wsz > 0 && wsz <= 256LL * 1024 * 1024)
            std::fwrite(data, 1, (size_t)wsz, f);
        std::fclose(f);
        return Value::make_void();
    };
    builtins_["FileDel"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(-1);
        return Value::make_int((int64_t)std::remove(valueToStdStr(args[0]).c_str()));
    };

    builtins_["StrNew"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_ptr(nullptr);
        std::string s;
        if (args[0].kind == Value::Str) s = args[0].str_val;
        else if (args[0].kind == Value::Ptr && args[0].ptr)
            s = reinterpret_cast<const char*>(args[0].ptr);
        uint8_t* p = heap_.alloc(s.size() + 1);
        std::memcpy(p, s.c_str(), s.size() + 1);
        return Value::make_ptr(p);
    };
    builtins_["StrDup"] = builtins_["StrNew"];
    builtins_["StrUpr"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_ptr(nullptr);
        if (args[0].kind == Value::Ptr && args[0].ptr) {
            char* p = reinterpret_cast<char*>(args[0].ptr);
            for (char* q = p; *q; ++q) *q = (char)std::toupper((unsigned char)*q);
            return args[0];
        }
        return Value::make_ptr(nullptr);
    };
    builtins_["StrLwr"] = [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_ptr(nullptr);
        if (args[0].kind == Value::Ptr && args[0].ptr) {
            char* p = reinterpret_cast<char*>(args[0].ptr);
            for (char* q = p; *q; ++q) *q = (char)std::tolower((unsigned char)*q);
            return args[0];
        }
        return Value::make_ptr(nullptr);
    };
    builtins_["SPrint"] = [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        char* dst = nullptr;
        if (args[0].kind == Value::Ptr && args[0].ptr)
            dst = reinterpret_cast<char*>(args[0].ptr);
        std::string fmt = valueToStdStr(args[1]);
        std::string out = formatPrint(fmt, args, 2);
        if (dst) {
            size_t cap = heap_.size(dst);
            size_t need = out.size() + 1;
            if (cap > 0 && need > cap) {
                std::memcpy(dst, out.c_str(), cap);
                dst[cap - 1] = '\0';
            } else {
                std::memcpy(dst, out.c_str(), need);
            }
        }
        return Value::make_int((int64_t)out.size());
    };
    builtins_["StrNLen"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        const char* s = valueToStr(args[0]);
        if (!s) return Value::make_int(0);
        int64_t maxlen = args[1].as_int();
        return Value::make_int((int64_t)strnlen(s, (size_t)maxlen));
    };
    builtins_["RandI64"] = [](const std::vector<Value>&) -> Value {
        uint64_t r = ((uint64_t)(unsigned)std::rand() << 32) | (uint64_t)(unsigned)std::rand();
        return Value::make_int((int64_t)r);
    };
    builtins_["DirExists"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        std::string path = valueToStdStr(args[0]);
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return Value::make_int(0);
        return Value::make_int(S_ISDIR(st.st_mode) ? 1 : 0);
    };
    builtins_["DirMk"] = [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(-1);
        std::string path = valueToStdStr(args[0]);
        return Value::make_int((int64_t)mkdir(path.c_str(), 0755));
    };
    builtins_["FileRename"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(-1);
        std::string src = valueToStdStr(args[0]);
        std::string dst = valueToStdStr(args[1]);
        return Value::make_int((int64_t)std::rename(src.c_str(), dst.c_str()));
    };
    builtins_["MemMove"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 3) return Value::make_ptr(nullptr);
        int64_t n = args[2].as_int();
        if (n <= 0 || n > 500000000) return args[0];
        if (args[0].kind == Value::Ptr && args[0].ptr && args[1].kind == Value::Ptr && args[1].ptr)
            std::memmove(args[0].ptr, args[1].ptr, (size_t)n);
        return args[0];
    };
    builtins_["ArgC"] = [](const std::vector<Value>&) -> Value {
        return Value::make_int((int64_t)::g_argc);
    };
    builtins_["ArgV"] = [](const std::vector<Value>&) -> Value {
        return Value::make_ptr(reinterpret_cast<uint8_t*>(::g_argv));
    };

    builtins_["StrOcc"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_int(0);
        const char* s = nullptr;
        if (args[0].kind == Value::Str) s = args[0].str_val.c_str();
        else if (args[0].kind == Value::Ptr && args[0].ptr)
            s = reinterpret_cast<const char*>(args[0].ptr);
        if (!s) return Value::make_int(0);
        char ch = (char)(args[1].as_int() & 0xFF);
        int64_t count = 0;
        while (*s) { if (*s++ == ch) ++count; }
        return Value::make_int(count);
    };
    builtins_["StrFirst"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_ptr(nullptr);
        const char* s = nullptr;
        if (args[0].kind == Value::Str) s = args[0].str_val.c_str();
        else if (args[0].kind == Value::Ptr && args[0].ptr)
            s = reinterpret_cast<const char*>(args[0].ptr);
        if (!s) return Value::make_ptr(nullptr);
        int ch = (int)(args[1].as_int() & 0xFF);
        const char* p = std::strchr(s, ch);
        if (!p) return Value::make_ptr(nullptr);
        return Value::make_str(p);
    };
    builtins_["StrLast"] = [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) return Value::make_ptr(nullptr);
        const char* s = nullptr;
        if (args[0].kind == Value::Str) s = args[0].str_val.c_str();
        else if (args[0].kind == Value::Ptr && args[0].ptr)
            s = reinterpret_cast<const char*>(args[0].ptr);
        if (!s) return Value::make_ptr(nullptr);
        int ch = (int)(args[1].as_int() & 0xFF);
        const char* p = std::strrchr(s, ch);
        if (!p) return Value::make_ptr(nullptr);
        return Value::make_str(p);
    };
#undef MATH1
#undef MATH2
}

/**
 * @brief True लौटाता है अगर name एक built-in function के रूप में registered है।
 *
 * @param name Lookup करने वाला function name।
 * @return True अगर name builtins_ में मिला, नहीं तो false।
 */
bool Interpreter::isBuiltin(const std::string& name) const {
    return builtins_.find(name) != builtins_.end();
}

/**
 * @brief सारे user-defined functions, classes, और globals clear करता है; builtins preserve रहते हैं।
 */
void Interpreter::reset() {
    functions_.clear();
    class_decls_.clear();
    struct_layouts_.clear();
    method_cache_.clear();
    globals_.clear();
    static_vars_.clear();
    static_initialized_.clear();
    last_class_decl_ = nullptr;
    call_stack_.clear();
    signal_ = Signal::None;
}

/**
 * @brief Managed interpreter heap की statistics लौटाता है।
 *
 * @return {total_bytes_allocated, number_of_live_blocks} का pair।
 */
std::pair<size_t, size_t> Interpreter::heapStats() const {
    return {heap_.totalBytes(), heap_.numBlocks()};
}


/**
 * @brief Translation unit execute करता है; अगर Main() defined हो तो उसे call करता है।
 *
 * पहले सभी top-level declarations register करता है, फिर top-level statements execute करता है,
 * और अंत में Main() invoke करता है अगर वो present हो।
 *
 * @param tu Execute करने वाला translation unit।
 * @return Main() का exit code, या 0 अगर Main() absent है।
 */
int Interpreter::run(TranslationUnit* tu) {
    for (auto* node : tu->decls) {
        if (auto* fd = dynamic_cast<FuncDecl*>(node)) {
            functions_[fd->name] = fd;
        } else if (auto* cd = dynamic_cast<ClassDecl*>(node)) {
            class_decls_[cd->name] = cd;
            last_class_decl_ = cd;
            method_cache_.clear();
            computeStructLayout(cd);
            for (auto* m : cd->members) {
                if (m->nk == NodeKind::FuncDecl) {
                    auto* fd = static_cast<FuncDecl*>(m);
                    functions_[cd->name + "$" + fd->name] = fd;
                }
            }
        } else if (auto* ud = dynamic_cast<UnionDecl*>(node)) {
            StructLayout layout;
            size_t max_size = 0;
            for (auto* f : ud->members) {
                auto* fld = dynamic_cast<FieldDecl*>(f);
                if (!fld) continue;
                layout.field_offsets.push_back({fld->name, 0, 0, -1});
                size_t sz = fld->type ? static_cast<size_t>(fld->type->sizeInBytes()) : 8;
                if (sz > max_size) max_size = sz;
            }
            layout.total_size = max_size;
            layout.field_index.reserve(layout.field_offsets.size());
            for (size_t fi = 0; fi < layout.field_offsets.size(); ++fi) {
                layout.field_index[layout.field_offsets[fi].name] = fi;
            }
            struct_layouts_[ud->name] = layout;
        }
    }

    for (auto* node : tu->decls) {
        if (dynamic_cast<FuncDecl*>(node)) continue;
        if (dynamic_cast<ClassDecl*>(node)) continue;
        if (dynamic_cast<UnionDecl*>(node)) continue;

        try {
            if (auto* stmt = dynamic_cast<Stmt*>(node)) {
                execStmt(stmt);
                if (signal_ == Signal::Return) {
                    signal_ = Signal::None;
                    return static_cast<int>(call_stack_.empty() ? 0 : call_stack_.back().return_val.as_int());
                }
            } else if (auto* decl = dynamic_cast<Decl*>(node)) {
                execDecl(decl);
            }
        } catch (HolyCException& e) {
            std::fprintf(stderr, "Unhandled HolyC exception: %ld\n", (long)e.code);
            return 1;
        }
    }

    auto it = functions_.find("Main");
    if (it != functions_.end()) {
        FuncDecl* main_fn = it->second;
        Frame frame;
        call_stack_.push_back(frame);

        labels_.clear();
        if (main_fn->body) scanLabels(main_fn->body);

        try {
            if (main_fn->body) execCompound(main_fn->body);
        } catch (HolyCException& e) {
            std::fprintf(stderr, "Unhandled HolyC exception: %ld\n", (long)e.code);
            call_stack_.pop_back();
            return 1;
        }

        Value ret = call_stack_.back().return_val;
        call_stack_.pop_back();
        signal_ = Signal::None;
        return static_cast<int>(ret.as_int());
    }

    return 0;
}

/**
 * @brief एक single expression evaluate करता है (public REPL entry point)।
 *
 * @param expr Evaluate करने वाला expression।
 * @return Result value।
 */
Value Interpreter::eval(Expr* expr) {
    return evalExpr(expr);
}

/**
 * @brief NodeKind पर dispatch करके कोई भी expression node evaluate करता है।
 *
 * @param e Evaluate करने वाला expression node।
 * @return Expression का produced value, या unhandled node kinds के लिए void।
 */
Value Interpreter::evalExpr(Expr* e) {
    if (!e) return Value::make_void();

    switch (e->nk) {
    case NodeKind::IntLiteralExpr: {
        auto* lit = static_cast<IntLiteralExpr*>(e);
        switch (lit->type_hint) {
        case PrimKind::U8: case PrimKind::U16: case PrimKind::U32: case PrimKind::U64:
            return Value::make_uint(lit->value);
        default:
            return Value::make_int(static_cast<int64_t>(lit->value));
        }
    }
    case NodeKind::FloatLiteralExpr:
        return Value::make_float(static_cast<FloatLiteralExpr*>(e)->value);
    case NodeKind::StringLiteralExpr:
        return Value::make_str(static_cast<StringLiteralExpr*>(e)->value);
    case NodeKind::CharLiteralExpr:
        return Value::make_int(static_cast<int64_t>(static_cast<CharLiteralExpr*>(e)->value));
    case NodeKind::BoolLiteralExpr:
        return Value::make_bool(static_cast<BoolLiteralExpr*>(e)->value);
    case NodeKind::IdentifierExpr: {
        auto* id = static_cast<IdentifierExpr*>(e);
        Value* vp = getVarPtr(id->name);
        if (vp) return *vp;
        if (functions_.find(id->name) != functions_.end() ||
            builtins_.find(id->name) != builtins_.end())
            return Value::make_funcptr(id->name);
        if (!current_method_class_.empty()) {
            Value thisVal = getVar("this");
            uint8_t* rawPtr = nullptr;
            if (thisVal.kind == Value::Ptr)
                rawPtr = thisVal.ptr;
            else if (thisVal.kind == Value::Int || thisVal.kind == Value::UInt)
                rawPtr = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(thisVal.as_uint()));
            if (rawPtr) {
                auto lit = struct_layouts_.find(current_method_class_);
                if (lit != struct_layouts_.end()) {
                    const auto& layout = lit->second;
                    auto fit = layout.field_index.find(id->name);
                    if (fit != layout.field_index.end() &&
                        fit->second < layout.field_offsets.size()) {
                        const auto& fl = layout.field_offsets[fit->second];
                        uint64_t raw = 0;
                        std::memcpy(&raw, rawPtr + fl.offset, sizeof(uint64_t));
                        if (fl.bit_width > 0 && fl.bit_width < 64 && fl.bit_start >= 0 && fl.bit_start < 64) {
                            uint64_t mask = (1ULL << fl.bit_width) - 1;
                            return Value::make_int(static_cast<int64_t>((raw >> fl.bit_start) & mask));
                        }
                        return Value::make_int(static_cast<int64_t>(raw));
                    }
                }
            }
        }
        return getVar(id->name);
    }
    case NodeKind::BinaryExpr:
        return evalBinaryOp(static_cast<BinaryExpr*>(e));
    case NodeKind::UnaryExpr:
        return evalUnaryOp(static_cast<UnaryExpr*>(e));
    case NodeKind::TernaryExpr:
        return evalTernary(static_cast<TernaryExpr*>(e));
    case NodeKind::ChainedCmpExpr:
        return evalChainedCmp(static_cast<ChainedCmpExpr*>(e));
    case NodeKind::PowerExpr:
        return evalPower(static_cast<PowerExpr*>(e));
    case NodeKind::CallExpr:
        return evalCall(static_cast<CallExpr*>(e));
    case NodeKind::PostfixCastExpr:
        return evalPostfixCast(static_cast<PostfixCastExpr*>(e));
    case NodeKind::SizeofExpr:
        return evalSizeof(static_cast<SizeofExpr*>(e));
    case NodeKind::OffsetExpr:
        return evalOffset(static_cast<OffsetExpr*>(e));
    case NodeKind::ArrayIndexExpr:
        return evalArrayIndex(static_cast<ArrayIndexExpr*>(e));
    case NodeKind::FieldAccessExpr:
        return evalFieldAccess(static_cast<FieldAccessExpr*>(e));
    case NodeKind::AddrOfExpr: {
        auto* ao = static_cast<AddrOfExpr*>(e);
        if (auto* id = dynamic_cast<IdentifierExpr*>(ao->operand)) {
            if (functions_.find(id->name) != functions_.end() ||
                builtins_.find(id->name) != builtins_.end())
                return Value::make_funcptr(id->name);
            Value* vp = getVarPtr(id->name);
            if (vp) {
                // Arrays decay होते हैं; class vars अपने calloc'd struct का Ptr hold करते हैं
                bool is_array = ao->operand->resolved_type &&
                                ao->operand->resolved_type->kind == Type::Array;
                if (is_array)
                    return *vp;
                bool is_class = ao->operand->resolved_type &&
                                ao->operand->resolved_type->kind == Type::Class;
                if (is_class && vp->kind == Value::Ptr)
                    return *vp;
                return Value::make_ptr(reinterpret_cast<uint8_t*>(vp));
            }
        } else if (auto* ai = dynamic_cast<ArrayIndexExpr*>(ao->operand)) {
            Value base = evalExpr(ai->base);
            Value index = evalExpr(ai->index);
            if (base.kind == Value::Ptr && base.ptr) {
                size_t elemSz = 8;
                if (ai->resolved_type) {
                    size_t s = static_cast<size_t>(ai->resolved_type->sizeInBytes());
                    if (s > 0) elemSz = s;
                }
                int64_t idx = index.as_int();
                if (idx < 0 || (uint64_t)idx > 0x10000000ULL / elemSz) return Value::make_ptr(nullptr);
                return Value::make_ptr(base.ptr + (uint64_t)idx * elemSz);
            }
        }
        return Value::make_ptr(nullptr);
    }
    case NodeKind::DerefExpr: {
        auto* de = static_cast<DerefExpr*>(e);
        Value ptr_val = evalExpr(de->operand);
        if (ptr_val.kind == Value::Str) {
            // str_val.i pointer offset hold करता है (pointer arithmetic से advance हुआ)
            size_t offset = static_cast<size_t>(ptr_val.i >= 0 ? ptr_val.i : 0);
            if (offset < ptr_val.str_val.size()) {
                return Value::make_int(static_cast<int64_t>(static_cast<uint8_t>(ptr_val.str_val[offset])));
            }
            return Value::make_int(0);
        }
        if (ptr_val.kind == Value::Ptr && ptr_val.ptr) {
            Value* target = reinterpret_cast<Value*>(ptr_val.ptr);
            return *target;
        }
        diag_.warning(de->loc, "null pointer dereference");
        return Value::make_int(0);
    }
    case NodeKind::ThrowExpr: {
        auto* te = static_cast<ThrowExpr*>(e);
        Value code = te->code ? evalExpr(te->code) : Value::make_int(1);
        throw HolyCException{code.as_int()};
        return Value::make_void();
    }
    case NodeKind::InitListExpr:
        return Value::make_int(0);
    default:
        return Value::make_void();
    }
}

/**
 * @brief Operator kind पर dispatch करके binary expression evaluate करता है।
 *
 * Assignment operators assignToExpr के through LHS को in-place mutate करते हैं। Logical AND/OR short-circuit करते हैं।
 * Integer, unsigned, और float operand types अलग-अलग dispatch paths follow करते हैं।
 *
 * @param e Binary expression node।
 * @return Result value; operand types के हिसाब से integer, unsigned, या float।
 */
Value Interpreter::evalBinaryOp(BinaryExpr* e) {
    BinOpKind op = e->op;

    bool is_assign = (op == BinOpKind::Assign || op == BinOpKind::AddAssign ||
                      op == BinOpKind::SubAssign || op == BinOpKind::MulAssign ||
                      op == BinOpKind::DivAssign || op == BinOpKind::ModAssign ||
                      op == BinOpKind::BitAndAssign || op == BinOpKind::BitOrAssign ||
                      op == BinOpKind::BitXorAssign || op == BinOpKind::ShlAssign ||
                      op == BinOpKind::ShrAssign || op == BinOpKind::PPAssign ||
                      op == BinOpKind::MMAssign);

    if (is_assign) {
        Value rhs = evalExpr(e->rhs);
        if (op == BinOpKind::Assign) {
            assignToExpr(e->lhs, rhs);
            return rhs;
        }
        Value lhs = evalExpr(e->lhs);
        Value result;
        if (lhs.kind == Value::Float || rhs.kind == Value::Float) {
            double l = lhs.as_float(), r = rhs.as_float();
            switch (op) {
            case BinOpKind::AddAssign: result = Value::make_float(l + r); break;
            case BinOpKind::SubAssign: result = Value::make_float(l - r); break;
            case BinOpKind::MulAssign: result = Value::make_float(l * r); break;
            case BinOpKind::DivAssign: result = Value::make_float(r != 0.0 ? l / r : 0.0); break;
            default: result = lhs; break;
            }
        } else {
            switch (op) {
            case BinOpKind::AddAssign: result = Value::make_int(lhs.as_int() + rhs.as_int()); break;
            case BinOpKind::SubAssign: result = Value::make_int(lhs.as_int() - rhs.as_int()); break;
            case BinOpKind::MulAssign: result = Value::make_int(lhs.as_int() * rhs.as_int()); break;
            case BinOpKind::DivAssign: result = Value::make_int(rhs.as_int() != 0 ? lhs.as_int() / rhs.as_int() : 0); break;
            case BinOpKind::ModAssign: result = Value::make_int(rhs.as_int() != 0 ? lhs.as_int() % rhs.as_int() : 0); break;
            case BinOpKind::BitAndAssign: result = Value::make_int(lhs.as_int() & rhs.as_int()); break;
            case BinOpKind::BitOrAssign:  result = Value::make_int(lhs.as_int() | rhs.as_int()); break;
            case BinOpKind::BitXorAssign: result = Value::make_int(lhs.as_int() ^ rhs.as_int()); break;
            case BinOpKind::ShlAssign: { int64_t s = rhs.as_int(); result = Value::make_int(s >= 0 && s < 64 ? lhs.as_int() << s : 0); break; }
            case BinOpKind::ShrAssign: { uint64_t s = rhs.as_uint(); result = Value::make_uint(s < 64 ? lhs.as_uint() >> s : 0); break; }
            case BinOpKind::PPAssign: result = Value::make_int(lhs.as_int() + 1); break;
            case BinOpKind::MMAssign: result = Value::make_int(lhs.as_int() - 1); break;
            default: result = lhs; break;
            }
        }
        assignToExpr(e->lhs, result);
        return result;
    }

    if (op == BinOpKind::LogAnd) {
        Value lhs = evalExpr(e->lhs);
        if (!lhs.as_bool()) return Value::make_bool(false);
        Value rhs = evalExpr(e->rhs);
        return Value::make_bool(rhs.as_bool());
    }
    if (op == BinOpKind::LogOr) {
        Value lhs = evalExpr(e->lhs);
        if (lhs.as_bool()) return Value::make_bool(true);
        Value rhs = evalExpr(e->rhs);
        return Value::make_bool(rhs.as_bool());
    }

    Value lhs = evalExpr(e->lhs);
    Value rhs = evalExpr(e->rhs);

    if (lhs.kind == Value::Int && rhs.kind == Value::Int) [[likely]] {
        int64_t l = lhs.i, r = rhs.i;
        switch (op) {
        case BinOpKind::Add:    return Value::make_int(l + r);
        case BinOpKind::Sub:    return Value::make_int(l - r);
        case BinOpKind::Mul:    return Value::make_int(l * r);
        case BinOpKind::Div:
            if (r == 0) { diag_.warning(e->loc, "integer division by zero"); return Value::make_int(0); }
            if (l == INT64_MIN && r == -1) return Value::make_int(INT64_MIN); // overflow पर wrap
            return Value::make_int(l / r);
        case BinOpKind::Mod:
            if (r == 0) { diag_.warning(e->loc, "integer modulo by zero"); return Value::make_int(0); }
            if (l == INT64_MIN && r == -1) return Value::make_int(0);
            return Value::make_int(l % r);
        case BinOpKind::Eq:     return Value::make_bool(l == r);
        case BinOpKind::Ne:     return Value::make_bool(l != r);
        case BinOpKind::Lt:     return Value::make_bool(l < r);
        case BinOpKind::Le:     return Value::make_bool(l <= r);
        case BinOpKind::Gt:     return Value::make_bool(l > r);
        case BinOpKind::Ge:     return Value::make_bool(l >= r);
        case BinOpKind::BitAnd: return Value::make_int(l & r);
        case BinOpKind::BitOr:  return Value::make_int(l | r);
        case BinOpKind::BitXor: return Value::make_int(l ^ r);
        case BinOpKind::Shl:    return Value::make_int(r >= 0 && r < 64 ? l << r : 0);
        case BinOpKind::Shr:    return Value::make_int(r >= 0 && r < 64 ? l >> r : 0);
        case BinOpKind::LogXor: return Value::make_bool((l != 0) != (r != 0));
        default: break;
        }
    }

    if (op == BinOpKind::LogXor) {
        return Value::make_bool(lhs.as_bool() != rhs.as_bool());
    }

    switch (op) {
    case BinOpKind::Eq: case BinOpKind::Ne:
    case BinOpKind::Lt: case BinOpKind::Le:
    case BinOpKind::Gt: case BinOpKind::Ge:
        return evalCmpOp(op, lhs, rhs);
    default: break;
    }

    // String pointer arithmetic: Value::i में stored offset को advance करो
    if (lhs.kind == Value::Str && (op == BinOpKind::Add || op == BinOpKind::Sub)) {
        int64_t delta = (op == BinOpKind::Add) ? rhs.as_int() : -rhs.as_int();
        Value result = lhs;
        result.i = lhs.i + delta;
        return result;
    }

    if (lhs.kind == Value::Float || rhs.kind == Value::Float) {
        double l = lhs.as_float(), r = rhs.as_float();
        switch (op) {
        case BinOpKind::Add: return Value::make_float(l + r);
        case BinOpKind::Sub: return Value::make_float(l - r);
        case BinOpKind::Mul: return Value::make_float(l * r);
        case BinOpKind::Div: return Value::make_float(r != 0.0 ? l / r : 0.0);
        case BinOpKind::Mod: return Value::make_float(r != 0.0 ? std::fmod(l, r) : 0.0);
        default: break;
        }
    }

    if (lhs.kind == Value::UInt || rhs.kind == Value::UInt) {
        uint64_t l = lhs.as_uint(), r = rhs.as_uint();
        switch (op) {
        case BinOpKind::Add: return Value::make_uint(l + r);
        case BinOpKind::Sub: return Value::make_uint(l - r);
        case BinOpKind::Mul: return Value::make_uint(l * r);
        case BinOpKind::Div: return Value::make_uint(r != 0 ? l / r : 0);
        case BinOpKind::Mod: return Value::make_uint(r != 0 ? l % r : 0);
        case BinOpKind::BitAnd: return Value::make_uint(l & r);
        case BinOpKind::BitOr:  return Value::make_uint(l | r);
        case BinOpKind::BitXor: return Value::make_uint(l ^ r);
        case BinOpKind::Shl: return Value::make_uint(r < 64 ? l << r : 0);
        case BinOpKind::Shr: return Value::make_uint(r < 64 ? l >> r : 0);
        default: break;
        }
    }

    int64_t l = lhs.as_int(), r = rhs.as_int();
    switch (op) {
    case BinOpKind::Add: return Value::make_int(l + r);
    case BinOpKind::Sub: return Value::make_int(l - r);
    case BinOpKind::Mul: return Value::make_int(l * r);
    case BinOpKind::Div:
        if (r == 0) { diag_.warning(e->loc, "integer division by zero"); return Value::make_int(0); }
        return Value::make_int(l / r);
    case BinOpKind::Mod:
        if (r == 0) { diag_.warning(e->loc, "integer modulo by zero"); return Value::make_int(0); }
        return Value::make_int(l % r);
    case BinOpKind::BitAnd: return Value::make_int(l & r);
    case BinOpKind::BitOr:  return Value::make_int(l | r);
    case BinOpKind::BitXor: return Value::make_int(l ^ r);
    case BinOpKind::Shl: return Value::make_int(r >= 0 && r < 64 ? l << r : 0);
    case BinOpKind::Shr: return Value::make_int(r >= 0 && r < 64 ? l >> r : 0);
    default: break;
    }

    return Value::make_void();
}

/**
 * @brief lhs और rhs को op से compare करता है, जरूरत पर float, string, या uint comparison में promote करता है।
 *
 * @param op Relational operator kind (Eq, Ne, Lt, Le, Gt, Ge)।
 * @param lhs Left-hand side value।
 * @param rhs Right-hand side value।
 * @return Comparison result represent करता boolean value।
 */
Value Interpreter::evalCmpOp(BinOpKind op, const Value& lhs, const Value& rhs) {
    if (lhs.kind == Value::Float || rhs.kind == Value::Float) {
        double l = lhs.as_float(), r = rhs.as_float();
        switch (op) {
        case BinOpKind::Eq: return Value::make_bool(l == r);
        case BinOpKind::Ne: return Value::make_bool(l != r);
        case BinOpKind::Lt: return Value::make_bool(l < r);
        case BinOpKind::Le: return Value::make_bool(l <= r);
        case BinOpKind::Gt: return Value::make_bool(l > r);
        case BinOpKind::Ge: return Value::make_bool(l >= r);
        default: break;
        }
    }
    if (lhs.kind == Value::Str && rhs.kind == Value::Str) {
        int cmp = lhs.str_val.compare(rhs.str_val);
        switch (op) {
        case BinOpKind::Eq: return Value::make_bool(cmp == 0);
        case BinOpKind::Ne: return Value::make_bool(cmp != 0);
        case BinOpKind::Lt: return Value::make_bool(cmp < 0);
        case BinOpKind::Le: return Value::make_bool(cmp <= 0);
        case BinOpKind::Gt: return Value::make_bool(cmp > 0);
        case BinOpKind::Ge: return Value::make_bool(cmp >= 0);
        default: break;
        }
    }
    if (lhs.kind == Value::UInt || rhs.kind == Value::UInt) {
        uint64_t l = lhs.as_uint(), r = rhs.as_uint();
        switch (op) {
        case BinOpKind::Eq: return Value::make_bool(l == r);
        case BinOpKind::Ne: return Value::make_bool(l != r);
        case BinOpKind::Lt: return Value::make_bool(l < r);
        case BinOpKind::Le: return Value::make_bool(l <= r);
        case BinOpKind::Gt: return Value::make_bool(l > r);
        case BinOpKind::Ge: return Value::make_bool(l >= r);
        default: break;
        }
    }
    int64_t l = lhs.as_int(), r = rhs.as_int();
    switch (op) {
    case BinOpKind::Eq: return Value::make_bool(l == r);
    case BinOpKind::Ne: return Value::make_bool(l != r);
    case BinOpKind::Lt: return Value::make_bool(l < r);
    case BinOpKind::Le: return Value::make_bool(l <= r);
    case BinOpKind::Gt: return Value::make_bool(l > r);
    case BinOpKind::Ge: return Value::make_bool(l >= r);
    default: break;
    }
    return Value::make_bool(false);
}

/**
 * @brief target द्वारा denote किए lvalue में val write करता है।
 *
 * Target एक identifier, dereference, array index, या field access हो सकता है।
 * Struct bit-fields और method bodies के अंदर implicit this-field assignment handle करता है।
 *
 * @param target Assignment receive करने वाला lvalue expression।
 * @param val Store करने वाला value।
 */
void Interpreter::assignToExpr(Expr* target, const Value& val) {
    if (!target) return;
    switch (target->nk) {
    case NodeKind::IdentifierExpr: {
        auto* id = static_cast<IdentifierExpr*>(target);
        if (!current_method_class_.empty() && !getVarPtr(id->name)) {
            Value thisVal = getVar("this");
            uint8_t* rawPtr = nullptr;
            if (thisVal.kind == Value::Ptr)
                rawPtr = thisVal.ptr;
            else if (thisVal.kind == Value::Int || thisVal.kind == Value::UInt)
                rawPtr = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(thisVal.as_uint()));
            if (rawPtr) {
                auto lit = struct_layouts_.find(current_method_class_);
                if (lit != struct_layouts_.end()) {
                    const auto& layout = lit->second;
                    auto fit = layout.field_index.find(id->name);
                    if (fit != layout.field_index.end() &&
                        fit->second < layout.field_offsets.size()) {
                        const auto& fl = layout.field_offsets[fit->second];
                        uint64_t v64 = val.as_uint();
                        if (fl.bit_width > 0 && fl.bit_width < 64 && fl.bit_start >= 0 && fl.bit_start < 64) {
                            uint64_t word = 0;
                            std::memcpy(&word, rawPtr + fl.offset, sizeof(uint64_t));
                            uint64_t mask = (1ULL << fl.bit_width) - 1;
                            word &= ~(mask << fl.bit_start);
                            word |= (v64 & mask) << fl.bit_start;
                            std::memcpy(rawPtr + fl.offset, &word, sizeof(uint64_t));
                        } else {
                            std::memcpy(rawPtr + fl.offset, &v64, sizeof(uint64_t));
                        }
                        return;
                    }
                }
            }
        }
        setVar(id->name, val);
        break;
    }
    case NodeKind::DerefExpr: {
        auto* de = static_cast<DerefExpr*>(target);
        Value ptr_val = evalExpr(de->operand);
        if (ptr_val.kind == Value::Ptr && ptr_val.ptr) {
            Value* slot = reinterpret_cast<Value*>(ptr_val.ptr);
            *slot = val;
        }
        break;
    }
    case NodeKind::ArrayIndexExpr: {
        auto* ai = static_cast<ArrayIndexExpr*>(target);
        Value base = evalExpr(ai->base);
        Value index = evalExpr(ai->index);
        uint8_t* rawPtr = nullptr;
        if (base.kind == Value::Ptr) rawPtr = base.ptr;
        else if (base.kind == Value::Int || base.kind == Value::UInt)
            rawPtr = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(base.as_uint()));
        if (rawPtr) {
            size_t elemSz = 8;
            if (ai->resolved_type) {
                size_t s = static_cast<size_t>(ai->resolved_type->sizeInBytes());
                if (s > 0) elemSz = s;
            }
            int64_t idx = index.as_int();
            if (idx < 0 || (uint64_t)idx > 0x10000000ULL / elemSz) break;
            uint8_t* elemPtr = rawPtr + (uint64_t)idx * elemSz;
            switch (elemSz) {
            case 1: { uint8_t  v = static_cast<uint8_t>(val.as_uint());  std::memcpy(elemPtr, &v, 1); break; }
            case 2: { uint16_t v = static_cast<uint16_t>(val.as_uint()); std::memcpy(elemPtr, &v, 2); break; }
            case 4: { uint32_t v = static_cast<uint32_t>(val.as_uint()); std::memcpy(elemPtr, &v, 4); break; }
            default: { uint64_t v = val.as_uint(); std::memcpy(elemPtr, &v, 8); break; }
            }
        }
        break;
    }
    case NodeKind::FieldAccessExpr: {
        auto* fa = static_cast<FieldAccessExpr*>(target);
        Value obj = evalExpr(fa->object);
        uint8_t* rawPtr = nullptr;
        if (obj.kind == Value::Ptr)
            rawPtr = obj.ptr;
        else if (obj.kind == Value::Int || obj.kind == Value::UInt)
            rawPtr = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(obj.as_uint()));

        if (rawPtr) {
            Type* objTy = fa->object->resolved_type;
            if (objTy && objTy->kind == Type::IntrinsicUnion) {
                const std::string& fn = fa->field;
                if (fn == "i64" || fn == "u64") {
                    uint64_t v = val.as_uint(); std::memcpy(rawPtr, &v, 8);
                } else if (fn == "f64") {
                    double d = val.as_float(); std::memcpy(rawPtr, &d, 8);
                }
                return;
            }
            std::string struct_name;
            if (fa->object->resolved_type) {
                Type* objTy2 = fa->object->resolved_type;
                if (objTy2->kind == Type::Pointer) {
                    auto& pt = std::get<PointerType>(objTy2->data);
                    if (pt.pointee && pt.pointee->kind == Type::Class) {
                        auto& ct = std::get<ClassType>(pt.pointee->data);
                        if (ct.decl) struct_name = ct.decl->name;
                    }
                } else if (objTy2->kind == Type::Class) {
                    auto& ct = std::get<ClassType>(objTy2->data);
                    if (ct.decl) struct_name = ct.decl->name;
                }
            }
            auto lit = struct_layouts_.find(struct_name);
            if (lit != struct_layouts_.end()) {
                const auto& layout = lit->second;
                auto fit = layout.field_index.find(fa->field);
                if (fit != layout.field_index.end() &&
                    fit->second < layout.field_offsets.size()) {
                    const auto& fl = layout.field_offsets[fit->second];
                    if (fl.bit_width > 0 && fl.bit_width < 64 && fl.bit_start >= 0 && fl.bit_start < 64) {
                        uint64_t word = 0;
                        std::memcpy(&word, rawPtr + fl.offset, sizeof(uint64_t));
                        uint64_t mask = (1ULL << fl.bit_width) - 1;
                        word &= ~(mask << fl.bit_start);
                        word |= (val.as_uint() & mask) << fl.bit_start;
                        std::memcpy(rawPtr + fl.offset, &word, sizeof(uint64_t));
                    } else {
                        std::memcpy(rawPtr + fl.offset, &val.u, sizeof(uint64_t));
                    }
                    return;
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief Unary expression evaluate करता है।
 *
 * Pre/post increment और decrement assignToExpr के through operand को mutate करते हैं।
 *
 * @param e Unary expression node।
 * @return Result value; post-increment/decrement mutation से पहले की value लौटाते हैं।
 */
Value Interpreter::evalUnaryOp(UnaryExpr* e) {
    switch (e->op) {
    case UnOpKind::Negate: {
        Value v = evalExpr(e->operand);
        if (v.kind == Value::Float) return Value::make_float(-v.f);
        return Value::make_int(-v.as_int());
    }
    case UnOpKind::LogNot: {
        Value v = evalExpr(e->operand);
        return Value::make_bool(!v.as_bool());
    }
    case UnOpKind::BitNot: {
        Value v = evalExpr(e->operand);
        if (v.kind == Value::UInt) return Value::make_uint(~v.u);
        return Value::make_int(~v.as_int());
    }
    case UnOpKind::PreInc: {
        Value v = evalExpr(e->operand);
        Value result;
        if (v.kind == Value::Float) result = Value::make_float(v.f + 1.0);
        else if (v.kind == Value::Str) { result = v; result.i++; }
        else result = Value::make_int(v.as_int() + 1);
        assignToExpr(e->operand, result);
        return result;
    }
    case UnOpKind::PreDec: {
        Value v = evalExpr(e->operand);
        Value result;
        if (v.kind == Value::Float) result = Value::make_float(v.f - 1.0);
        else if (v.kind == Value::Str) { result = v; result.i--; }
        else result = Value::make_int(v.as_int() - 1);
        assignToExpr(e->operand, result);
        return result;
    }
    case UnOpKind::PostInc: {
        Value v = evalExpr(e->operand);
        Value result;
        if (v.kind == Value::Float) result = Value::make_float(v.f + 1.0);
        else if (v.kind == Value::Str) { result = v; result.i++; }
        else result = Value::make_int(v.as_int() + 1);
        assignToExpr(e->operand, result);
        return v;
    }
    case UnOpKind::PostDec: {
        Value v = evalExpr(e->operand);
        Value result;
        if (v.kind == Value::Float) result = Value::make_float(v.f - 1.0);
        else if (v.kind == Value::Str) { result = v; result.i--; }
        else result = Value::make_int(v.as_int() - 1);
        assignToExpr(e->operand, result);
        return v;
    }
    case UnOpKind::AddrOf: {
        if (auto* id = dynamic_cast<IdentifierExpr*>(e->operand)) {
            Value* vp = getVarPtr(id->name);
            if (vp) return Value::make_ptr(reinterpret_cast<uint8_t*>(vp));
        }
        return Value::make_ptr(nullptr);
    }
    case UnOpKind::Deref: {
        Value ptr_val = evalExpr(e->operand);
        if (ptr_val.kind == Value::Str) {
            size_t offset = static_cast<size_t>(ptr_val.i >= 0 ? ptr_val.i : 0);
            if (offset < ptr_val.str_val.size())
                return Value::make_int(static_cast<int64_t>(static_cast<uint8_t>(ptr_val.str_val[offset])));
            return Value::make_int(0);
        }
        if (ptr_val.kind == Value::Ptr && ptr_val.ptr) {
            Value* target = reinterpret_cast<Value*>(ptr_val.ptr);
            return *target;
        }
        diag_.warning(e->loc, "null pointer dereference");
        return Value::make_int(0);
    }
    }
    return Value::make_void();
}

/**
 * @brief Ternary conditional expression evaluate करता है, unchosen branch को short-circuit करता है।
 *
 * @param e Ternary expression node।
 * @return ली गई branch की value।
 */
Value Interpreter::evalTernary(TernaryExpr* e) {
    Value cond = evalExpr(e->cond);
    return cond.as_bool() ? evalExpr(e->then_expr) : evalExpr(e->else_expr);
}

/**
 * @brief Chained comparison (a < b < c) को short-circuit के साथ sequential pairwise tests के रूप में evaluate करता है।
 *
 * हर pair left-to-right evaluate होती है; जैसे ही कोई pair fail होती है result false हो जाता है।
 *
 * @param e Chained comparison expression node।
 * @return Boolean true अगर सभी pairwise comparisons hold करें, नहीं तो false।
 */
Value Interpreter::evalChainedCmp(ChainedCmpExpr* e) {
    if (e->operands.size() < 2) return Value::make_bool(true);
    Value prev = evalExpr(e->operands[0]);
    for (size_t i = 0; i < e->ops.size() && i + 1 < e->operands.size(); ++i) {
        Value next = evalExpr(e->operands[i + 1]);
        Value cmp = evalCmpOp(e->ops[i], prev, next);
        if (!cmp.as_bool()) return Value::make_bool(false);
        prev = next;
    }
    return Value::make_bool(true);
}

/**
 * @brief Power expression (base ** exp) evaluate करता है।
 *
 * @param e Power expression node।
 * @return दोनों operands integer हों तो integer result; नहीं तो float।
 */
Value Interpreter::evalPower(PowerExpr* e) {
    Value base = evalExpr(e->base);
    Value exp = evalExpr(e->exp);
    double result = std::pow(base.as_float(), exp.as_float());
    if (base.kind != Value::Float && exp.kind != Value::Float) {
        return Value::make_int(static_cast<int64_t>(result));
    }
    return Value::make_float(result);
}

/**
 * @brief Value को target primitive या pointer type के रूप में reinterpret करता है।
 *
 * Pointer casts raw bit pattern preserve करते हैं। Primitive casts Value::convert_to use करते हैं।
 *
 * @param e Postfix cast expression node।
 * @return Target type में reinterpreted value।
 */
Value Interpreter::evalPostfixCast(PostfixCastExpr* e) {
    Value v = evalExpr(e->expr);
    if (!e->target_type) return v;
    if (e->target_type->kind == Type::Prim) {
        auto pk = std::get<PrimitiveType>(e->target_type->data).kind;
        return v.convert_to(pk);
    }
    if (e->target_type->kind == Type::Pointer) {
        return Value::make_ptr(reinterpret_cast<uint8_t*>(v.as_uint()));
    }
    return v;
}

/**
 * @brief Type या expression का byte size लौटाता है।
 *
 * Class types के लिए computed layout size पाने को struct_layouts_ consult करता है।
 *
 * @param e Sizeof expression node।
 * @return Byte size represent करता integer value।
 */
Value Interpreter::evalSizeof(SizeofExpr* e) {
    auto sizeOfType = [this](Type* ty) -> int64_t {
        if (!ty) return 8;
        if (ty->kind == Type::Class) {
            auto& ct = std::get<ClassType>(ty->data);
            std::string sname = ct.decl ? ct.decl->name : "";
            if (!sname.empty()) {
                auto lit = struct_layouts_.find(sname);
                if (lit != struct_layouts_.end())
                    return static_cast<int64_t>(lit->second.total_size);
            }
        }
        int64_t sz = ty->sizeInBytes();
        return sz > 0 ? sz : 8;
    };

    if (e->target_type) {
        return Value::make_int(sizeOfType(e->target_type));
    }
    if (e->target_expr) {
        Value v = evalExpr(e->target_expr);
        if (e->target_expr->resolved_type)
            return Value::make_int(sizeOfType(e->target_expr->resolved_type));
        switch (v.kind) {
        case Value::Int: case Value::UInt: case Value::Float: case Value::Ptr:
            return Value::make_int(8);
        case Value::Bool: return Value::make_int(1);
        default: return Value::make_int(0);
        }
    }
    return Value::make_int(0);
}

/**
 * @brief Class layout में struct member का byte offset लौटाता है।
 *
 * `lastclass` class name को most recently defined class में resolve करता है।
 *
 * @param e Offset expression node।
 * @return Named member का integer byte offset, या 0 अगर नहीं मिला।
 */
Value Interpreter::evalOffset(OffsetExpr* e) {
    std::string cls_name = e->class_name;
    if (cls_name == "lastclass" && last_class_decl_)
        cls_name = last_class_decl_->name;
    auto it = struct_layouts_.find(cls_name);
    if (it != struct_layouts_.end()) {
        auto fit = it->second.field_index.find(e->member_name);
        if (fit != it->second.field_index.end() &&
            fit->second < it->second.field_offsets.size()) {
            const auto& fl = it->second.field_offsets[fit->second];
            return Value::make_int(static_cast<int64_t>(fl.offset));
        }
    }
    return Value::make_int(0);
}

/**
 * @brief x86-64 SysV ABI के through extern function call करता है, ज़्यादा से ज़्यादा 6 integer या pointer arguments के साथ।
 *
 * @param fn Call करने वाले function का pointer।
 * @param args Pass करने वाले arguments का array, हर एक int64_t bit-pattern के रूप में।
 * @param nargs Pass करने वाले arguments की संख्या; 6 तक clamped।
 * @return Integer या pointer result as int64_t bit-pattern।
 */
static int64_t call_extern_i64(void* fn, int64_t* args, int nargs) {
    switch (nargs) {
    case 0: return ((int64_t(*)())fn)();
    case 1: return ((int64_t(*)(int64_t))fn)(args[0]);
    case 2: return ((int64_t(*)(int64_t,int64_t))fn)(args[0],args[1]);
    case 3: return ((int64_t(*)(int64_t,int64_t,int64_t))fn)(args[0],args[1],args[2]);
    case 4: return ((int64_t(*)(int64_t,int64_t,int64_t,int64_t))fn)(args[0],args[1],args[2],args[3]);
    case 5: return ((int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t))fn)(args[0],args[1],args[2],args[3],args[4]);
    default: return ((int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t))fn)(args[0],args[1],args[2],args[3],args[4],args[5]);
    }
}

/**
 * @brief x86-64 SysV ABI के through extern function call करता है, ज़्यादा से ज़्यादा 1 double argument के साथ।
 *
 * @param fn Call करने वाले function का pointer।
 * @param args Arguments का array; nargs >= 1 होने पर args[0] को double के रूप में reinterpret किया जाता है।
 * @param nargs Pass करने वाले arguments की संख्या।
 * @return Double result, या 0.0 अगर nargs > 1।
 */
static double call_extern_f64(void* fn, int64_t* args, int nargs) {
    switch (nargs) {
    case 0: return ((double(*)())fn)();
    case 1: return ((double(*)(double))fn)(*reinterpret_cast<double*>(&args[0]));
    default: return 0.0;
    }
}

/**
 * @brief Function call resolve और invoke करता है, method dispatch, inheritance, और extern symbols handle करता है।
 *
 * call_stack_ पर नया Frame push करता है, parameters bind करता है, body execute करता है,
 * फिर return से पहले सारी saved interpreter state restore करता है।
 *
 * @param e Call expression node।
 * @return Called function का returned value।
 */
Value Interpreter::evalCall(CallExpr* e) {
    std::string fname;
    Value this_val;
    bool has_this = false;

    if (auto* id = dynamic_cast<IdentifierExpr*>(e->callee)) {
        fname = id->name;
        if (!isBuiltin(fname) && functions_.find(fname) == functions_.end() && extern_syms_.find(fname) == extern_syms_.end()) {
            Value v = getVar(fname);
            if (v.kind == Value::FuncPtr)
                fname = v.str_val;
        }
        // Qualified parent call (जैसे Vehicle$SetSpeed) — implicit 'this' inject करो
        if (fname.find('$') != std::string::npos && !current_method_class_.empty() && !has_this) {
            Value* thisPtr = getVarPtr("this");
            if (thisPtr) {
                this_val = *thisPtr;
                has_this = true;
            }
        }
    } else if (auto* fa = dynamic_cast<FieldAccessExpr*>(e->callee)) {
        this_val = evalExpr(fa->object);
        has_this = true;
        std::string class_name;
        if (fa->object->resolved_type) {
            Type* objTy = fa->object->resolved_type;
            if (fa->is_arrow && objTy->kind == Type::Pointer) {
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
        std::string lookup_key = class_name + "$" + fa->field;
        auto mcit = method_cache_.find(lookup_key);
        if (mcit != method_cache_.end()) {
            fname = mcit->second;
        } else {
            fname = lookup_key;
            auto cdit = class_decls_.find(class_name);
            if (functions_.find(fname) == functions_.end() && cdit != class_decls_.end()) {
                std::string base = cdit->second->base_name;
                while (!base.empty() && functions_.find(fname) == functions_.end()) {
                    fname = base + "$" + fa->field;
                    auto bit = class_decls_.find(base);
                    if (bit == class_decls_.end()) break;
                    base = bit->second->base_name;
                }
            }
            method_cache_.try_emplace(lookup_key, fname);
        }
    } else {
        Value callee_val = evalExpr(e->callee);
        if (callee_val.kind == Value::FuncPtr) {
            fname = callee_val.str_val;
        } else {
            diag_.error(e->loc, "indirect function calls require a function pointer value");
            return Value::make_void();
        }
    }

    std::vector<Value> args;
    args.reserve(e->args.size());
    for (auto* arg : e->args) {
        args.push_back(evalExpr(arg));
    }

    {
        auto bit = builtins_.find(fname);
        if (bit != builtins_.end()) return bit->second(args);
    }

    {
        auto eit = extern_syms_.find(fname);
        if (eit != extern_syms_.end()) {
            std::vector<int64_t> iargs;
            iargs.reserve(args.size());
            for (auto& av : args) {
                if (av.kind == Value::Ptr)
                    iargs.push_back(reinterpret_cast<int64_t>(av.ptr));
                else if (av.kind == Value::Float)
                    iargs.push_back(*reinterpret_cast<int64_t*>(&av.f));
                else
                    iargs.push_back(av.as_int());
            }
            if (eit->second.return_type == "f64") {
                double r = call_extern_f64(eit->second.sym, iargs.data(), static_cast<int>(iargs.size()));
                return Value::make_float(r);
            } else {
                int64_t r = call_extern_i64(eit->second.sym, iargs.data(), static_cast<int>(iargs.size()));
                if (eit->second.return_type == "ptr")
                    return Value::make_ptr(reinterpret_cast<uint8_t*>(r));
                return Value::make_int(r);
            }
        }
    }

    auto fit = functions_.find(fname);
    if (fit == functions_.end()) {
        diag_.error(e->loc, "undefined function: " + fname);
        return Value::make_void();
    }

    FuncDecl* func = fit->second;

    static constexpr size_t kMaxCallDepth = 2000;
    if (call_stack_.size() >= kMaxCallDepth) {
        diag_.error(e->loc, "call stack overflow (max depth " +
                    std::to_string(kMaxCallDepth) + ")");
        return Value::make_void();
    }

    Frame frame;
    frame.func_name = fname;
    call_stack_.push_back(std::move(frame));

    std::string saved_func_name = current_func_name_;
    current_func_name_ = fname;

    std::string saved_method_class = current_method_class_;
    if (has_this) {
        call_stack_.back().vars["this"] = this_val;
        size_t dollar = fname.find('$');
        if (dollar != std::string::npos)
            current_method_class_ = fname.substr(0, dollar);
    } else {
        current_method_class_.clear();
    }

    for (size_t i = 0; i < func->params.size(); ++i) {
        ParamDecl* param = func->params[i];
        Value arg_val;
        if (i < args.size()) {
            arg_val = args[i];
        } else if (param->default_value) {
            arg_val = evalExpr(param->default_value);
        } else {
            arg_val = Value::make_int(0);
        }
        call_stack_.back().vars[param->name] = arg_val;
    }

    if (func->is_vararg) {
        for (size_t i = func->params.size(); i < args.size(); ++i)
            call_stack_.back().varargs.push_back(args[i]);
    }

    decltype(labels_) saved_labels;
    std::swap(labels_, saved_labels);
    if (func->body) scanLabels(func->body);
    if (func->body) execCompound(func->body);

    Value ret = call_stack_.back().return_val;
    call_stack_.pop_back();
    std::swap(labels_, saved_labels);
    current_func_name_ = saved_func_name;
    current_method_class_ = saved_method_class;
    signal_ = Signal::None;

    return ret;
}

/**
 * @brief Struct या union field को उसके computed byte offset पर read करता है।
 *
 * Bit-fields, pointer-typed fields, और IntrinsicUnion pseudo-type handle करता है।
 *
 * @param e Field-access expression node।
 * @return Raw memory से extract किया field value, या 0 अगर field नहीं मिला।
 */
Value Interpreter::evalFieldAccess(FieldAccessExpr* e) {
    Value obj = evalExpr(e->object);
    uint8_t* rawPtr = nullptr;
    if (obj.kind == Value::Ptr)
        rawPtr = obj.ptr;
    else if (obj.kind == Value::Int || obj.kind == Value::UInt)
        rawPtr = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(obj.as_uint()));

    if (rawPtr) {
        Type* objTy = e->object->resolved_type;
        if (objTy && objTy->kind == Type::IntrinsicUnion) {
            const std::string& fn = e->field;
            if (fn == "i64" || fn == "u64") {
                int64_t v; std::memcpy(&v, rawPtr, 8);
                return Value::make_int(v);
            }
            if (fn == "f64") {
                double d; std::memcpy(&d, rawPtr, 8);
                return Value::make_float(d);
            }
            // Sub-element fields (u8[], u16[] आदि): base का pointer लौटाओ; caller index करेगा
            return Value::make_ptr(rawPtr);
        }

        std::string struct_name;
        if (e->object->resolved_type) {
            Type* objTy2 = e->object->resolved_type;
            if (objTy2->kind == Type::Pointer) {
                auto& pt = std::get<PointerType>(objTy2->data);
                if (pt.pointee && pt.pointee->kind == Type::Class) {
                    auto& ct = std::get<ClassType>(pt.pointee->data);
                    if (ct.decl) struct_name = ct.decl->name;
                }
            } else if (objTy2->kind == Type::Class) {
                auto& ct = std::get<ClassType>(objTy2->data);
                if (ct.decl) struct_name = ct.decl->name;
            }
        }
        FieldDecl* resolved_field = nullptr;
        if (!struct_name.empty()) {
            auto cit = class_decls_.find(struct_name);
            if (cit != class_decls_.end()) {
                for (auto* m : cit->second->members) {
                    if (auto* fd = dynamic_cast<FieldDecl*>(m)) {
                        if (fd->name == e->field) { resolved_field = fd; break; }
                    }
                }
            }
        }

        auto lit = struct_layouts_.find(struct_name);
        if (lit != struct_layouts_.end()) {
            const auto& layout = lit->second;
            auto fit = layout.field_index.find(e->field);
            if (fit != layout.field_index.end() &&
                fit->second < layout.field_offsets.size()) {
                const auto& fl = layout.field_offsets[fit->second];
                uint64_t raw = 0;
                std::memcpy(&raw, rawPtr + fl.offset, sizeof(uint64_t));
                if (fl.bit_width > 0 && fl.bit_width < 64 && fl.bit_start >= 0 && fl.bit_start < 64) {
                    uint64_t mask = (1ULL << fl.bit_width) - 1;
                    return Value::make_int(static_cast<int64_t>((raw >> fl.bit_start) & mask));
                }
                if (e->resolved_type && e->resolved_type->kind == Type::Pointer)
                    return Value::make_ptr(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(raw)));
                if (resolved_field && resolved_field->type &&
                    resolved_field->type->kind == Type::Prim) {
                    auto& pt = std::get<PrimitiveType>(resolved_field->type->data);
                    if (pt.kind == PrimKind::F64) {
                        double d;
                        std::memcpy(&d, rawPtr + fl.offset, sizeof(double));
                        return Value::make_float(d);
                    }
                }
                return Value::make_int(static_cast<int64_t>(raw));
            }
        }
    }
    return Value::make_int(0);
}

/**
 * @brief Heap-allocated array में index करता है, resolved type info से element size respect करता है।
 *
 * @param e Array-index expression node।
 * @return Element value; nested array types के लिए chained indexing support करने को pointer लौटाता है।
 */
Value Interpreter::evalArrayIndex(ArrayIndexExpr* e) {
    Value base = evalExpr(e->base);
    Value index = evalExpr(e->index);

    if (base.kind == Value::Str) {
        int64_t base_off = base.i;
        size_t idx = static_cast<size_t>(base_off + index.as_int());
        if (idx < base.str_val.size()) {
            return Value::make_int(static_cast<int64_t>(static_cast<uint8_t>(base.str_val[idx])));
        }
        return Value::make_int(0);
    }

    uint8_t* rawPtr = nullptr;
    if (base.kind == Value::Ptr) {
        rawPtr = base.ptr;
    } else if (base.kind == Value::Int || base.kind == Value::UInt) {
        rawPtr = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(base.as_uint()));
    }

    if (rawPtr) {
        size_t elemSz = 8;
        if (e->resolved_type) {
            size_t s = static_cast<size_t>(e->resolved_type->sizeInBytes());
            if (s > 0) elemSz = s;
        }
        int64_t idx = index.as_int();
        if (idx < 0) {
            diag_.error({}, "array index " + std::to_string(idx) + " is negative");
            return Value::make_int(0);
        }
        if ((uint64_t)idx > 0x10000000ULL / elemSz) return Value::make_int(0);
        uint8_t* elemPtr = rawPtr + (uint64_t)idx * elemSz;

        // Nested array: chained m[i][j] indexing enable करने के लिए buffer में pointer लौटाओ
        if (e->resolved_type && e->resolved_type->kind == Type::Array) {
            return Value::make_ptr(elemPtr);
        }

        bool is_ptr_type = e->resolved_type && e->resolved_type->kind == Type::Pointer;

        switch (elemSz) {
        case 1: return Value::make_int(*reinterpret_cast<int8_t*>(elemPtr));
        case 2: { int16_t v; std::memcpy(&v, elemPtr, 2); return Value::make_int(v); }
        case 4: { int32_t v; std::memcpy(&v, elemPtr, 4); return Value::make_int(v); }
        default: {
            uint64_t v; std::memcpy(&v, elemPtr, 8);
            if (is_ptr_type) return Value::make_ptr(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(v)));
            return Value::make_int(static_cast<int64_t>(v));
        }
        }
    }

    return Value::make_int(0);
}

/**
 * @brief NodeKind से single statement dispatch करता है; signal pending हो तो तुरंत return करता है।
 *
 * @param s Execute करने वाला statement node।
 */
void Interpreter::execStmt(Stmt* s) {
    if (!s || signal_ != Signal::None) return;

    switch (s->nk) {
    case NodeKind::CompoundStmt:
        execCompound(static_cast<CompoundStmt*>(s));
        break;
    case NodeKind::DeclStmt: {
        auto* ds = static_cast<DeclStmt*>(s);
        if (ds->decl) execDecl(ds->decl);
        break;
    }
    case NodeKind::ExprStmt: {
        auto* es = static_cast<ExprStmt*>(s);
        if (es->expr) evalExpr(es->expr);
        break;
    }
    case NodeKind::IfStmt:
        execIf(static_cast<IfStmt*>(s));
        break;
    case NodeKind::ForStmt:
        execFor(static_cast<ForStmt*>(s));
        break;
    case NodeKind::WhileStmt:
        execWhile(static_cast<WhileStmt*>(s));
        break;
    case NodeKind::DoWhileStmt:
        execDoWhile(static_cast<DoWhileStmt*>(s));
        break;
    case NodeKind::SwitchStmt:
        execSwitch(static_cast<SwitchStmt*>(s));
        break;
    case NodeKind::ReturnStmt:
        execReturn(static_cast<ReturnStmt*>(s));
        break;
    case NodeKind::BreakStmt:
        signal_ = Signal::Break;
        break;
    case NodeKind::ContinueStmt:
        signal_ = Signal::Continue;
        break;
    case NodeKind::GotoStmt: {
        auto* gs = static_cast<GotoStmt*>(s);
        pending_goto_ = gs->label;
        signal_ = Signal::Break; // Break use करो ताकि label वाले compound तक unwind हो सके
        break;
    }
    case NodeKind::LabelStmt: {
        auto* ls = static_cast<LabelStmt*>(s);
        if (ls->stmt) execStmt(ls->stmt);
        break;
    }
    case NodeKind::StringOutputStmt:
        execStringOutput(static_cast<StringOutputStmt*>(s));
        break;
    case NodeKind::ExeBlockStmt: {
        auto* eb = static_cast<ExeBlockStmt*>(s);
        if (eb->body) execCompound(eb->body);
        break;
    }
    case NodeKind::TryCatchStmt: {
        auto* tc = static_cast<TryCatchStmt*>(s);
        try {
            if (tc->try_body) execCompound(tc->try_body);
        } catch (HolyCException& e) {
            if (tc->catch_body) {
                setVar("__except_code", Value::make_int(e.code));
                signal_ = Signal::None;
                execCompound(tc->catch_body);
            }
        }
        break;
    }
    case NodeKind::AsmStmt: {
        auto* as = static_cast<AsmStmt*>(s);
        std::string raw = as->raw_text;
        auto trim = [](std::string& str) {
            size_t a = str.find_first_not_of(" \t\n\r;");
            size_t b = str.find_last_not_of(" \t\n\r;");
            if (a == std::string::npos) { str = ""; return; }
            str = str.substr(a, b - a + 1);
        };
        trim(raw);
        std::string lraw = raw;
        for (auto& c : lraw) c = (char)tolower((unsigned char)c);

        if (lraw == "nop" || lraw.empty()) {
        } else if (lraw.find("rdtsc") != std::string::npos) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t ticks = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
            uint32_t lo = (uint32_t)(ticks & 0xFFFFFFFF);
            uint32_t hi = (uint32_t)(ticks >> 32);
            for (size_t oi = 0; oi < as->outputs.size(); oi++) {
                Value val = (oi == 0) ? Value::make_int((int64_t)lo) : Value::make_int((int64_t)hi);
                if (as->outputs[oi].expr)
                    assignToExpr(as->outputs[oi].expr, val);
            }
        } else if (lraw.find("cpuid") != std::string::npos) {
            uint32_t vals[4] = {0x000206a7, 0x00100800, 0x1fbae3bf, 0xbfebfbff};
            for (size_t oi = 0; oi < as->outputs.size() && oi < 4; oi++) {
                if (as->outputs[oi].expr)
                    assignToExpr(as->outputs[oi].expr, Value::make_int((int64_t)vals[oi]));
            }
        } else {
            bool bound_any = false;
            for (auto& op : as->outputs) {
                if (op.expr) {
                    assignToExpr(op.expr, Value::make_int(0));
                    bound_any = true;
                }
            }
            if (!bound_any) {
                std::string snippet = raw.size() > 40 ? raw.substr(0, 40) : raw;
                diag_.warning(as->loc, "inline asm ignored in interpreter mode: " + snippet);
            }
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief Statements का block sequentially execute करता है।
 *
 * Backward gotos पर goto-as-loop semantics implement करने के लिए block re-enter करता है।
 *
 * @param s Execute करने वाला compound statement node।
 */
void Interpreter::execCompound(CompoundStmt* s) {
    for (size_t i = 0; i < s->stmts.size(); ++i) {
        execStmt(s->stmts[i]);

        // Goto handling: backward gotos (loops) support करने के लिए while(true) re-enter करता है।
        if (!pending_goto_.empty() && signal_ == Signal::Break) [[unlikely]] {
            while (true) {
                auto it = labels_.find(pending_goto_);
                if (it == labels_.end() || it->second.parent != s) break;
                pending_goto_.clear();
                signal_ = Signal::None;
                for (size_t j = it->second.index; j < s->stmts.size(); ++j) {
                    execStmt(s->stmts[j]);
                    if (signal_ != Signal::None) break;
                }
                if (pending_goto_.empty() || signal_ != Signal::Break) break;
            }
            return;
        }

        if (signal_ != Signal::None) [[unlikely]] return;
    }
}

/**
 * @brief Condition evaluate करके ली गई branch execute करता है; unchosen branch पूरी तरह skip होती है।
 *
 * @param s If statement node।
 */
void Interpreter::execIf(IfStmt* s) {
    Value cond = evalExpr(s->cond);
    if (cond.as_bool()) {
        if (s->then_body) execStmt(s->then_body);
    } else {
        if (s->else_body) execStmt(s->else_body);
    }
}

/**
 * @brief For loop execute करता है; init एक बार run करता है, हर iteration से पहले cond test करता है, और हर body के बाद post run करता है।
 *
 * @param s For statement node।
 */
void Interpreter::execFor(ForStmt* s) {
    if (s->init) execStmt(s->init);
    while (signal_ == Signal::None) {
        if (s->cond) {
            Value cond = evalExpr(s->cond);
            if (!cond.as_bool()) break;
        }
        if (s->body) execStmt(s->body);
        if (signal_ == Signal::Break) { signal_ = Signal::None; break; }
        if (signal_ == Signal::Continue) signal_ = Signal::None;
        if (signal_ == Signal::Return) return;
        if (s->post) evalExpr(s->post);
    }
}

/**
 * @brief While loop execute करता है; हर body execution से पहले cond test करता है।
 *
 * @param s While statement node।
 */
void Interpreter::execWhile(WhileStmt* s) {
    while (signal_ == Signal::None) {
        Value cond = evalExpr(s->cond);
        if (!cond.as_bool()) break;
        if (s->body) execStmt(s->body);
        if (signal_ == Signal::Break) { signal_ = Signal::None; break; }
        if (signal_ == Signal::Continue) signal_ = Signal::None;
        if (signal_ == Signal::Return) return;
    }
}

/**
 * @brief Do-while loop execute करता है; cond test से पहले body हमेशा कम से कम एक बार run होती है।
 *
 * @param s Do-while statement node।
 */
void Interpreter::execDoWhile(DoWhileStmt* s) {
    do {
        if (s->body) execStmt(s->body);
        if (signal_ == Signal::Break) { signal_ = Signal::None; break; }
        if (signal_ == Signal::Continue) signal_ = Signal::None;
        if (signal_ == Signal::Return) return;
        Value cond = evalExpr(s->cond);
        if (!cond.as_bool()) break;
    } while (signal_ == Signal::None);
}

/**
 * @brief Switch statement execute करता है; value ranges (case N..M:) और fall-through support करता है।
 *
 * Matching cases internal runCasesFrom lambda के through execute होते हैं जो fall-through handle करता है।
 * अगर कोई case match न हो तो default case execute होता है अगर एक present हो।
 *
 * @param s Switch statement node।
 */
void Interpreter::execSwitch(SwitchStmt* s) {
    Value expr = evalExpr(s->expr);
    bool matched = false;
    bool found_default = false;
    size_t default_idx = 0;

    auto runCasesFrom = [&](size_t start) {
        for (size_t j = start; j < s->cases.size(); ++j) {
            for (auto* stmt : s->cases[j]->stmts) {
                execStmt(stmt);
                if (signal_ == Signal::Break) { signal_ = Signal::None; return; }
                if (signal_ != Signal::None) return;
            }
        }
    };

    for (size_t i = 0; i < s->cases.size(); ++i) {
        CaseStmt* cs = s->cases[i];
        if (!cs->value) {
            found_default = true;
            default_idx = i;
            continue;
        }
        Value case_val = evalExpr(cs->value);
        bool case_match;
        if (cs->range_end) {
            Value range_hi = evalExpr(cs->range_end);
            int64_t v = expr.as_int(), lo = case_val.as_int(), hi = range_hi.as_int();
            case_match = (v >= lo && v <= hi);
        } else {
            case_match = (expr.as_int() == case_val.as_int());
        }
        if (case_match) {
            matched = true;
            runCasesFrom(i);
            return;
        }
    }

    if (!matched && found_default)
        runCasesFrom(default_idx);
}

/**
 * @brief Return value current frame में store करता है और execCompound unwind करने को Signal::Return raise करता है।
 *
 * @param s Return statement node।
 */
void Interpreter::execReturn(ReturnStmt* s) {
    Value ret = s->value ? evalExpr(s->value) : Value::make_void();
    if (!call_stack_.empty()) {
        call_stack_.back().return_val = ret;
        call_stack_.back().has_returned = true;
    }
    signal_ = Signal::Return;
}

/**
 * @brief HolyC string-output statement ("fmt"(args)) format और print करता है।
 *
 * सभी argument expressions evaluate करता है, formatPrint के through string format करता है,
 * और result stdout पर write करता है।
 *
 * @param s String-output statement node।
 */
void Interpreter::execStringOutput(StringOutputStmt* s) {
    if (!s->format) return;
    std::vector<Value> args;
    args.reserve(s->args.size());
    for (auto* arg : s->args) {
        args.push_back(evalExpr(arg));
    }
    std::string out = formatPrint(s->format->value, args);
    std::fputs(out.c_str(), stdout);
}

/**
 * @brief Interpreter state में declaration register करता है।
 *
 * Variables current call frame या globals से bind होते हैं जब कोई frame active नहीं होता।
 * Classes computeStructLayout trigger करती हैं और अपने methods register करती हैं। Extern declarations
 * dlsym के through resolve होकर extern_syms_ में store होते हैं। Enums globals_ populate करते हैं।
 *
 * @param d Register करने वाला declaration node।
 */
void Interpreter::execDecl(Decl* d) {
    if (!d) return;

    switch (d->nk) {
    case NodeKind::VarDecl: {
        auto* vd = static_cast<VarDecl*>(d);
        if (vd->storage == Storage::Static) {
            // Static locals static_vars_ में "funcName.varName" key से रहते हैं
            std::string key = current_func_name_ + "." + vd->name;
            if (static_initialized_.find(key) == static_initialized_.end()) {
                Value val;
                if (vd->init) {
                    val = evalExpr(vd->init);
                } else {
                    val = Value::make_int(0);
                }
                static_vars_[key] = val;
                static_initialized_.insert(key);
            }
            if (!call_stack_.empty()) {
                call_stack_.back().static_keys[vd->name] = std::move(key);
            }
            break;
        }
        Value val;
        if (vd->init && vd->init->nk == NodeKind::InitListExpr &&
            vd->type && vd->type->kind == Type::Class) {
            auto* il = static_cast<InitListExpr*>(vd->init);
            auto& ct = std::get<ClassType>(vd->type->data);
            std::string sname = ct.decl ? ct.decl->name : "";
            auto lit = struct_layouts_.find(sname);
            size_t sz = lit != struct_layouts_.end() ? lit->second.total_size : 8;
            uint8_t* buf = heap_.calloc(sz);
            val = Value::make_ptr(buf);
            if (lit != struct_layouts_.end()) {
                auto& layout = lit->second;
                size_t fi = 0;
                for (auto& fl : layout.field_offsets) {
                    if (fl.bit_width >= 0) continue; // skip bitfields
                    if (fi >= il->values.size()) break;
                    Value ev = evalExpr(il->values[fi++]);
                    std::memcpy(buf + fl.offset, &ev.u, sizeof(uint64_t));
                }
            }
        } else if (vd->type && vd->type->kind == Type::Array &&
            vd->init && vd->init->nk == NodeKind::InitListExpr) {
            auto* il = static_cast<InitListExpr*>(vd->init);
            size_t totalCount = 1;
            Type* elemTy = vd->type;
            while (elemTy && elemTy->kind == Type::Array) {
                auto& at = std::get<ArrayType>(elemTy->data);
                if (at.size > 0) {
                    if (totalCount > 0x10000000 / static_cast<size_t>(at.size))
                        totalCount = 0x10000000;
                    else
                        totalCount *= static_cast<size_t>(at.size);
                }
                elemTy = at.element;
            }
            size_t elemSz = elemTy ? static_cast<size_t>(elemTy->sizeInBytes()) : 8;
            if (elemSz == 0) elemSz = 8;
            size_t allocSz = (elemSz > 0 && totalCount > 0x40000000 / elemSz)
                ? 0x40000000 : totalCount * elemSz;
            uint8_t* buf = heap_.calloc(allocSz);
            for (size_t i = 0; i < il->values.size() && i < totalCount; ++i) {
                Value ev = evalExpr(il->values[i]);
                uint8_t* slot = buf + i * elemSz;
                if (elemSz == 1) {
                    *slot = static_cast<uint8_t>(ev.as_int());
                } else if (elemSz == 2) {
                    int16_t v16 = static_cast<int16_t>(ev.as_int());
                    std::memcpy(slot, &v16, 2);
                } else if (elemSz == 4) {
                    int32_t v32 = static_cast<int32_t>(ev.as_int());
                    std::memcpy(slot, &v32, 4);
                } else {
                    int64_t v64 = ev.as_int();
                    std::memcpy(slot, &v64, 8);
                }
            }
            val = Value::make_ptr(buf);
        } else if (vd->type && vd->type->kind == Type::Array &&
                   vd->init && vd->init->nk == NodeKind::StringLiteralExpr) {
            auto* sl = static_cast<StringLiteralExpr*>(vd->init);
            auto& at = std::get<ArrayType>(vd->type->data);
            int count = at.size > 0 ? at.size : static_cast<int>(sl->value.size() + 1);
            uint8_t* buf = heap_.calloc(static_cast<size_t>(count));
            size_t copyLen = std::min(sl->value.size(), static_cast<size_t>(count > 0 ? count - 1 : 0));
            std::memcpy(buf, sl->value.data(), copyLen);
            val = Value::make_ptr(buf);
        } else if (vd->init) {
            val = evalExpr(vd->init);
            // F32 declare हो तो float precision पर truncate करो
            if (vd->type && vd->type->kind == Type::Prim &&
                std::get<PrimitiveType>(vd->type->data).kind == PrimKind::F32 &&
                val.kind == Value::Float) {
                val = Value::make_float(static_cast<double>(static_cast<float>(val.f)));
            }
        } else if (vd->type && vd->type->kind == Type::Array) {
            auto& at = std::get<ArrayType>(vd->type->data);
            size_t count = at.size > 0 ? static_cast<size_t>(at.size) : 1;
            size_t elemSz = at.element ? static_cast<size_t>(at.element->sizeInBytes()) : 8;
            if (elemSz == 0) elemSz = 8;
            size_t allocSz = (elemSz > 0 && count > 0x40000000 / elemSz)
                ? 0x40000000 : count * elemSz;
            val = Value::make_ptr(heap_.calloc(allocSz));
        } else if (vd->type && vd->type->kind == Type::Class) {
            auto& ct = std::get<ClassType>(vd->type->data);
            std::string sname = ct.decl ? ct.decl->name : "";
            auto lit = struct_layouts_.find(sname);
            size_t sz = lit != struct_layouts_.end() ? lit->second.total_size : 8;
            val = Value::make_ptr(heap_.calloc(sz));
        } else if (vd->type && vd->type->kind == Type::IntrinsicUnion) {
            val = Value::make_ptr(heap_.calloc(8));
        } else {
            val = Value::make_int(0);
        }
        if (!call_stack_.empty()) {
            call_stack_.back().vars[vd->name] = val;
        } else {
            globals_[vd->name] = val;
        }
        break;
    }
    case NodeKind::FuncDecl: {
        auto* fd = static_cast<FuncDecl*>(d);
        functions_[fd->name] = fd;
        break;
    }
    case NodeKind::ClassDecl: {
        auto* cd = static_cast<ClassDecl*>(d);
        class_decls_[cd->name] = cd;
        last_class_decl_ = cd;
        method_cache_.clear();
        computeStructLayout(cd);
        for (auto* m : cd->members) {
            if (m->nk == NodeKind::FuncDecl) {
                auto* fd = static_cast<FuncDecl*>(m);
                functions_[cd->name + "$" + fd->name] = fd;
            }
        }
        break;
    }
    case NodeKind::EnumDecl: {
        auto* ed = static_cast<EnumDecl*>(d);
        int64_t counter = 0;
        for (auto& m : ed->members) {
            if (m.value) counter = evalExpr(m.value).as_int();
            globals_[m.name] = Value::make_int(counter);
            counter++;
        }
        break;
    }
    case NodeKind::TypedefDecl:
    case NodeKind::UnionDecl:
        break;
    case NodeKind::ExternDecl: {
        auto* ed = static_cast<ExternDecl*>(d);
        if (!ed->inner || ed->inner->nk != NodeKind::FuncDecl) break;
        auto* fd = static_cast<FuncDecl*>(ed->inner);
        if (extern_syms_.count(fd->name)) break;
        void* sym = dlsym(RTLD_DEFAULT, fd->name.c_str());
        if (!sym) break;
        ExternBinding binding;
        binding.sym = sym;
        if (fd->return_type) {
            if (fd->return_type->isFloat())        binding.return_type = "f64";
            else if (fd->return_type->isPointer()) binding.return_type = "ptr";
            else if (fd->return_type->isVoid())    binding.return_type = "void";
            else                                    binding.return_type = "i64";
        } else binding.return_type = "i64";
        binding.param_types.reserve(fd->params.size());
        for (auto* param : fd->params) {
            if (!param->type)                       binding.param_types.push_back("i64");
            else if (param->type->isFloat())        binding.param_types.push_back("f64");
            else if (param->type->isPointer())      binding.param_types.push_back("ptr");
            else                                    binding.param_types.push_back("i64");
        }
        extern_syms_[fd->name] = binding;
        break;
    }
    default:
        break;
    }
}

/**
 * @brief Class के लिए field byte offsets compute करता है, पहले base class layout inherit करता है।
 *
 * Bit-fields contiguous 8-byte words में pack होते हैं। Same group में union members एक ही
 * starting offset share करते हैं। Results struct_layouts_[cd->name] में store होते हैं।
 *
 * @param cd वह class declaration जिसका layout compute करना है।
 */
void Interpreter::computeStructLayout(ClassDecl* cd) {
    StructLayout layout;
    layout.field_offsets.reserve(cd->members.size());
    size_t offset = 0;

    if (!cd->base_name.empty()) {
        auto bit = struct_layouts_.find(cd->base_name);
        if (bit != struct_layouts_.end()) {
            for (auto& fl : bit->second.field_offsets) {
                layout.field_offsets.push_back(fl);
            }
            offset = bit->second.total_size;
        }
    }

    // Contiguous bit fields 8-byte words में pack होते हैं।
    size_t current_word_byte_offset = 0;
    int current_bit_offset = 0;    // current word में use हुए bits
    bool in_bit_field_run = false;

    std::unordered_map<int, size_t> union_group_start;
    std::unordered_map<int, size_t> union_group_max;

    {   // First pass: union group sizes और starting offsets compute करो
        size_t tmp_offset = offset;
        for (auto* member : cd->members) {
            if (member->nk != NodeKind::FieldDecl) continue;
            auto* fd = static_cast<FieldDecl*>(member);
            {
                if (fd->is_union_member && fd->union_group >= 0) {
                    int grp = fd->union_group;
                    size_t sz = fd->type ? static_cast<size_t>(fd->type->sizeInBytes()) : 8;
                    if (sz == 0) sz = 8;
                    if (union_group_start.find(grp) == union_group_start.end()) {
                        size_t align = sz > 8 ? 8 : sz;
                        size_t grp_off = (tmp_offset + align - 1) & ~(align - 1);
                        union_group_start[grp] = grp_off;
                        union_group_max[grp] = sz;
                    } else {
                        auto mit = union_group_max.find(grp);
                        if (mit != union_group_max.end() && sz > mit->second) mit->second = sz;
                    }
                } else if (!fd->is_union_member) {
                    size_t sz = fd->type ? static_cast<size_t>(fd->type->sizeInBytes()) : 8;
                    if (sz == 0) sz = 8;
                    size_t align = sz > 8 ? 8 : sz;
                    tmp_offset = (tmp_offset + align - 1) & ~(align - 1);
                    tmp_offset += sz;
                }
            }
        }
    }

    for (auto* member : cd->members) {
        if (member->nk != NodeKind::FieldDecl) continue;
        auto* fd = static_cast<FieldDecl*>(member);
        {
            if (fd->is_union_member && fd->union_group >= 0) {
                int grp = fd->union_group;
                auto grp_s = union_group_start.find(grp);
                size_t grp_start = (grp_s != union_group_start.end()) ? grp_s->second : offset;
                FieldLayout fl;
                fl.name = fd->name;
                fl.offset = grp_start;
                fl.bit_start = 0;
                fl.bit_width = -1;
                layout.field_offsets.push_back(fl);
                auto grp_m = union_group_max.find(grp);
                size_t grp_end = grp_start + (grp_m != union_group_max.end() ? grp_m->second : 8);
                if (grp_end > offset) offset = grp_end;
                continue;
            }

            if (fd->bit_width >= 0) {
                int width = fd->bit_width;
                if (!in_bit_field_run) {
                    size_t align = 8;
                    offset = (offset + align - 1) & ~(align - 1);
                    current_word_byte_offset = offset;
                    current_bit_offset = 0;
                    offset += 8; // reserve 8 bytes for the bit-field word
                    in_bit_field_run = true;
                } else if (current_bit_offset + width > 64) {
                    size_t align = 8;
                    offset = (offset + align - 1) & ~(align - 1);
                    current_word_byte_offset = offset;
                    current_bit_offset = 0;
                    offset += 8;
                }
                FieldLayout fl;
                fl.name = fd->name;
                fl.offset = current_word_byte_offset;
                fl.bit_start = current_bit_offset;
                fl.bit_width = width;
                layout.field_offsets.push_back(fl);
                current_bit_offset += width;
            } else {
                in_bit_field_run = false;
                current_bit_offset = 0;

                size_t sz = fd->type ? static_cast<size_t>(fd->type->sizeInBytes()) : 8;
                if (sz == 0) sz = 8;
                size_t align = sz > 8 ? 8 : sz;
                offset = (offset + align - 1) & ~(align - 1);
                FieldLayout fl;
                fl.name = fd->name;
                fl.offset = offset;
                fl.bit_start = 0;
                fl.bit_width = -1;
                layout.field_offsets.push_back(fl);
                offset += sz;
            }
        }
    }
    layout.total_size = offset;
    layout.field_index.reserve(layout.field_offsets.size());
    for (size_t fi = 0; fi < layout.field_offsets.size(); ++fi) {
        layout.field_index[layout.field_offsets[fi].name] = fi;
    }
    struct_layouts_[cd->name] = layout;
}

/**
 * @brief HolyC extensions के साथ printf-style string format करता है।
 *
 * Standard C specifiers के साथ %h (repeat modifier), %n (engineering notation),
 * %z (indexed null-separated list), %D (packed Now() value से date), और %T (time) support करता है।
 *
 * @param fmt Format string।
 * @param args Argument vector। Indexing arg_start से शुरू होती है।
 * @param arg_start args में वह index जहाँ से format arguments शुरू होते हैं; args[0] को format string itself बनने देता है।
 * @return Fully formatted output string।
 */
std::string Interpreter::formatPrint(const std::string& fmt, const std::vector<Value>& args, size_t arg_start) {
    const size_t fmt_len = fmt.size();
    std::string result;
    result.reserve(fmt_len);
    size_t arg_idx = 0;
    size_t i = 0;

    auto applyWidth = [](std::string& s, int width, bool left_align, bool zero_pad) {
        if (width > 0 && static_cast<int>(s.size()) < width) {
            int pad = width - static_cast<int>(s.size());
            char pad_char = zero_pad ? '0' : ' ';
            if (left_align) {
                s.append(pad, ' ');
            } else {
                std::string padded;
                padded.reserve(s.size() + pad);
                padded.append(pad, pad_char);
                padded.append(s);
                s = std::move(padded);
            }
        }
    };

    while (i < fmt_len) {
        if (fmt[i] != '%') {
            size_t lit_start = i;
            while (i < fmt_len && fmt[i] != '%') ++i;
            result.append(fmt, lit_start, i - lit_start);
            continue;
        }
        ++i;
        if (i >= fmt_len) break;
        if (fmt[i] == '%') { result += '%'; ++i; continue; }

        // %h HolyC repeat modifier है; standard flags से पहले parse करो
        bool h_modifier = false;
        bool h_star = false;
        int h_count = 0;
        if (fmt[i] == 'h') {
            h_modifier = true;
            ++i;
            if (i < fmt_len && fmt[i] == '*') {
                h_star = true; ++i;
            } else {
                while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9')
                    h_count = h_count * 10 + (fmt[i++] - '0');
            }
        }

        bool left_align = false, zero_pad = false, plus = false, space = false;
        for (;;) {
            if (i >= fmt_len) break;
            if      (fmt[i] == '-') { left_align = true; ++i; }
            else if (fmt[i] == '0') { zero_pad = true; ++i; }
            else if (fmt[i] == '+') { plus = true; ++i; }
            else if (fmt[i] == ' ') { space = true; ++i; }
            else break;
        }
        int width = 0;
        bool has_width = false;
        if (i < fmt_len && fmt[i] == '*') {
            width = arg_start + arg_idx < args.size() ? (int)args[arg_start + arg_idx++].as_int() : 0;
            has_width = true; ++i;
        } else {
            while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
                if (width <= 10000) width = width * 10 + (fmt[i] - '0');
                ++i;
                has_width = true;
            }
        }
        if (!has_width) width = 0;

        int precision = -1;
        if (i < fmt_len && fmt[i] == '.') {
            ++i; precision = 0;
            if (i < fmt_len && fmt[i] == '*') {
                precision = arg_start + arg_idx < args.size() ? (int)args[arg_start + arg_idx++].as_int() : 0;
                ++i;
            } else {
                while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
                    if (precision <= 100) precision = precision * 10 + (fmt[i] - '0');
                    ++i;
                }
            }
        }

        if (width < 0) width = 0;
        if (width > 10000) width = 10000;
        if (precision >= 0 && precision > 20) precision = 20;

        // Standard C length modifiers l/ll skip करो (z नहीं, वो HolyC format spec है)
        while (i < fmt_len && fmt[i] == 'l')
            ++i;

        if (i >= fmt_len) break;
        char spec = fmt[i++];

        if (h_modifier && h_star) {
            h_count = arg_start + arg_idx < args.size() ? (int)args[arg_start + arg_idx++].as_int() : 0;
        }

        Value arg = arg_start + arg_idx < args.size() ? args[arg_start + arg_idx++] : Value::make_int(0);

        char buf[256];
        buf[0] = '\0';
        std::string formatted;

        switch (spec) {
        case 'd': case 'i':
            if (h_modifier) {
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(arg.as_int()));
                formatted = buf;
                applyWidth(formatted, h_count > 0 ? h_count : width, left_align, zero_pad);
                result += formatted; continue;
            }
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(arg.as_int()));
            break;
        case 'u':
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(arg.as_uint()));
            break;
        case 'x':
            std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(arg.as_uint()));
            break;
        case 'X':
            std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(arg.as_uint()));
            break;
        case 'b': case 'B': {
            uint64_t val = arg.as_uint();
            if (val == 0) {
                buf[0] = '0'; buf[1] = '\0';
            } else {
                char tmp[65]; int pos = 64;
                while (val) { tmp[--pos] = '0' + (val & 1); val >>= 1; }
                int len = 64 - pos;
                std::memmove(buf, tmp + pos, len);
                buf[len] = '\0';
            }
            break;
        }
        case 'e':
            if (precision >= 0)
                std::snprintf(buf, sizeof(buf), "%.*e", precision, arg.as_float());
            else
                std::snprintf(buf, sizeof(buf), "%e", arg.as_float());
            break;
        case 'E':
            if (precision >= 0)
                std::snprintf(buf, sizeof(buf), "%.*E", precision, arg.as_float());
            else
                std::snprintf(buf, sizeof(buf), "%E", arg.as_float());
            break;
        case 'g':
            if (precision >= 0)
                std::snprintf(buf, sizeof(buf), "%.*g", precision, arg.as_float());
            else
                std::snprintf(buf, sizeof(buf), "%g", arg.as_float());
            break;
        case 'n': {
            // %n: 1000 की nearest power तक scaled engineering notation
            double val = arg.as_float();
            const char* suffixes[] = {"", "k", "M", "G", "T", "P"};
            const char* neg_suffixes[] = {"", "m", "u", "n", "p", "f"};
            double absval = std::fabs(val);
            int idx = 0;
            if (absval >= 1.0) {
                while (absval >= 1000.0 && idx < 5) { absval /= 1000.0; idx++; }
            } else if (absval > 0.0) {
                while (absval < 1.0 && idx < 5) { absval *= 1000.0; idx++; }
                if (val < 0) absval = -absval;
                std::snprintf(buf, sizeof(buf), "%g%s", absval, neg_suffixes[idx]);
                break;
            }
            if (val < 0) absval = -absval;
            std::snprintf(buf, sizeof(buf), "%g%s", absval, suffixes[idx]);
            break;
        }
        case 'z': {
            // %z: null-separated string list में index करो
            int64_t idx = arg.as_int();
            Value list_arg = arg_start + arg_idx < args.size() ? args[arg_start + arg_idx++] : Value::make_str("");
            const char* list = nullptr;
            if (list_arg.kind == Value::Str)
                list = list_arg.str_val.c_str();
            else if (list_arg.kind == Value::Ptr && list_arg.ptr)
                list = reinterpret_cast<const char*>(list_arg.ptr);
            if (list) {
                if (idx < 0) idx = 0;
                int64_t cur = 0;
                int64_t iters = 0;
                while (cur < idx && iters < 1000) {
                    while (*list) ++list; // current entry के end तक skip करो
                    ++list; ++cur; ++iters; // null के past, next entry
                    if (*list == '\0') break; // double-null: list का end
                }
                std::snprintf(buf, sizeof(buf), "%s", list);
            }
            break;
        }
        case 'c':
            if (h_modifier) {
                int count = h_count > 0 ? h_count : width;
                formatted.assign(count, static_cast<char>(arg.as_int() & 0xFF));
                result += formatted; continue;
            }
            buf[0] = static_cast<char>(arg.as_int() & 0xFF);
            buf[1] = '\0';
            break;
        case 's': {
            std::string s;
            if (arg.kind == Value::Str)
                s = arg.str_val;
            else if (arg.kind == Value::Ptr && arg.ptr)
                s = reinterpret_cast<const char*>(arg.ptr);
            else if ((arg.kind == Value::Int || arg.kind == Value::UInt) && arg.as_uint() != 0) {
                // Integer के रूप में stored pointer (जैसे U8** element से load हुआ)
                const char* cp = reinterpret_cast<const char*>(static_cast<uintptr_t>(arg.as_uint()));
                s = cp;
            }
            if (precision >= 0 && static_cast<int>(s.size()) > precision)
                s.resize(precision);
            applyWidth(s, width, left_align, false);
            result += s;
            continue;
        }
        case 'f':
            if (precision >= 0)
                std::snprintf(buf, sizeof(buf), "%.*f", precision, arg.as_float());
            else
                std::snprintf(buf, sizeof(buf), "%f", arg.as_float());
            break;
        case 'p':
            std::snprintf(buf, sizeof(buf), "%p", reinterpret_cast<void*>(arg.as_uint()));
            break;
        case 'D': {
            int64_t dv = arg.as_int();
            int day  = (dv >> 17) & 0x1F;
            int mon  = (dv >> 22) & 0x0F;
            int yr   = (dv >> 26) & 0xFFFFF;
            char dbuf[32];
            std::snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d", yr, mon, day);
            result += dbuf;
            continue;
        }
        case 'T': {
            int64_t dv = arg.as_int();
            int sec_  = (dv >>  0) & 0x3F;
            int min_  = (dv >>  6) & 0x3F;
            int hr    = (dv >> 12) & 0x1F;
            char tbuf[32];
            std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", hr, min_, sec_);
            result += tbuf;
            continue;
        }
        default:
            buf[0] = spec; buf[1] = '\0';
            break;
        }

        formatted = buf;
        if ((plus || space) && (spec == 'd' || spec == 'i' || spec == 'f' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G')) {
            if (!formatted.empty() && formatted[0] != '-') {
                formatted.insert(0, 1, plus ? '+' : ' ');
            }
        }
        applyWidth(formatted, width, left_align, zero_pad);
        result += formatted;
    }

    return result;
}

/**
 * @brief call stack या globals पर existing variable binding में val write करता है।
 *
 * Existing binding के लिए call stack top-down search करता है। अगर name नहीं मिला तो
 * current frame में (या stack empty होने पर globals में) नई binding create करता है।
 *
 * @param name Variable name।
 * @param val Store करने वाला value।
 */
void Interpreter::setVar(const std::string& name, Value val) {
    if (!call_stack_.empty()) [[likely]] {
        auto& top = call_stack_.back();
        if (auto kit = top.static_keys.find(name); kit != top.static_keys.end()) {
            static_vars_[kit->second] = val;
            return;
        }
        auto vit = top.vars.find(name);
        if (vit != top.vars.end()) [[likely]] {
            vit->second = val;
            return;
        }
        for (auto it = call_stack_.rbegin() + 1; it != call_stack_.rend(); ++it) {
            if (auto kit = it->static_keys.find(name); kit != it->static_keys.end()) {
                static_vars_[kit->second] = val;
                return;
            }
            auto vit2 = it->vars.find(name);
            if (vit2 != it->vars.end()) {
                vit2->second = val;
                return;
            }
        }
    }
    auto git = globals_.find(name);
    if (git != globals_.end()) {
        git->second = val;
        return;
    }
    if (!call_stack_.empty()) {
        call_stack_.back().vars[name] = val;
    } else {
        globals_[name] = val;
    }
}

/**
 * @brief Call stack top-down search करके फिर globals में variable lookup करता है।
 *
 * @param name Variable name।
 * @return Variable की value, या 0 अगर नहीं मिला।
 */
Value Interpreter::getVar(const std::string& name) {
    if (!call_stack_.empty()) [[likely]] {
        auto& top = call_stack_.back();
        if (auto kit = top.static_keys.find(name); kit != top.static_keys.end()) {
            auto sit = static_vars_.find(kit->second);
            if (sit != static_vars_.end()) return sit->second;
            return Value::make_int(0);
        }
        auto vit = top.vars.find(name);
        if (vit != top.vars.end()) [[likely]] return vit->second;
        for (auto it = call_stack_.rbegin() + 1; it != call_stack_.rend(); ++it) {
            if (auto kit = it->static_keys.find(name); kit != it->static_keys.end()) {
                auto sit = static_vars_.find(kit->second);
                if (sit != static_vars_.end()) return sit->second;
                return Value::make_int(0);
            }
            auto vit2 = it->vars.find(name);
            if (vit2 != it->vars.end()) return vit2->second;
        }
    }
    auto git = globals_.find(name);
    if (git != globals_.end()) return git->second;
    return Value::make_int(0);
}

/**
 * @brief In-place mutation के लिए variable के storage का mutable pointer लौटाता है।
 *
 * @param name Variable name।
 * @return Owning container में Value का pointer, या nullptr अगर नहीं मिला।
 */
Value* Interpreter::getVarPtr(const std::string& name) {
    if (!call_stack_.empty()) [[likely]] {
        auto& top = call_stack_.back();
        if (auto kit = top.static_keys.find(name); kit != top.static_keys.end()) {
            auto sit = static_vars_.find(kit->second);
            return (sit != static_vars_.end()) ? &sit->second : nullptr;
        }
        auto vit = top.vars.find(name);
        if (vit != top.vars.end()) [[likely]] return &vit->second;
        for (auto it = call_stack_.rbegin() + 1; it != call_stack_.rend(); ++it) {
            if (auto kit = it->static_keys.find(name); kit != it->static_keys.end()) {
                auto sit = static_vars_.find(kit->second);
                return (sit != static_vars_.end()) ? &sit->second : nullptr;
            }
            auto vit2 = it->vars.find(name);
            if (vit2 != it->vars.end()) return &vit2->second;
        }
    }
    auto git = globals_.find(name);
    if (git != globals_.end()) return &git->second;
    return nullptr;
}

/**
 * @brief Goto resolution के लिए labels_ populate करने को compound statement recursively scan करता है।
 *
 * हर label का parent CompoundStmt और उस compound में उसका index record करता है ताकि
 * execCompound सही position से execution resume कर सके।
 *
 * @param body Scan करने वाला compound statement।
 */
void Interpreter::scanLabels(CompoundStmt* body) {
    for (size_t i = 0; i < body->stmts.size(); ++i) {
        Stmt* s = body->stmts[i];
        switch (s->nk) {
        case NodeKind::LabelStmt:
            labels_[static_cast<LabelStmt*>(s)->name] = {body, i};
            break;
        case NodeKind::CompoundStmt:
            scanLabels(static_cast<CompoundStmt*>(s));
            break;
        case NodeKind::IfStmt: {
            auto* ifs = static_cast<IfStmt*>(s);
            if (ifs->then_body && ifs->then_body->nk == NodeKind::CompoundStmt)
                scanLabels(static_cast<CompoundStmt*>(ifs->then_body));
            if (ifs->else_body && ifs->else_body->nk == NodeKind::CompoundStmt)
                scanLabels(static_cast<CompoundStmt*>(ifs->else_body));
            break;
        }
        case NodeKind::ForStmt: {
            auto* fs = static_cast<ForStmt*>(s);
            if (fs->body && fs->body->nk == NodeKind::CompoundStmt)
                scanLabels(static_cast<CompoundStmt*>(fs->body));
            break;
        }
        case NodeKind::WhileStmt: {
            auto* ws = static_cast<WhileStmt*>(s);
            if (ws->body && ws->body->nk == NodeKind::CompoundStmt)
                scanLabels(static_cast<CompoundStmt*>(ws->body));
            break;
        }
        case NodeKind::DoWhileStmt: {
            auto* dw = static_cast<DoWhileStmt*>(s);
            if (dw->body && dw->body->nk == NodeKind::CompoundStmt)
                scanLabels(static_cast<CompoundStmt*>(dw->body));
            break;
        }
        default: break;
        }
    }
}

/**
 * @brief Label के बाद immediately आने वाले statement से execution resume करता है, उसके containing compound में।
 *
 * @param label Jump करने वाला label name।
 */
void Interpreter::execFromLabel(const std::string& label) {
    auto it = labels_.find(label);
    if (it == labels_.end()) return;
    CompoundStmt* parent = it->second.parent;
    for (size_t i = it->second.index; i < parent->stmts.size(); ++i) {
        execStmt(parent->stmts[i]);
        if (signal_ != Signal::None) return;
    }
}

/**
 * @brief Exe-block context में compound statement execute करता है और सारा Emit() output लौटाता है।
 *
 * Emit buffer clear करता है, temporary call frame push करता है, body run करता है, फिर frame pop करता है।
 *
 * @param body Execute करने वाला compound statement।
 * @return Execution के दौरान Emit() के through लिखा सारा text।
 */
std::string Interpreter::runExeBlock(CompoundStmt* body) {
    emit_buffer_.clear();
    in_exe_block_ = true;

    Frame frame;
    call_stack_.push_back(frame);

    execCompound(body);

    call_stack_.pop_back();
    signal_ = Signal::None;
    in_exe_block_ = false;

    std::string result = std::move(emit_buffer_);
    emit_buffer_.clear();
    return result;
}

} // namespace holyc
