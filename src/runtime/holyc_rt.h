#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief HolyC format codes (%h, %b, %n, %z, %D, %T) use करके stdout पर print करो।
 *
 * @param fmt HolyC format string।
 * @return लिखे गए characters की संख्या।
 */
int __holyc_print(const char* fmt, ...);

/**
 * @brief HolyC format codes के through formatted string allocate करो; caller Free() करे।
 *
 * @param fmt HolyC format string।
 * @return Heap-allocated result string, या allocation failure पर nullptr।
 */
char* MStrPrint(const char* fmt, ...);

/**
 * @brief MStrPrint का alias — formatted string allocate करो; caller Free() करे।
 *
 * @param fmt HolyC format string।
 * @return Heap-allocated result string, या allocation failure पर nullptr।
 */
char* StrPrintf(const char* fmt, ...);

/**
 * @brief dst पर formatted string append करो (in-place); नई length लौटाओ।
 *
 * @param dst Destination buffer (काफी बड़ा होना चाहिए)।
 * @param fmt HolyC format string।
 * @return Appending के बाद dst की नई total length।
 */
int64_t CatPrint(char* dst, const char* fmt, ...);

// Math functions — गणितीय operations

/**
 * @brief Integer exponentiation; negative exponents के लिए 0 लौटाओ।
 *
 * @param base Base value।
 * @param exp  Exponent; negative values 0 देते हैं।
 * @return base की exp power।
 */
int64_t __holyc_ipow(int64_t base, int64_t exp);

double  __holyc_sqrt(double x);
double  __holyc_abs_f64(double x);
int64_t __holyc_abs_i64(int64_t x);
double  __holyc_sin(double x);
double  __holyc_cos(double x);
double  __holyc_tan(double x);
double  __holyc_atan(double x);
double  __holyc_atan2(double y, double x);
double  __holyc_exp(double x);
double  __holyc_log(double x);
double  __holyc_log2(double x);
double  __holyc_log10(double x);
double  __holyc_pow(double base, double exp);
double  __holyc_ceil(double x);
double  __holyc_floor(double x);
double  __holyc_round(double x);

/**
 * @brief val को inclusive range [lo, hi] में clamp करो।
 *
 * @param val Clamp करने वाली value।
 * @param lo  Lower bound (inclusive)।
 * @param hi  Upper bound (inclusive)।
 * @return [lo, hi] में clamped val।
 */
int64_t __holyc_clamp(int64_t val, int64_t lo, int64_t hi);

int64_t __holyc_min(int64_t a, int64_t b);
int64_t __holyc_max(int64_t a, int64_t b);

/**
 * @brief Negative, zero, या positive x के लिए -1, 0, या 1 लौटाओ।
 *
 * @param x Input integer।
 * @return x का signum।
 */
int64_t __holyc_sign(int64_t x);

/**
 * @brief Uniformly distributed 64-bit random value लौटाओ।
 *
 * @return Random uint64_t value।
 */
uint64_t RandU64(void);

/**
 * @brief Global random number generator को seed करो।
 *
 * @param seed srand() को pass करने वाला seed value।
 */
void SeedRand(int64_t seed);

int64_t ToI64(double x);
double  ToF64(int64_t x);

double ACos(double x);
double ASin(double x);
double Sinh(double x);
double Cosh(double x);
double Tanh(double x);
double Fmod(double x, double y);
double FMod(double x, double y);
double Cbrt(double x);
double Trunc(double x);
void   Abort(void);

// Memory functions — memory allocation और management

/**
 * @brief n bytes allocate करो (uninitialized); n <= 0 या > 1 GB हो तो nullptr लौटाओ।
 *
 * @param n Allocate करने के bytes की संख्या।
 * @return Allocated memory का pointer, या failure पर nullptr।
 */
void* MAlloc(int64_t n);

/**
 * @brief n zero-initialized bytes allocate करो; n <= 0 या > 1 GB हो तो nullptr लौटाओ।
 *
 * @param n Allocate करने के bytes की संख्या।
 * @return Zero-initialized memory का pointer, या failure पर nullptr।
 */
void* CAlloc(int64_t n);

void    Free(void* p);

/**
 * @brief src से dst में n bytes copy करो; n <= 0 हो तो no-op।
 *
 * @param dst Destination buffer।
 * @param src Source buffer।
 * @param n   Copy करने के bytes की संख्या।
 * @return dst।
 */
void* MemCpy(void* dst, const void* src, int64_t n);

