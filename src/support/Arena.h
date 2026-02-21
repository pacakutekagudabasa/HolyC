#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace holyc {

/**
 * @brief AST nodes के लिए destructor tracking के साथ 64 KB blocks वाला bump allocator।
 *
 * Allocations एक contiguous 64 KB block से serve होती हैं; block exhausted होने पर
 * नया block allocate होता है। Non-trivially destructible objects का destructor register
 * होता है और Arena destroy होने पर reverse order में call होते हैं।
 */
class Arena {
public:
    static constexpr size_t BlockSize = 65536;
    static constexpr size_t kMaxSingleAlloc = 256ULL * 1024 * 1024;

    Arena() { dtors_.reserve(256); }
    ~Arena() {
        for (auto it = dtors_.rbegin(); it != dtors_.rend(); ++it)
            it->second(it->first);
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    /**
     * @brief Current block से raw aligned bytes allocate करो; slow path नया block allocate करता है।
     *
     * @param size  Allocate करने के bytes की संख्या।
     * @param align Required alignment bytes में।
     * @return Allocated region का pointer।
     * @throws std::bad_alloc अगर size kMaxSingleAlloc से exceed करे या memory exhausted हो।
     */
    void* allocRaw(size_t size, size_t align) {
        if (size > kMaxSingleAlloc) throw std::bad_alloc();

        size_t aligned = current_;
        if (align > 1) {
            size_t rem = aligned % align;
            if (rem != 0) {
                size_t pad = align - rem;
                if (aligned + pad < aligned) throw std::bad_alloc();
                aligned += pad;
            }
        }

        if (!blocks_.empty() && aligned + size >= aligned && aligned + size <= capacity_) [[likely]] {
            void* ptr = block_ + aligned;
            current_ = aligned + size;
            return ptr;
        }
        size_t allocSize = size > BlockSize ? size : BlockSize;
        auto newBlock = std::make_unique<char[]>(allocSize);
        block_ = newBlock.get();
        blocks_.push_back(std::move(newBlock));
        current_ = size;
        capacity_ = allocSize;
        return block_;
    }

    /**
     * @brief Arena में T construct करो और non-trivial हो तो destructor register करो।
     *
     * @tparam T    Construct करने वाला type।
     * @tparam Args Constructor argument types।
     * @param args  T के constructor को forward किए जाने वाले arguments।
     * @return Newly constructed T का pointer।
     */
    template <typename T, typename... Args>
    T* alloc(Args&&... args) {
        void* mem = allocRaw(sizeof(T), alignof(T));
        T* obj = new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            dtors_.emplace_back(
                static_cast<void*>(obj),
                [](void* p) { static_cast<T*>(p)->~T(); }
            );
        }
        return obj;
    }

private:
    std::vector<std::unique_ptr<char[]>> blocks_;
    std::vector<std::pair<void*, void(*)(void*)>> dtors_;
    char* block_ = nullptr;
    size_t current_ = 0;
    size_t capacity_ = 0;
};

} // namespace holyc
