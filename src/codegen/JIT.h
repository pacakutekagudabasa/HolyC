#pragma once

#ifdef HCC_HAS_LLVM

#include <memory>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace holyc {

/**
 * @brief JIT executor: ORC LLJIT के through HolyC LLVM module compile और immediately run करता है।
 */
class JIT {
public:
    /**
     * @brief Module को JIT-compile करो और उसका 'main' symbol invoke करो।
     *
     * Native target initialize करता है, ORC LLJIT instance construct करता है,
     * DynamicLibrarySearchGenerator add करता है ताकि libc और runtime symbols resolve हों,
     * फिर 'main' lookup करके call करता है।
     *
     * @param mod Compile और run करने वाला LLVM module। Ownership consume होता है।
     * @param ctx Module के types और constants का owner LLVMContext। Ownership consume होता है।
     * @return main का return किया exit code, या compilation या lookup fail हो तो 1।
     */
    static int run(std::unique_ptr<llvm::Module> mod,
                   std::unique_ptr<llvm::LLVMContext> ctx);
};

} // namespace holyc

#endif // HCC_HAS_LLVM
