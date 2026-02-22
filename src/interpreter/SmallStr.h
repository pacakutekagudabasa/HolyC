#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace holyc {

/**
 * @brief 16-byte SSO string: 14 chars तक inline, उससे ज़्यादा heap पर।
 *
 * Inline representation में s_.len (0-14) में length store होती है और characters
 * s_.inl[0..14] में। Heap representation में s_.len == HeapFlag (0xFF) set होता है,
 * 24-bit length h_.lb[0..2] में store होती है, और heap pointer h_.ptr में।
 */
class SmallStr {
public:
    static constexpr size_t SsoMax   = 14;

    SmallStr() noexcept { s_.len = 0; s_.inl[0] = '\0'; }

    SmallStr(const char* s) {
        s_.len = 0;
        if (s) assign(s, std::strlen(s));
        else   s_.inl[0] = '\0';
    }

    SmallStr(const char* s, size_t n) {
        s_.len = 0;
        assign(s ? s : "", n);
    }

    // explicit: ternary में operator std::string() से ambiguity से बचाता है
    explicit SmallStr(const std::string& s) {
        s_.len = 0;
        assign(s.data(), s.size());
    }

    SmallStr(const SmallStr& o) {
        s_.len = 0;
        if (o.is_heap() && o.h_.ptr) {
            assign(o.h_.ptr, o.heap_len());
        } else {
            s_.len = o.s_.len;
            std::memcpy(s_.inl, o.s_.inl, 15);
        }
    }

    SmallStr(SmallStr&& o) noexcept {
        s_.len = o.s_.len;
        std::memcpy(s_.inl, o.s_.inl, 15);
        o.s_.len = 0;
        o.s_.inl[0] = '\0';
    }

    SmallStr& operator=(const SmallStr& o) {
        if (this == &o) return *this;
        if (o.is_heap() && o.h_.ptr) {
            assign(o.h_.ptr, o.heap_len());
        } else {
            if (is_heap()) { delete[] h_.ptr; }
            s_.len = o.s_.len;
            std::memcpy(s_.inl, o.s_.inl, 15);
        }
        return *this;
    }

    SmallStr& operator=(SmallStr&& o) noexcept {
        if (this == &o) return *this;
        if (is_heap()) delete[] h_.ptr;
        s_.len = o.s_.len;
        std::memcpy(s_.inl, o.s_.inl, 15);
        o.s_.len = 0;
        o.s_.inl[0] = '\0';
        return *this;
    }

    SmallStr& operator=(const std::string& s) {
        assign(s.data(), s.size());
        return *this;
    }

    SmallStr& operator=(const char* s) {
        if (is_heap()) { delete[] h_.ptr; s_.len = 0; }
        if (s) assign(s, std::strlen(s));
        else   { s_.len = 0; s_.inl[0] = '\0'; }
        return *this;
    }

    ~SmallStr() { if (is_heap()) delete[] h_.ptr; }

    /**
     * @brief String data का null-terminated pointer लौटाओ।
     *
     * @return Inline buffer या heap buffer का pointer।
     */
    const char* c_str()  const noexcept { return is_heap() ? h_.ptr  : s_.inl; }
    const char* data()   const noexcept { return c_str(); }
    size_t      size()   const noexcept { return is_heap() ? heap_len() : (size_t)s_.len; }
    size_t      length() const noexcept { return size(); }
    bool        empty()  const noexcept { return size() == 0; }

    char operator[](size_t i) const noexcept { return c_str()[i]; }

    /**
     * @brief strcmp के through lexicographic comparison करो।
     *
     * @param o Compare करने वाला दूसरा SmallStr।
     * @return strcmp की तरह negative, zero, या positive।
     */
    int  compare(const SmallStr& o)    const noexcept { return std::strcmp(c_str(), o.c_str()); }
    bool operator==(const SmallStr& o) const noexcept { return compare(o) == 0; }
    bool operator!=(const SmallStr& o) const noexcept { return compare(o) != 0; }
    bool operator< (const SmallStr& o) const noexcept { return compare(o) <  0; }

    operator std::string() const { return std::string(c_str(), size()); }

private:
    static constexpr uint8_t HeapFlag = 0xFF;

    union {
        struct { uint8_t len; char    inl[15];                            } s_;
        struct { uint8_t flag; uint8_t lb[3]; uint8_t pad[4]; char* ptr; } h_;
    };

    bool   is_heap()   const noexcept { return s_.len == HeapFlag; }
    size_t heap_len()  const noexcept {
        return (size_t)h_.lb[0]
             | ((size_t)h_.lb[1] << 8)
             | ((size_t)h_.lb[2] << 16);
    }
    void set_heap_len(size_t n) noexcept {
        h_.lb[0] = (uint8_t)(n);
        h_.lb[1] = (uint8_t)(n >> 8);
        h_.lb[2] = (uint8_t)(n >> 16);
    }

    void assign(const char* s, size_t n) {
        if (n > 0xFFFFFF) n = 0xFFFFFF; // 24-bit length cap
        if (is_heap()) delete[] h_.ptr;
        if (n <= SsoMax) {
            s_.len = (uint8_t)n;
            std::memcpy(s_.inl, s, n);
            s_.inl[n] = '\0';
        } else {
            h_.flag = HeapFlag;
            set_heap_len(n);
            h_.ptr = new char[n + 1];
            std::memcpy(h_.ptr, s, n);
            h_.ptr[n] = '\0';
        }
    }
};

static_assert(sizeof(SmallStr) == 16,  "SmallStr must be exactly 16 bytes");
static_assert(alignof(SmallStr) == 8,  "SmallStr must be 8-byte aligned");

} // namespace holyc