/**
 * @brief dst पर n bytes को val set करो; n <= 0 हो तो no-op।
 *
 * @param dst Destination buffer।
 * @param val Write करने वाला byte value (low 8 bits use होते हैं)।
 * @param n   Set करने के bytes की संख्या।
 * @return dst।
 */
void* MemSet(void* dst, int64_t val, int64_t n);

int64_t MemCmp(const void* a, const void* b, int64_t n);

/**
 * @brief Allocation resize करो; new_size <= 0 या > 1 GB हो तो nullptr लौटाओ।
 *
 * @param ptr      Existing allocation (nullptr हो सकता है)।
 * @param new_size Bytes में requested new size।
 * @return Resized block का pointer, या invalid size पर nullptr।
 */
void* ReAlloc(void* ptr, int64_t new_size);

/**
 * @brief ptr का usable size allocator के report के हिसाब से लौटाओ।
 *
 * @param ptr Live allocation का pointer।
 * @return Usable byte count, या ptr nullptr हो तो 0।
 */
int64_t MSize(void* ptr);

/**
 * @brief src का same usable size वाला duplicate allocate करो।
 *
 * @param src Duplicate करने वाला source allocation।
 * @return नई copy का pointer, या src nullptr हो तो nullptr।
 */
void* MAllocIdent(void* src);

// String functions — string manipulation operations (string पर काम करने के functions)
int64_t StrLen(const char* s);
char*   StrCpy(char* dst, const char* src);
int64_t StrCmp(const char* a, const char* b);
int64_t StrNCmp(const char* a, const char* b, int64_t n);

/**
 * @brief haystack में needle search करो; first match का pointer या nullptr लौटाओ।
 *
 * @param needle   ढूंढने वाला substring।
 * @param haystack जिसमें search करना है वह string।
 * @return haystack में first match की शुरुआत पर pointer, या nullptr।
 */
char* StrFind(const char* needle, const char* haystack);

char* StrCat(char* dst, const char* src);

/**
 * @brief src से dst में n-1 bytes तक copy करो और null-terminate करो; 64 MB पर cap करो।
 *
 * @param dst Destination buffer कम से कम n bytes का।
 * @param src Source string।
 * @param n   Null terminator सहित maximum bytes।
 * @return dst।
 */
char* StrCpyN(char* dst, const char* src, int64_t n);

/**
 * @brief dst पर src append करो total buffer size n से exceed किए बिना; 64 MB पर cap करो।
 *
 * @param dst Destination buffer जिसमें already null-terminated string है।
 * @param src Append करने वाली string।
 * @param n   dst की total buffer capacity।
 * @return dst।
 */
char* StrCatN(char* dst, const char* src, int64_t n);

/**
 * @brief Case-insensitive string comparison।
 *
 * @param a First string।
 * @param b Second string।
 * @return strcmp की तरह negative, zero, या positive।
 */
int64_t StrICmp(const char* a, const char* b);

/**
 * @brief s को base-10 integer के रूप में parse करो; null input पर 0 लौटाओ।
 *
 * @param s Null-terminated decimal string, या nullptr।
 * @return Parsed value, या s nullptr हो तो 0।
 */
int64_t Str2I64(const char* s);

/**
 * @brief s को floating-point number के रूप में parse करो; null input पर 0.0 लौटाओ।
 *
 * @param s Null-terminated float string, या nullptr।
 * @return Parsed value, या s nullptr हो तो 0.0।
 */
double  Str2F64(const char* s);

/**
 * @brief * और ? wildcards के साथ glob pattern match करो; match पर 1 लौटाओ।
 *
 * @param pattern Glob pattern string।
 * @param str     Pattern के against test करने वाली string।
 * @return 1 अगर str pattern से match करे, नहीं तो 0।
 */
int64_t WildMatch(const char* pattern, const char* str);

/**
 * @brief WildMatch का alias।
 *
 * @param pattern Glob pattern string।
 * @param str     Pattern के against test करने वाली string।
 * @return 1 अगर str pattern से match करे, नहीं तो 0।
 */
int64_t StrMatch(const char* pattern, const char* str);


/**
 * @brief str में ch की occurrences की संख्या लौटाओ।
 *
 * @param str Search करने वाली null-terminated string।
 * @param ch  Count करने वाला character value।
 * @return Occurrences की count।
 */
int64_t StrOcc(const char* str, int64_t ch);

/**
 * @brief str में ch की first occurrence का pointer लौटाओ, या nullptr।
 *
 * @param str Null-terminated string।
 * @param ch  ढूंढने वाला character।
 * @return First occurrence का pointer, या न मिले तो nullptr।
 */
