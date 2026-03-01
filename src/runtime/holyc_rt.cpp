#include "holyc_rt.h"

#include <cmath>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cinttypes>
#include <ctime>
#include <vector>
#include <malloc.h>  // malloc_usable_size (Linux) — allocated block का usable size जानने के लिए
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static constexpr size_t SPRINT_BUF_MAX = 65536;

/**
 * @brief fp पर binary value optional width और alignment flags के साथ print करता है।
 *
 * @param fp        Output file stream।
 * @param val       Binary में print करने वाली value।
 * @param width     Minimum field width।
 * @param left      True होने पर left-justify।
 * @param zero_pad  True होने पर spaces की जगह zeros से pad करो।
 * @param upper     Reserved; unused।
 * @return Total characters written।
 */
static int print_binary_fp(FILE* fp, uint64_t val, int width, bool left, bool zero_pad, bool upper) {
    (void)upper;
    char buf[65];
    int len = 0;
    if (val == 0) {
        buf[0] = '0';
        len = 1;
    } else {
        uint64_t v = val;
        int pos = 64;
        while (v) {
            buf[--pos] = '0' + (v & 1);
            v >>= 1;
        }
        len = 64 - pos;
        memmove(buf, buf + pos, len);
    }
    buf[len] = '\0';

    int printed = 0;
    int pad = (width > len) ? width - len : 0;
    char padch = zero_pad ? '0' : ' ';

    if (!left) {
        for (int i = 0; i < pad; i++) { fputc(padch, fp); printed++; }
    }
    fputs(buf, fp);
    printed += len;
    if (left) {
        for (int i = 0; i < pad; i++) { fputc(' ', fp); printed++; }
    }
    return printed;
}

/**
 * @brief Core HolyC print engine — %h repeat, %b binary, %n engineering, %z enum, %D/%T date/time handle करता है।
 *
 * @param fp  Output file stream।
 * @param fmt HolyC format string।
 * @param ap  Variadic argument list।
 * @return fp पर total characters written।
 */
