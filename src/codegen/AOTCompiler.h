#pragma once

#ifdef HCC_HAS_LLVM

#include <memory>
#include <string>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace holyc {

/**
 * @brief Ahead-of-time compiler: LLVM module को native binary, object, या bitcode file में lower करता है।
 */
class AOTCompiler {
public:
    struct Options {
        std::string outputFile = "a.out";
        bool objectOnly = false;       // -c
        bool emitAsm = false;          // -S
        bool emitBC = false;           // --emit-bc
        bool sharedLib = false;        // -shared
        int optLevel = 0;             // -O0 to -O3
        bool optSize = false;         // -Os
        bool debugInfo = false;       // -g
        std::string targetTriple;     // --target (empty = host)
    };

    /**
     * @brief दिए गए LLVM module को executable (या opts के हिसाब से object/asm/bitcode) में compile करो।
     *
     * Options के हिसाब से LLVM target-machine pipeline run करता है, फिर bitcode/asm/object
     * directly write करता है या host 'cc' invocation के through native executable link करता है।
     * Cross-compilation में object file write करके linking hint print करता है।
     *
     * @param mod Compile करने वाला LLVM module। Ownership consume होता है।
     * @param ctx Module के types और constants का owner LLVMContext।
     * @param opts Output format, optimization level, और target control करने वाले compilation options।
     * @return Success पर 0, failure पर non-zero।
     */
    static int compile(std::unique_ptr<llvm::Module> mod,
                       llvm::LLVMContext& ctx,
                       const Options& opts);
};

} // namespace holyc

#endif // HCC_HAS_LLVM