char*   StrFirst(const char* str, int64_t ch);

/**
 * @brief str में ch की last occurrence का pointer लौटाओ, या nullptr।
 *
 * @param str Null-terminated string।
 * @param ch  ढूंढने वाला character।
 * @return Last occurrence का pointer, या न मिले तो nullptr।
 */
char*   StrLast(const char* str, int64_t ch);

// Bit operations — bit-level manipulation functions (bits पर काम करने के functions)

/**
 * @brief Bit-scan forward: lowest set bit का index लौटाओ, या x == 0 हो तो -1।
 *
 * @param x Input value।
 * @return Lowest set bit का 0-based bit index, या -1।
 */
int64_t Bsf(int64_t x);

/**
 * @brief Bit-scan reverse: highest set bit का index लौटाओ, या x == 0 हो तो -1।
 *
 * @param x Input value।
 * @return Highest set bit का 0-based bit index, या -1।
 */
int64_t Bsr(int64_t x);

/**
 * @brief x में set bits की population count लौटाओ।
 *
 * @param x Input value।
 * @return Set bits की संख्या।
 */
int64_t BCnt(int64_t x);

/**
 * @brief Bit test: val का bit `bit` set हो तो 1 लौटाओ, नहीं तो 0।
 *
 * @param val Test करने वाली value।
 * @param bit Bit index (0-63)।
 * @return Bit set हो तो 1, नहीं तो 0।
 */
int64_t Bt(int64_t val, int64_t bit);

/**
 * @brief Bit test and set: *val में bit `bit` set करो; bit की old value लौटाओ।
 *
 * @param val Modify करने वाली value का pointer।
 * @param bit Bit index (0-63)।
 * @return Bit की previous value (0 या 1)।
 */
int64_t Bts(int64_t* val, int64_t bit);

/**
 * @brief Bit test and reset: *val में bit `bit` clear करो; bit की old value लौटाओ।
 *
 * @param val Modify करने वाली value का pointer।
 * @param bit Bit index (0-63)।
 * @return Bit की previous value (0 या 1)।
 */
int64_t Btr(int64_t* val, int64_t bit);

/**
 * @brief Bit test and complement: *val में bit `bit` toggle करो; bit की old value लौटाओ।
 *
 * @param val Modify करने वाली value का pointer।
 * @param bit Bit index (0-63)।
 * @return Bit की previous value (0 या 1)।
 */
int64_t Btc(int64_t* val, int64_t bit);

/**
 * @brief val से bit `bit` पर शुरू होने वाला `count`-bit unsigned field extract करो।
 *
 * @param val   Source integer।
 * @param bit   Starting bit index (0-63)।
 * @param count Extract करने के bits की संख्या।
 * @return Extracted unsigned bit field।
 */
int64_t BFieldExtU32(int64_t val, int64_t bit, int64_t count);

// Character operations — character classification और conversion
int64_t ToUpper(int64_t ch);
int64_t ToLower(int64_t ch);
int64_t IsAlpha(int64_t ch);
int64_t IsDigit(int64_t ch);
int64_t IsAlphaNum(int64_t ch);
int64_t IsUpper(int64_t ch);
int64_t IsLower(int64_t ch);
int64_t IsSpace(int64_t ch);
int64_t IsPunct(int64_t ch);
int64_t IsCtrl(int64_t ch);
int64_t IsXDigit(int64_t ch);
int64_t IsGraph(int64_t ch);
int64_t IsPrint(int64_t ch);

// I/O — Input/Output operations (terminal पर पढ़ना और लिखना)

/**
 * @brief ch में packed up to 8 bytes stdout पर characters लिखो।
 *
 * @param ch 64-bit integer में packed up to 8 ASCII bytes (low byte first)।
 */
void PutChars(int64_t ch);

/**
 * @brief stdin से एक character read करो; EOF पर -1 लौटाओ।
 *
 * @return Character value (0-255), या EOF पर -1।
 */
int64_t GetChar(void);

/**
 * @brief buf में line read करो (max-1 chars तक), trailing newline strip करो; length लौटाओ।
 *
 * @param buf Line receive करने वाला buffer।
 * @param max Null terminator सहित maximum buffer size।
 * @return buf में रखी string की length।
 */
int64_t GetStr(char* buf, int64_t max);

// System — system-level operations जैसे sleep और time

/**
 * @brief nanosleep use करके ms milliseconds sleep करो।
 *
 * @param ms Sleep करने के milliseconds।
 */