static int __holyc_fprint_va(FILE* fp, const char* fmt, va_list ap) {
    if (!fmt) return 0;

    int total = 0;
    const char* p = fmt;

    while (*p) {
        if (*p != '%') {
            fputc(*p, fp);
            total++;
            p++;
            continue;
        }
        p++; // '%' skip करो
        if (*p == '\0') break;
        if (*p == '%') { fputc('%', fp); total++; p++; continue; }

        // %h — HolyC repeat modifier (जैसे %h5c char 5 बार repeat करता है)
        bool h_modifier = false;
        bool h_star = false;
        int h_count = 0;
        if (*p == 'h') {
            h_modifier = true;
            p++;
            if (*p == '*') { h_star = true; p++; }
            else {
                while (*p >= '0' && *p <= '9') {
                    if (h_count > 1000) h_count = 1000;
                    h_count = h_count * 10 + (*p++ - '0');
                }
                if (h_count > 10000) h_count = 10000;
            }
        }

        bool left = false, zero_pad = false, plus = false, space = false, hash = false;
        for (;;) {
            if (*p == '-') { left = true; p++; }
            else if (*p == '0') { zero_pad = true; p++; }
            else if (*p == '+') { plus = true; p++; }
            else if (*p == ' ') { space = true; p++; }
            else if (*p == '#') { hash = true; p++; }
            else break;
        }
        (void)plus; (void)space; (void)hash;

        int width = 0;
        bool has_width = false;
        if (*p == '*') {
            width = (int)va_arg(ap, int64_t);
            has_width = true;
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                if (width > 1000) width = 1000;
                width = width * 10 + (*p - '0');
                has_width = true;
                p++;
            }
        }
        if (!has_width) width = 0;

        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                prec = (int)va_arg(ap, int64_t);
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    if (prec > 100) prec = 100;
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
        }

        while (*p == 'l' || *p == 'j' || *p == 't')
            p++;

        if (*p == '\0') break;

        if (width > 10000) width = 10000;
        if (prec >= 0 && prec > 20) prec = 20;

        char spec = *p++;

        if (h_modifier && h_star) {
            int64_t raw = va_arg(ap, int64_t);
            h_count = (raw < 0) ? 0 : (raw > 10000) ? 10000 : (int)raw;
        }

        switch (spec) {
        case 'd': case 'i': {
            int64_t val = va_arg(ap, int64_t);
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "%" PRId64, val);
            int pad = (width > len) ? width - len : 0;
            char padch = zero_pad ? '0' : ' ';
            if (!left) for (int i = 0; i < pad; i++) { fputc(padch, fp); total++; }
            fputs(buf, fp); total += len;
            if (left) for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
            break;
        }
        case 'u': {
            uint64_t val = va_arg(ap, uint64_t);
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "%" PRIu64, val);
            int pad = (width > len) ? width - len : 0;
            char padch = zero_pad ? '0' : ' ';
            if (!left) for (int i = 0; i < pad; i++) { fputc(padch, fp); total++; }
            fputs(buf, fp); total += len;
            if (left) for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
            break;
        }
        case 'x': case 'X': {
            uint64_t val = va_arg(ap, uint64_t);
            char buf[32];
            const char* f = (spec == 'x') ? "%" PRIx64 : "%" PRIX64;
            int len = snprintf(buf, sizeof(buf), f, val);
            int pad = (width > len) ? width - len : 0;
            char padch = zero_pad ? '0' : ' ';
            if (!left) for (int i = 0; i < pad; i++) { fputc(padch, fp); total++; }
            fputs(buf, fp); total += len;
            if (left) for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
            break;
        }
        case 'b': case 'B': {
            uint64_t val = va_arg(ap, uint64_t);
            total += print_binary_fp(fp, val, width, left, zero_pad, spec == 'B');
            break;
        }
        case 'f': {
            double val = va_arg(ap, double);
            char fmt_buf[32];
            if (prec >= 0)
                snprintf(fmt_buf, sizeof(fmt_buf), "%%%s%s%d.%df",
                         left ? "-" : "", zero_pad ? "0" : "", width, prec);
            else
                snprintf(fmt_buf, sizeof(fmt_buf), "%%%s%s%df",
                         left ? "-" : "", zero_pad ? "0" : "", width);
            total += fprintf(fp, fmt_buf, val);
            break;
        }
        case 'e': case 'E': {
            double val = va_arg(ap, double);
            char fmt_buf[32];
            if (prec >= 0)
                snprintf(fmt_buf, sizeof(fmt_buf), "%%%s%d.%d%c",
                         left ? "-" : "", width, prec, spec);
            else
                snprintf(fmt_buf, sizeof(fmt_buf), "%%%s%d%c",
                         left ? "-" : "", width, spec);
            total += fprintf(fp, fmt_buf, val);
            break;
        }
        case 'g': {
            double val = va_arg(ap, double);
            char fmt_buf[32];
            if (prec >= 0)
                snprintf(fmt_buf, sizeof(fmt_buf), "%%%s%d.%dg",
                         left ? "-" : "", width, prec);
            else
                snprintf(fmt_buf, sizeof(fmt_buf), "%%%s%dg",
                         left ? "-" : "", width);
            total += fprintf(fp, fmt_buf, val);
            break;
        }
        case 'n': {
            // Engineering notation: 1000s से scale करो और SI suffix append करो।
            double val = va_arg(ap, double);
            double absval = fabs(val);
            static const char* pos_sfx[] = {"", "k", "M", "G", "T", "P"};
            static const char* neg_sfx[] = {"", "m", "u", "n", "p", "f"};
            int idx = 0;
            const char* sfx;
            if (absval >= 1.0) {
                while (absval >= 1000.0 && idx < 5) { absval /= 1000.0; idx++; }
                if (val < 0) absval = -absval;
                sfx = pos_sfx[idx];
            } else if (absval > 0.0) {
                while (absval < 1.0 && idx < 5) { absval *= 1000.0; idx++; }
                if (val < 0) absval = -absval;
                sfx = neg_sfx[idx];
            } else {
                sfx = pos_sfx[0];
            }
            total += fprintf(fp, "%g%s", absval, sfx);
            break;
        }
        case 'z': {
            // %z idx list — null-separated string list से idx-th entry print करो।
            int64_t idx = va_arg(ap, int64_t);
            const char* list = va_arg(ap, const char*);
            if (idx < 0) idx = 0;
            if (list) {
                int64_t iters = 0;
                for (int64_t cur = 0; cur < idx && iters < 1000; cur++, iters++) {
                    while (*list) list++;
                    list++;
                    if (*list == '\0') break;
                }
                int len = (int)strlen(list);
                if (prec >= 0 && prec < len) len = prec;
                int pad = (width > len) ? width - len : 0;
                if (!left) for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
                fwrite(list, 1, len, fp); total += len;
                if (left)  for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
            }
            break;
        }
        case 'c': {
            int64_t val = va_arg(ap, int64_t);
            if (h_modifier) {
                int count = h_count > 0 ? h_count : width;
                for (int k = 0; k < count; k++) { fputc((int)(val & 0xFF), fp); total++; }
            } else {
                fputc((int)(val & 0xFF), fp);
                total++;
            }
            break;
        }
        case 's': {
            const char* val = va_arg(ap, const char*);
            if (!val) val = "(null)";
            int len = (int)strlen(val);
            if (prec >= 0 && prec < len) len = prec;
            int pad = (width > len) ? width - len : 0;
            if (!left) for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
            fwrite(val, 1, len, fp); total += len;
            if (left) for (int i = 0; i < pad; i++) { fputc(' ', fp); total++; }
            break;
        }
        case 'p': {
            void* val = va_arg(ap, void*);
            total += fprintf(fp, "%p", val);
            break;
        }
        case 'D': {
            // %D — packed date integer (Terry Davis encoding) — date को integer में pack करता है
            int64_t dv = va_arg(ap, int64_t);
            int day  = (dv >> 17) & 0x1F;
            int mon  = (dv >> 22) & 0x0F;
            int yr   = (int)((dv >> 26) & 0xFFFFF);
            total += fprintf(fp, "%04d-%02d-%02d", yr, mon, day);
            break;
        }
        case 'T': {
            // %T — packed time integer (Terry Davis encoding) — time को integer में pack करता है
            int64_t dv = va_arg(ap, int64_t);
            int sec_  = (dv >>  0) & 0x3F;
            int min_  = (dv >>  6) & 0x3F;
            int hr    = (dv >> 12) & 0x1F;
            total += fprintf(fp, "%02d:%02d:%02d", hr, min_, sec_);
            break;
        }
        default:
            fputc('%', fp);
            fputc(spec, fp);
            total += 2;
            break;
        }
    }

    fflush(fp);
    return total;
}

