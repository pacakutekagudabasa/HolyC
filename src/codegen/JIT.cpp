#ifdef HCC_HAS_LLVM

#include "codegen/JIT.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Error.h>

#include <cstdio>

namespace holyc {

int JIT::run(std::unique_ptr<llvm::Module> mod,
             std::unique_ptr<llvm::LLVMContext> ctx) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto jitExpected = llvm::orc::LLJITBuilder().create();
    if (!jitExpected) {
        llvm::errs() << "JIT creation failed: " << jitExpected.takeError() << "\n";
        return 1;
    }
    auto& jit = *jitExpected;

    // Current process के लिए dynamic library search add करो (libc symbols resolve होंगे)
    auto dlsg = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix());
    if (!dlsg) {
        llvm::errs() << "Failed to create DynamicLibrarySearchGenerator: "
                      << dlsg.takeError() << "\n";
        return 1;
    }
    jit->getMainJITDylib().addGenerator(std::move(*dlsg));

    // IR module अपने owning context के साथ add करो
    auto tsCtx = std::make_unique<llvm::orc::ThreadSafeContext>(std::move(ctx));
    auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(mod), *tsCtx));
    if (err) {
        llvm::errs() << "Failed to add IR module: " << err << "\n";
        return 1;
    }

    // main lookup करो
    auto mainSym = jit->lookup("main");
    if (!mainSym) {
        llvm::errs() << "Failed to find 'main': " << mainSym.takeError() << "\n";
        return 1;
    }

    auto* mainFn = mainSym->toPtr<int(*)()>();
    return mainFn();
}

} // namespace holyc

#endif // HCC_HAS_LLVM
