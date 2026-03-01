#pragma once

#ifdef HCC_HAS_LLVM

#include <string>
#include <memory>
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>

namespace holyc {

/**
 * @brief JIT compilation के लिए persistent bitcode cache।
 *
 * Cache key = SHA-256(hcc_version | target_triple | preprocessed_source)।
 * Cache directory default है $HOME/.cache/hcc/ या HCC_CACHE_DIR env var।
 * Compiled modules LLVM bitcode files के रूप में store होते हैं; concurrent compiles पर
 * TOCTOU races से बचने के लिए atomic tmp-then-rename writes use होती हैं।
 */
class JITCache {
public:
    /**
     * @brief दिए गए directory से backed JITCache construct करो।
     *
     * @param cacheDir .bc files store करने वाली directory का absolute path।
     */
    explicit JITCache(const std::string& cacheDir);

    /**
     * @brief Cache hit पर cached .bc file का path लौटाओ, miss पर empty string।
     *
     * @param preprocessed_src Preprocessed source text (cache key का हिस्सा)।
     * @param triple           LLVM target triple string।
     * @return Cached bitcode file का absolute path, या miss पर ""।
     */
    std::string lookup(const std::string& preprocessed_src,
                       const std::string& triple) const;

    /**
     * @brief Compiled module को cache में bitcode के रूप में store करो।
     *
     * @param preprocessed_src Preprocessed source text (cache key का हिस्सा)।
     * @param triple           LLVM target triple string।
     * @param mod              Serialize करने वाला compiled LLVM module।
     */
    void store(const std::string& preprocessed_src,
               const std::string& triple,
               llvm::Module& mod) const;

    /**
     * @brief Cache directory path लौटाओ।
     *
     * @return Cache directory string का reference।
     */
    const std::string& dir() const { return cacheDir_; }

    /**
     * @brief Default cache directory compute करो ($HOME/.cache/hcc या HCC_CACHE_DIR)।
     *
     * @return Default cache directory का absolute path।
     */
    static std::string defaultDir();

private:
    std::string cacheDir_;

    /**
     * @brief Cache key hex string के रूप में compute करो (SHA-256 of version|triple|source)।
     *
     * @param preprocessed_src Preprocessed source text।
     * @param triple           LLVM target triple string।
     * @return 64-character lowercase hex SHA-256 digest।
     */
    static std::string computeKey(const std::string& preprocessed_src,
                                   const std::string& triple);

    /**
     * @brief Cache directory exist सुनिश्चित करो, ज़रूरत पर create करो।
     *
     * @param dir Create करने वाला directory path।
     * @return Success पर True, creation fail हो तो false।
     */
    static bool ensureDir(const std::string& dir);
};

} // namespace holyc

#endif // HCC_HAS_LLVM