void Sleep(int64_t ms);

/**
 * @brief किसी fixed epoch से monotonic time milliseconds में लौटाओ।
 *
 * @return Current CLOCK_MONOTONIC time milliseconds में।
 */
int64_t GetTicks(void);

/**
 * @brief Current local time को Terry Davis packed date/time integer के रूप में लौटाओ।
 *
 * Integer अलग-अलग bit fields में seconds, minutes, hours, day, month, और year encode करता है।
 *
 * @return Packed date/time integer।
 */
int64_t Now(void);

/**
 * @brief GetTicks का alias।
 *
 * @return Current CLOCK_MONOTONIC time milliseconds में।
 */
int64_t GetTickCount(void);

/**
 * @brief Hardware breakpoint trap trigger करो (__builtin_trap)।
 */
void SysDbg(void);

// File I/O — file read/write operations (file से पढ़ना और लिखना)

/**
 * @brief File open करो; mode HC_O_READ/WRITE/APPEND/CREATE का bitmask है। fd या -1 लौटाओ।
 *
 * @param path Open करने वाला file path।
 * @param mode HC_O_READ (1), HC_O_WRITE (2), HC_O_APPEND (4), HC_O_CREATE (8) का bitmask।
 * @return int64_t cast किया opaque file descriptor, या error पर -1।
 */
int64_t FileOpen(const char* path, int64_t mode);

/**
 * @brief FileOpen से returned file descriptor close करो।
 *
 * @param fd Close करने वाला file descriptor।
 */
void    FileClose(int64_t fd);

/**
 * @brief fd से buf में count bytes तक read करो; bytes read लौटाओ।
 *
 * @param fd    File descriptor।
 * @param buf   Destination buffer।
 * @param count Maximum bytes to read।
 * @return Actually read bytes।
 */
int64_t FileRead(int64_t fd, void* buf, int64_t count);

/**
 * @brief buf से fd पर count bytes write करो; bytes written लौटाओ।
 *
 * @param fd    File descriptor।
 * @param buf   Source buffer।
 * @param count Write करने के bytes की संख्या।
 * @return Actually written bytes।
 */
int64_t FileWrite(int64_t fd, const void* buf, int64_t count);

/**
 * @brief fd के पीछे file का total byte size लौटाओ।
 *
 * @param fd File descriptor।
 * @return File size bytes में, या error पर -1।
 */
int64_t FileSize(int64_t fd);

/**
 * @brief fd में absolute byte offset पर seek करो; success पर 0 लौटाओ।
 *
 * @param fd     File descriptor।
 * @param offset Absolute byte position।
 * @return Success पर 0, error पर non-zero।
 */
int64_t FileSeek(int64_t fd, int64_t offset);

/**
 * @brief Path exist करे और reading के लिए open हो सके तो 1, नहीं तो 0 लौटाओ।
 *
 * @param path Test करने वाला file path।
 * @return Accessible हो तो 1, नहीं तो 0।
 */
int64_t FileExists(const char* path);

/**
 * @brief पूरी file को malloc'd buffer में read करो (max 256 MB); *size_out set करो।
 *
 * @param path     Read करने वाला file path।
 * @param size_out Non-null हो तो read किए bytes की संख्या receive करता है।
 * @return File contents वाला heap-allocated buffer, या failure पर nullptr।
 */
void*   FileReadAll(const char* path, int64_t* size_out);

/**
 * @brief data से size bytes को path पर write करो, file create या truncate करो।
 *
 * @param path Write करने वाला file path।
 * @param data Source data buffer।
 * @param size Write करने के bytes की संख्या।
 */
void    FileWriteAll(const char* path, const void* data, int64_t size);

/**
 * @brief File delete करो; success पर 0 लौटाओ।
 *
 * @param path Delete करने वाला file path।
 * @return Success पर 0, failure पर non-zero।
 */
int64_t FileDel(const char* path);

// Exception support (setjmp/longjmp based) — HolyC exception handling के लिए

/**
 * @brief Enclosing try block के लिए per-thread try-stack पर jmp_buf push करो।
 *
 * @param buf Caller द्वारा allocated jmp_buf का pointer।
 */
void    __holyc_try_push(void* buf);

/**
 * @brief Try block cleanly exit होने पर try-stack से innermost jmp_buf pop करो।
 */
void    __holyc_try_pop(void);

/**
 * @brief Most recent throw का exception code लौटाओ।
 *
 * @return Last __holyc_throw() call से set हुआ exception code।
 */
int64_t __holyc_except_code(void);