/**
 * @brief HolyC format engine use करके stdout पर print करता है।
 *
 * @param fmt HolyC format string।
 * @return Written characters की संख्या।
 */
extern "C" int __holyc_print(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __holyc_fprint_va(stdout, fmt, ap);
    va_end(ap);
    return r;
}

/**
 * @brief Formatted string allocate करके लौटाता है (caller को Free() करना होगा)।
 *
 * @param fmt HolyC format string।
 * @return Heap-allocated formatted string, या failure पर nullptr।
 */
extern "C" char* MStrPrint(const char* fmt, ...) {
    char* buf = nullptr;
    size_t sz = 0;
    FILE* f = open_memstream(&buf, &sz);
    if (!f) return nullptr;
    va_list ap;
    va_start(ap, fmt);
    __holyc_fprint_va(f, fmt, ap);
    va_end(ap);
    fclose(f);
    return buf;
}

/**
 * @brief MStrPrint का alias; formatted string allocate करके लौटाता है (caller को Free() करना होगा)।
 *
 * @param fmt HolyC format string।
 * @return Heap-allocated formatted string, या failure पर nullptr।
 */
extern "C" char* StrPrintf(const char* fmt, ...) {
    char* buf = nullptr;
    size_t sz = 0;
    FILE* f = open_memstream(&buf, &sz);
    if (!f) return nullptr;
    va_list ap;
    va_start(ap, fmt);
    __holyc_fprint_va(f, fmt, ap);
    va_end(ap);
    fclose(f);
    return buf;
}

/**
 * @brief dst पर HolyC-formatted string append करता है (SPRINT_BUF_MAX तक capped); new length लौटाता है।
 *
 * @param dst Destination buffer।
 * @param fmt HolyC format string।
 * @return dst की new total length।
 */
extern "C" int64_t CatPrint(char* dst, const char* fmt, ...) {
    char* buf = nullptr;
    size_t sz = 0;
    FILE* f = open_memstream(&buf, &sz);
    if (!f) return 0;
    va_list ap;
    va_start(ap, fmt);
    __holyc_fprint_va(f, fmt, ap);
    va_end(ap);
    fclose(f);
    if (buf && dst) {
        size_t dst_len = strlen(dst);
        if (dst_len < SPRINT_BUF_MAX - 1) {
            size_t space = SPRINT_BUF_MAX - 1 - dst_len;
            strncat(dst, buf, space);
        }
        free(buf);
    } else if (buf) {
        free(buf);
    }
    return (int64_t)(dst ? strlen(dst) : 0);
}

/**
 * @brief Binary method से integer exponentiation; negative exp पर 0 लौटाता है।
 *
 * @param base Base value।
 * @param exp  Exponent; negative values 0 yield करते हैं।
 * @return base को power exp तक raise किया हुआ।
 */
