#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace holyc {

/**
 * @brief HolyC MAlloc/Free/ReAlloc runtime calls के लिए tracked heap allocator।
 *
 * सभी allocations एक internal map में register होती हैं ताकि उनके sizes query
 * हो सकें और Heap destroy होने पर कोई भी live block free हो जाए।
 */
class Heap {
public:
    /**
     * @brief n uninitialized bytes allocate करो और block register करो।
     *
     * @param n Allocate करने के bytes की संख्या।
     * @return Allocated memory का pointer, या failure पर nullptr।
     */
    uint8_t* alloc(size_t n) {
        auto* p = static_cast<uint8_t*>(std::malloc(n));
        if (p) allocs_[p] = n;
        return p;
    }

    /**
     * @brief n zero-initialized bytes allocate करो और block register करो।
     *
     * @param n Allocate करने के bytes की संख्या।
     * @return Zero-initialized memory का pointer, या failure पर nullptr।
     */
    uint8_t* calloc(size_t n) {
        auto* p = static_cast<uint8_t*>(std::calloc(1, n));
        if (p) allocs_[p] = n;
        return p;
    }

    /**
     * @brief पहले से allocated block free करो; untracked pointers को silently ignore करो।
     *
     * @param vptr Free करने वाला pointer (nullptr या untracked हो सकता है)।
     */
    void free(void* vptr) {
        auto* ptr = static_cast<uint8_t*>(vptr);
        auto it = allocs_.find(ptr);
        if (it != allocs_.end()) {
            std::free(ptr);
            allocs_.erase(it);
        }
    }

    /**
     * @brief Tracked block resize करो; size map atomically update करो।
     *
     * @param old_ptr  Existing allocation का pointer।
     * @param new_size Bytes में नया requested size।
     * @return Resized block का pointer, या failure पर nullptr।
     */
    uint8_t* realloc(void* old_ptr, size_t new_size) {
        auto* op = static_cast<uint8_t*>(old_ptr);
        auto* p = static_cast<uint8_t*>(std::realloc(op, new_size));
        if (p) {
            // failure पर realloc null लौटाता है; old_ptr valid रहता है
            if (op && op != p) allocs_.erase(op);
            allocs_[p] = new_size;
        }
        return p;
    }

    /**
     * @brief ptr का tracked allocation size लौटाओ, या unknown हो तो 0।
     *
     * @param ptr Query करने वाला pointer।
     * @return Registered size bytes में, या ptr untracked हो तो 0।
     */
    size_t size(void* ptr) {
        auto* p = static_cast<uint8_t*>(ptr);
        auto it = allocs_.find(p);
        return it != allocs_.end() ? it->second : 0;
    }

    /**
     * @brief सभी tracked allocation sizes का sum bytes में लौटाओ।
     *
     * @return Currently allocated कुल bytes।
     */
    size_t totalBytes() const {
        size_t total = 0;
        for (auto& [p, n] : allocs_) total += n;
        return total;
    }

    /**
     * @brief Currently live tracked allocations की संख्या लौटाओ।
     *
     * @return Live blocks की count।
     */
    size_t numBlocks() const { return allocs_.size(); }

    ~Heap() {
        for (auto& [p, _] : allocs_) {
            std::free(p);
        }
        allocs_.clear();
    }

private:
    std::unordered_map<uint8_t*, size_t> allocs_;
};

} // namespace holyc