/**
 * @brief Nearest try block पर longjmp करके HolyC exception throw करो; कोई न हो तो abort।
 *
 * @param code __holyc_except_code() के through available exception code।
 */
void    __holyc_throw(int64_t code);


/**
 * @brief Per-thread vararg buffer में slot idx पर vararg value store करो।
 *
 * @param idx Slot index (0-63)।
 * @param val Store करने वाली value।
 */
void    __holyc_va_store(int64_t idx, int64_t val);

/**
 * @brief Per-thread buffer में vararg values की count set करो।
 *
 * @param n Valid vararg slots की संख्या।
 */
void    __holyc_va_set_count(int64_t n);

/**
 * @brief Per-thread buffer में stored varargs की संख्या लौटाओ।
 *
 * @return __holyc_va_set_count() से set vararg count।
 */
int64_t __holyc_vararg_count(void);

/**
 * @brief Per-thread buffer से i-th vararg value लौटाओ।
 *
 * @param i Vararg buffer में index।
 * @return Stored value, या i out of range हो तो 0।
 */
int64_t __holyc_vararg_get(int64_t i);

// Extended string functions — अतिरिक्त string utility functions

/**
 * @brief s का malloc'd copy allocate करो; caller Free() करे।
 *
 * @param s Duplicate करने वाली source string।
 * @return Heap-allocated copy, या s nullptr हो तो nullptr।
 */
char*   StrNew(const char* s);

/**
 * @brief StrNew का alias।
 *
 * @param s Duplicate करने वाली source string।
 * @return Heap-allocated copy, या s nullptr हो तो nullptr।
 */
char*   StrDup(const char* s);

/**
 * @brief s को in-place uppercase में convert करो; s लौटाओ।
 *
 * @param s Convert करने वाली string।
 * @return s।
 */
char*   StrUpr(char* s);

/**
 * @brief s को in-place lowercase में convert करो; s लौटाओ।
 *
 * @param s Convert करने वाली string।
 * @return s।
 */
char*   StrLwr(char* s);

/**
 * @brief Standard printf codes (HolyC codes नहीं) use करके buf में format करो; length लौटाओ।
 *
 * @param buf Destination buffer (up to 64 KB)।
 * @param fmt Standard printf format string।
 * @return buf में formatted string की length।
 */
int64_t SPrint(char* buf, const char* fmt, ...);

/**
 * @brief s की length लौटाओ, most max_len characters scan करके।
 *
 * @param s       Measure करने वाली string।
 * @param max_len Maximum scan length।
 * @return s में characters की संख्या, max_len पर capped।
 */
int64_t StrNLen(const char* s, int64_t max_len);

// Extended random — अतिरिक्त random number generation

/**
 * @brief Signed 64-bit random integer लौटाओ।
 *
 * @return Random int64_t value।
 */
int64_t RandI64(void);

// Directory / filesystem — directory और file system operations

/**
 * @brief Path existing directory हो तो 1, नहीं तो 0 लौटाओ।
 *
 * @param path Test करने वाला path।
 * @return path directory हो तो 1, नहीं तो 0।
 */
int64_t DirExists(const char* path);

/**
 * @brief Mode 0755 से directory create करो; success पर 0 लौटाओ।
 *
 * @param path Create करने वाला directory path।
 * @return Success पर 0, failure पर non-zero।
 */
int64_t DirMk(const char* path);

/**
 * @brief File को src से dst पर rename करो; success पर 0 लौटाओ।
 *
 * @param src Current file path।
 * @param dst New file path।
 * @return Success पर 0, failure पर non-zero।
 */
int64_t FileRename(const char* src, const char* dst);

// Memory move — overlapping regions को safely copy करना

/**
 * @brief src से dst में n bytes move करो, overlapping regions handle करो।
 *
 * @param dst Destination buffer।
 * @param src Source buffer।
 * @param n   Move करने के bytes की संख्या।
 * @return dst।
 */
void*   MemMove(void* dst, const void* src, int64_t n);

// Argc / Argv access — command-line arguments तक पहुंच
extern int    g_argc; //!< Global argv count (main में set होता है)।
extern char** g_argv; //!< Global argv array (main में set होता है)।

/**
 * @brief Program का argument vector (argv) लौटाओ।
 *
 * @return argv array का pointer।
 */
char**  ArgV(void);

/**
 * @brief Program का argument count (argc) लौटाओ।
 *
 * @return main() से argc value।
 */
int64_t ArgC(void);

#ifdef __cplusplus
}
#endif