extern "C" int64_t __holyc_ipow(int64_t base, int64_t exp) {
    if (exp < 0) return 0;
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

extern "C" double __holyc_sqrt(double x)      { return sqrt(x); }
extern "C" double __holyc_abs_f64(double x)    { return fabs(x); }
extern "C" int64_t __holyc_abs_i64(int64_t x)  { return x < 0 ? -x : x; }
extern "C" double __holyc_sin(double x)        { return sin(x); }
extern "C" double __holyc_cos(double x)        { return cos(x); }
extern "C" double __holyc_tan(double x)        { return tan(x); }
extern "C" double __holyc_atan(double x)       { return atan(x); }
extern "C" double __holyc_atan2(double y, double x) { return atan2(y, x); }
extern "C" double __holyc_exp(double x)        { return exp(x); }
extern "C" double __holyc_log(double x)        { return log(x); }
extern "C" double __holyc_log2(double x)       { return log2(x); }
extern "C" double __holyc_log10(double x)      { return log10(x); }
extern "C" double __holyc_pow(double b, double e) { return pow(b, e); }
extern "C" double __holyc_ceil(double x)       { return ceil(x); }
extern "C" double __holyc_floor(double x)      { return floor(x); }
extern "C" double __holyc_round(double x)      { return round(x); }

extern "C" int64_t __holyc_clamp(int64_t val, int64_t lo, int64_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

extern "C" int64_t __holyc_min(int64_t a, int64_t b) { return a < b ? a : b; }
extern "C" int64_t __holyc_max(int64_t a, int64_t b) { return a > b ? a : b; }
extern "C" int64_t __holyc_sign(int64_t x) { return (x > 0) - (x < 0); }

/**
 * @brief दो rand() calls से बना 64-bit random value लौटाता है।
 *
 * @return Random uint64_t value।
 */
extern "C" uint64_t RandU64(void) {
    return ((uint64_t)(unsigned)rand() << 32) | (uint64_t)(unsigned)rand();
}

extern "C" void SeedRand(int64_t seed) {
    srand((unsigned)seed);
}

extern "C" int64_t ToI64(double x)  { return (int64_t)x; }
extern "C" double  ToF64(int64_t x) { return (double)x;  }

extern "C" __attribute__((weak)) double ACos(double x)          { return acos(x); }
extern "C" __attribute__((weak)) double ASin(double x)          { return asin(x); }
extern "C" __attribute__((weak)) double Sinh(double x)          { return sinh(x); }
extern "C" __attribute__((weak)) double Cosh(double x)          { return cosh(x); }
extern "C" __attribute__((weak)) double Tanh(double x)          { return tanh(x); }
extern "C" __attribute__((weak)) double Fmod(double x, double y){ return fmod(x, y); }
extern "C" __attribute__((weak)) double FMod(double x, double y){ return fmod(x, y); }
extern "C" __attribute__((weak)) double Cbrt(double x)          { return cbrt(x); }
extern "C" __attribute__((weak)) double Trunc(double x)         { return trunc(x); }
extern "C" __attribute__((weak)) void   Abort(void)             { abort(); }

static constexpr int64_t kMaxAlloc = 1LL << 30;  // Integer overflow से बचाने के लिए 1 GB hard cap

/**
 * @brief n bytes allocate करता है; non-positive या oversized requests पर nullptr लौटाता है।
 *
 * @param n Allocate करने वाले bytes की संख्या।
 * @return Allocated memory का pointer, या failure पर nullptr।
 */
extern "C" void* MAlloc(int64_t n) {
    if (n <= 0 || n > kMaxAlloc) return nullptr;
    return malloc((size_t)n);
}
extern "C" void* CAlloc(int64_t n) {
    if (n <= 0 || n > kMaxAlloc) return nullptr;
    return calloc(1, (size_t)n);
}
extern "C" void  Free(void* p)      { free(p); }
extern "C" void* MemCpy(void* dst, const void* src, int64_t n) {
    if (n <= 0) return dst;
    return memcpy(dst, src, (size_t)n);
}
extern "C" void* MemSet(void* dst, int64_t val, int64_t n) {
    if (n <= 0) return dst;
    return memset(dst, (int)val, (size_t)n);
}
extern "C" int64_t MemCmp(const void* a, const void* b, int64_t n) { return memcmp(a, b, (size_t)n); }

/**
 * @brief ptr को new_size तक resize करता है; new_size [1, 1 GB] range से बाहर हो तो nullptr लौटाता है।
 *
 * @param ptr      Existing allocation (nullptr हो सकता है)।
 * @param new_size Requested new size bytes में।
 * @return Resized block का pointer, या invalid size पर nullptr।
 */
extern "C" void* ReAlloc(void* ptr, int64_t new_size) {
    if (new_size <= 0 || new_size > (1LL << 30)) return nullptr;
    return realloc(ptr, (size_t)new_size);
}

/**
 * @brief malloc_usable_size से ptr का usable allocation size लौटाता है।
 *
 * @param ptr Live allocation का pointer।
 * @return Usable byte count, या ptr nullptr हो तो 0।
 */
extern "C" int64_t MSize(void* ptr) {
    if (!ptr) return 0;
    return (int64_t)malloc_usable_size(ptr);
}

/**
 * @brief Same usable size के साथ src की byte-for-byte copy allocate करके लौटाता है।
 *
 * @param src Duplicate करने वाला source allocation।
 * @return New copy का pointer, या src nullptr हो तो nullptr।
 */
extern "C" void* MAllocIdent(void* src) {
    if (!src) return nullptr;
    size_t sz = malloc_usable_size(src);
    if (sz == 0) sz = 8;
    void* dst = malloc(sz);
    if (dst) memcpy(dst, src, sz);
    return dst;
}

// User HolyC stdlib इन्हें override कर सके इसलिए weak linkage।
extern "C" __attribute__((weak)) int64_t StrLen(const char* s) { return s ? (int64_t)strlen(s) : 0; }
extern "C" char* StrCpy(char* dst, const char* src)         { return strcpy(dst, src); }
extern "C" __attribute__((weak)) int64_t StrCmp(const char* a, const char* b) { return strcmp(a, b); }
extern "C" int64_t StrNCmp(const char* a, const char* b, int64_t n) { return strncmp(a, b, (size_t)n); }
extern "C" __attribute__((weak)) char* StrFind(const char* needle, const char* haystack) {
    if (!needle || !haystack) return nullptr;
    return const_cast<char*>(strstr(haystack, needle));
}
extern "C" char* StrCat(char* dst, const char* src) { return strcat(dst, src); }

extern "C" char* StrCpyN(char* dst, const char* src, int64_t n) {
    if (!dst || !src || n <= 0) return dst;
    if (n > (1LL << 26)) n = 1LL << 26;  // 64 MB cap — बहुत बड़े buffer से बचाने के लिए
    strncpy(dst, src, (size_t)n);
    dst[n - 1] = '\0';
    return dst;
}

extern "C" char* StrCatN(char* dst, const char* src, int64_t n) {
    if (!dst || !src || n <= 1) return dst;
    if (n > (1LL << 26)) n = 1LL << 26;  // 64 MB cap — बहुत बड़े buffer से बचाने के लिए
    size_t dst_len = strlen(dst);
    if (dst_len >= (size_t)n - 1) return dst;
    strncat(dst, src, (size_t)n - dst_len - 1);
    return dst;
}

extern "C" int64_t StrICmp(const char* a, const char* b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    return (int64_t)strcasecmp(a, b);
}
extern "C" int64_t Str2I64(const char* s) { return s ? (int64_t)atoll(s) : 0; }
extern "C" double  Str2F64(const char* s) { return s ? atof(s) : 0.0; }

/**
 * @brief * और ? wildcards के साथ glob pattern matching।
 *
 * @param pattern Glob pattern string।
 * @param str     Pattern के against test करने वाला string।
 * @return str pattern match करे तो 1, नहीं तो 0।
 */
extern "C" int64_t WildMatch(const char* pattern, const char* str) {
    if (!pattern || !str) return 0;
    const char* p = pattern;
    const char* s = str;
    const char* star_p = nullptr;
    const char* star_s = nullptr;

    while (*s) {
        if (*p == '*') {
            star_p = p++;
            star_s = s;
        } else if (*p == '?' || *p == *s) {
            p++; s++;
        } else if (star_p) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return 0;
        }
    }
    while (*p == '*') p++;
    return *p == '\0' ? 1 : 0;
}

extern "C" int64_t StrMatch(const char* pattern, const char* str) {
    return WildMatch(pattern, str);
}

extern "C" __attribute__((weak)) int64_t StrOcc(const char* s, int64_t ch) {
    if (!s) return 0;
    int64_t count = 0;
    while (*s) if ((unsigned char)*s++ == (unsigned char)ch) count++;
    return count;
}

extern "C" __attribute__((weak)) char* StrFirst(const char* s, int64_t ch) {
    if (!s) return nullptr;
    return const_cast<char*>(strchr(s, (int)ch));
}

extern "C" __attribute__((weak)) char* StrLast(const char* s, int64_t ch) {
    if (!s) return nullptr;
    return const_cast<char*>(strrchr(s, (int)ch));
}

/**
 * @brief Bsf — bit scan forward; lowest set bit का bit index लौटाता है, या -1।
 *
 * @param x Input value।
 * @return Lowest set bit का 0-based index, या x == 0 हो तो -1।
 */
extern "C" int64_t Bsf(int64_t x) {
    if (x == 0) return -1;
    return __builtin_ctzll((uint64_t)x);
}

/**
 * @brief Bsr — bit scan reverse; highest set bit का bit index लौटाता है, या -1।
 *
 * @param x Input value।
 * @return Highest set bit का 0-based index, या x == 0 हो तो -1।
 */
extern "C" int64_t Bsr(int64_t x) {
    if (x == 0) return -1;
    return 63 - __builtin_clzll((uint64_t)x);
}

extern "C" int64_t BCnt(int64_t x) {
    return __builtin_popcountll((uint64_t)x);
}

extern "C" int64_t Bt(int64_t val, int64_t bit) {
    if (bit < 0 || bit >= 64) return 0;
    return ((uint64_t)val >> bit) & 1;
}

extern "C" int64_t Bts(int64_t* val, int64_t bit) {
    if (!val || bit < 0 || bit >= 64) return 0;
    int64_t r = ((uint64_t)*val >> bit) & 1;
    *val |= (int64_t)(1ULL << bit);
    return r;
}

extern "C" int64_t Btr(int64_t* val, int64_t bit) {
    if (!val || bit < 0 || bit >= 64) return 0;
    int64_t r = ((uint64_t)*val >> bit) & 1;
    *val &= (int64_t)~(1ULL << bit);
    return r;
}

extern "C" int64_t Btc(int64_t* val, int64_t bit) {
    if (!val || bit < 0 || bit >= 64) return 0;
    int64_t r = ((uint64_t)*val >> bit) & 1;
    *val ^= (int64_t)(1ULL << bit);
    return r;
}

extern "C" int64_t BFieldExtU32(int64_t val, int64_t bit, int64_t count) {
    if (bit < 0 || bit >= 64 || count <= 0 || count > 63) return 0;
    return ((uint64_t)val >> bit) & ((1ULL << count) - 1);
}

extern "C" int64_t ToUpper(int64_t ch) { return toupper((int)ch); }
extern "C" int64_t ToLower(int64_t ch) { return tolower((int)ch); }
extern "C" int64_t IsAlpha(int64_t ch)    { return isalpha((int)ch) ? 1 : 0; }
extern "C" int64_t IsDigit(int64_t ch)    { return isdigit((int)ch) ? 1 : 0; }
extern "C" int64_t IsAlphaNum(int64_t ch) { return isalnum((int)ch) ? 1 : 0; }
extern "C" int64_t IsUpper(int64_t ch)    { return isupper((int)ch) ? 1 : 0; }
extern "C" int64_t IsLower(int64_t ch)    { return islower((int)ch) ? 1 : 0; }

extern "C" __attribute__((weak)) int64_t IsSpace(int64_t ch)  { return isspace((int)ch)  ? 1 : 0; }
extern "C" __attribute__((weak)) int64_t IsPunct(int64_t ch)  { return ispunct((int)ch)  ? 1 : 0; }
extern "C" __attribute__((weak)) int64_t IsCtrl(int64_t ch)   { return iscntrl((int)ch)  ? 1 : 0; }
extern "C" __attribute__((weak)) int64_t IsXDigit(int64_t ch) { return isxdigit((int)ch) ? 1 : 0; }
extern "C" __attribute__((weak)) int64_t IsGraph(int64_t ch)  { return isgraph((int)ch)  ? 1 : 0; }
extern "C" __attribute__((weak)) int64_t IsPrint(int64_t ch)  { return isprint((int)ch)  ? 1 : 0; }

/**
 * @brief PutChars — single I64 value में packed up to 8 bytes write करता है।
 *
 * Bytes low-to-high extract होते हैं; पहले zero byte पर writing रुकती है।
 *
 * @param ch 64-bit integer में packed up to 8 ASCII bytes (low byte first)।
 */
extern "C" void PutChars(int64_t ch) {
    for (int i = 0; i < 8; i++) {
        char c = (char)((ch >> (i * 8)) & 0xFF);
        if (c == 0) break;
        fputc(c, stdout);
    }
    fflush(stdout);
}

/**
 * @brief stdin से एक character read करता है; EOF पर -1 लौटाता है।
 *
 * @return Character value (0-255), या EOF पर -1।
 */
extern "C" int64_t GetChar(void) {
    int c = fgetc(stdin);
    return (c == EOF) ? -1 : (int64_t)c;
}

/**
 * @brief stdin से buf में line read करता है; trailing newline strip करता है; character count लौटाता है।
 *
 * @param buf Line receive करने वाला buffer।
 * @param max Null terminator सहित maximum buffer size।
 * @return buf में placed characters की संख्या।
 */
extern "C" int64_t GetStr(char* buf, int64_t max) {
    if (!buf || max <= 0) return 0;
    if (!fgets(buf, (int)max, stdin)) {
        buf[0] = '\0';
        return 0;
    }
    int64_t len = (int64_t)strlen(buf);
    if (len > 0 && buf[len-1] == '\n') {
        buf[--len] = '\0';
    }
    return len;
}

/**
 * @brief Calling thread को ms milliseconds sleep कराता है।
 *
 * @param ms Milliseconds sleep करने के लिए।
 */
extern "C" void Sleep(int64_t ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

/**
 * @brief CLOCK_MONOTONIC time milliseconds में लौटाता है।
 *
 * @return Current monotonic time milliseconds में।
 */
extern "C" int64_t GetTicks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

/**
 * @brief Now — packed date/time integer लौटाता है (Terry Davis encoding)।
 *
 * Bits 0-5: seconds (सेकंड), 6-11: minutes (मिनट), 12-16: hours (घंटे), 17-21: day (दिन),
 * 22-25: month, 26+: year।
 *
 * @return Packed local date/time integer।
 */
extern "C" int64_t Now(void) {
    time_t t = time(nullptr);
    struct tm tm_val;
    localtime_r(&t, &tm_val);
    return ((int64_t)tm_val.tm_sec)
         | ((int64_t)tm_val.tm_min  <<  6)
         | ((int64_t)tm_val.tm_hour << 12)
         | ((int64_t)tm_val.tm_mday << 17)
         | ((int64_t)(tm_val.tm_mon+1) << 22)
         | ((int64_t)(tm_val.tm_year+1900) << 26);
}
extern "C" int64_t GetTickCount(void) { return GetTicks(); }

extern "C" void SysDbg(void) {
    __builtin_trap();
}

#define HC_O_READ   1
#define HC_O_WRITE  2
#define HC_O_APPEND 4
#define HC_O_CREATE 8

/**
 * @brief File open करता है; opaque FILE* को int64_t में cast करके लौटाता है, या error पर -1।
 *
 * @param path Open करने वाला file path।
 * @param mode HC_O_* flags का bitmask।
 * @return Opaque file descriptor, या error पर -1।
 */
extern "C" int64_t FileOpen(const char* path, int64_t mode) {
    if (!path) return -1;
    char flags[8];
    if (mode & HC_O_APPEND)
        snprintf(flags, sizeof(flags), "a%s", (mode & HC_O_READ) ? "+" : "");
    else if ((mode & HC_O_WRITE) || (mode & HC_O_CREATE))
        snprintf(flags, sizeof(flags), "w%s", (mode & HC_O_READ) ? "+" : "");
    else
        snprintf(flags, sizeof(flags), "r");
    FILE* f = fopen(path, flags);
    if (!f) return -1;
    return (int64_t)(intptr_t)f;
}

extern "C" void FileClose(int64_t fd) {
    if (fd > 0) fclose((FILE*)(intptr_t)fd);
}

extern "C" int64_t FileRead(int64_t fd, void* buf, int64_t count) {
    if (fd <= 0 || !buf || count <= 0) return 0;
    return (int64_t)fread(buf, 1, (size_t)count, (FILE*)(intptr_t)fd);
}

extern "C" int64_t FileWrite(int64_t fd, const void* buf, int64_t count) {
    if (fd <= 0 || !buf || count <= 0) return 0;
    return (int64_t)fwrite(buf, 1, (size_t)count, (FILE*)(intptr_t)fd);
}

extern "C" int64_t FileSize(int64_t fd) {
    if (fd <= 0) return -1;
    FILE* f = (FILE*)(intptr_t)fd;
    off_t pos = ftello(f);
    fseeko(f, 0, SEEK_END);
    off_t size = ftello(f);
    fseeko(f, pos, SEEK_SET);
    return (int64_t)size;
}

extern "C" int64_t FileSeek(int64_t fd, int64_t offset) {
    if (fd <= 0) return -1;
    return (int64_t)fseek((FILE*)(intptr_t)fd, (long)offset, SEEK_SET);
}

extern "C" int64_t FileExists(const char* path) {
    if (!path) return 0;
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

/**
 * @brief पूरी file malloc'd buffer में read करता है; 256 MB से बड़ी files reject करता है।
 *
 * @param path     Read करने वाला file path।
 * @param size_out Non-null हो तो read bytes की संख्या receive करता है।
 * @return Heap-allocated null-terminated buffer, या failure पर nullptr।
 */
extern "C" void* FileReadAll(const char* path, int64_t* size_out) {
    if (!path) return nullptr;
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); return nullptr; }
    int64_t sz = (int64_t)ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return nullptr; }
    static constexpr int64_t kMaxFileRead = 256LL * 1024 * 1024;
    if (sz > kMaxFileRead) { fclose(f); return nullptr; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (size_out) *size_out = (int64_t)rd;
    return buf;
}

extern "C" void FileWriteAll(const char* path, const void* data, int64_t size) {
    if (!path || !data || size <= 0) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, (size_t)size, f);
    fclose(f);
}

extern "C" int64_t FileDel(const char* path) {
    if (!path) return -1;
    return (int64_t)remove(path);
}

// Exception support setjmp/longjmp use करता है। हर try{} block एक jmp_buf push करता है;
// throw उसे pop करके longjmp में जाता है। TLS इसे per-thread बनाता है।
static thread_local std::vector<jmp_buf*> __try_stack;
static thread_local int64_t __holyc_except_code_tl = 0;

/**
 * @brief per-thread exception stack पर jmp_buf pointer push करता है।
 *
 * @param buf Caller द्वारा allocated jmp_buf का pointer।
 */
extern "C" void __holyc_try_push(void* buf) {
    __try_stack.push_back(reinterpret_cast<jmp_buf*>(buf));
}

/**
 * @brief per-thread exception stack से innermost jmp_buf pop करता है।
 */
extern "C" void __holyc_try_pop(void) {
    if (!__try_stack.empty())
        __try_stack.pop_back();
}

/**
 * @brief सबसे recent throw द्वारा stored exception code लौटाता है।
 *
 * @return Last __holyc_throw() call का exception code।
 */
extern "C" int64_t __holyc_except_code(void) {
    return __holyc_except_code_tl;
}

/**
 * @brief Nearest try block पर longjmp करके HolyC exception throw करता है।
 *
 * कोई try block active नहीं हो तो process abort करता है।
 *
 * @param code Exception code जो __holyc_except_code() से available होगा।
 */
extern "C" void __holyc_throw(int64_t code) {
    if (__try_stack.empty()) {
        fprintf(stderr, "Unhandled HolyC exception: %ld\n", (long)code);
        abort();
    }
    __holyc_except_code_tl = code;
    jmp_buf* jb = __try_stack.back();
    __try_stack.pop_back();
    longjmp(*jb, 1);
}

// Weak ताकि linker runtime या interpreter definition दोनों में से pick कर सके।
__attribute__((weak)) int    g_argc = 0;
__attribute__((weak)) char** g_argv = nullptr;

extern "C" __attribute__((weak)) char** ArgV(void) { return g_argv; }
extern "C" __attribute__((weak)) int64_t ArgC(void) { return (int64_t)g_argc; }

extern "C" __attribute__((weak)) char* StrNew(const char* s) {
    if (!s) return nullptr;
    size_t len = strlen(s);
    char* p = (char*)malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

extern "C" __attribute__((weak)) char* StrDup(const char* s) {
    return StrNew(s);
}

extern "C" __attribute__((weak)) char* StrUpr(char* s) {
    if (!s) return nullptr;
    for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return s;
}

extern "C" __attribute__((weak)) char* StrLwr(char* s) {
    if (!s) return nullptr;
    for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return s;
}

extern "C" __attribute__((weak)) int64_t SPrint(char* buf, const char* fmt, ...) {
    if (!buf || !fmt) return 0;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, SPRINT_BUF_MAX, fmt, ap);
    va_end(ap);
    return (int64_t)(r >= 0 ? strlen(buf) : 0);
}

extern "C" __attribute__((weak)) int64_t StrNLen(const char* s, int64_t max_len) {
    if (!s) return 0;
    return (int64_t)strnlen(s, (size_t)max_len);
}

extern "C" __attribute__((weak)) int64_t RandI64(void) {
    return (int64_t)(((uint64_t)(unsigned)rand() << 32) | (uint64_t)(unsigned)rand());
}

extern "C" __attribute__((weak)) int64_t DirExists(const char* path) {
    if (!path) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

extern "C" __attribute__((weak)) int64_t DirMk(const char* path) {
    if (!path) return -1;
    return (int64_t)mkdir(path, 0755);
}

extern "C" __attribute__((weak)) int64_t FileRename(const char* src, const char* dst) {
    if (!src || !dst) return -1;
    return (int64_t)rename(src, dst);
}

extern "C" __attribute__((weak)) void* MemMove(void* dst, const void* src, int64_t n) {
    if (!dst || !src || n <= 0) return dst;
    return memmove(dst, src, (size_t)n);
}

// HolyC user-defined vararg TLS buffer।
// HolyC vararg functions के call sites call से पहले extra args यहाँ store करते हैं;
// __vararg_count()/__vararg_get(i) callee के अंदर से इन्हें read करते हैं।
#define HOLYC_VA_MAX 64
static thread_local int64_t holyc_va_buf[HOLYC_VA_MAX];
static thread_local int64_t holyc_va_n = 0;

/**
 * @brief Next user vararg call के लिए per-thread vararg count set करता है।
 *
 * @param n Follow होने वाले vararg values की संख्या।
 */
extern "C" void __holyc_va_set_count(int64_t n) {
    holyc_va_n = n;
}

/**
 * @brief per-thread vararg buffer में idx पर val store करता है (max 64 slots)।
 *
 * @param idx Slot index (0-63)।
 * @param val Store करने वाली value।
 */
extern "C" void __holyc_va_store(int64_t idx, int64_t val) {
    if (idx >= 0 && idx < HOLYC_VA_MAX)
        holyc_va_buf[idx] = val;
}

/**
 * @brief per-thread buffer में varargs की संख्या लौटाता है।
 *
 * @return Most recent __holyc_va_set_count() call द्वारा set किया गया count।
 */
extern "C" int64_t __holyc_vararg_count(void) {
    return holyc_va_n;
}

/**
 * @brief per-thread buffer से idx-th vararg लौटाता है; range से बाहर हो तो 0।
 *
 * @param idx Vararg buffer में index।
 * @return Stored value, या idx out of range हो तो 0।
 */
extern "C" int64_t __holyc_vararg_get(int64_t idx) {
    if (idx < 0 || idx >= holyc_va_n) return 0;
    return holyc_va_buf[idx];
}
