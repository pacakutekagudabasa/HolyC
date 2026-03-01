#ifdef HCC_HAS_LLVM

#include "codegen/AOTCompiler.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <llvm/Support/Path.h>

namespace holyc {

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
int AOTCompiler::compile(std::unique_ptr<llvm::Module> mod,
                         llvm::LLVMContext& ctx,
                         const Options& opts) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    std::string triple = opts.targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple()
        : opts.targetTriple;
    mod->setTargetTriple(triple);

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        llvm::errs() << "error: " << error << "\n";
        return 1;
    }

    llvm::TargetOptions tOpts;
    auto tm = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(
            triple, "generic", "", tOpts,
            llvm::Reloc::PIC_));
    if (!tm) {
        llvm::errs() << "error: could not create target machine\n";
        return 1;
    }

    mod->setDataLayout(tm->createDataLayout());

    {
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;

        llvm::PassBuilder PB(tm.get());
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        llvm::ModulePassManager MPM;

        if (opts.optLevel == 0) {
            llvm::FunctionPassManager FPM;
            FPM.addPass(llvm::PromotePass());
            MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
        } else {
            llvm::OptimizationLevel level;
            if (opts.optSize)
                level = llvm::OptimizationLevel::Os;
            else if (opts.optLevel == 1)
                level = llvm::OptimizationLevel::O1;
            else if (opts.optLevel == 2)
                level = llvm::OptimizationLevel::O2;
            else
                level = llvm::OptimizationLevel::O3;
            MPM = PB.buildPerModuleDefaultPipeline(level);
        }

        MPM.run(*mod, MAM);
    }

    if (opts.emitBC) {
        std::error_code EC;
        llvm::raw_fd_ostream out(opts.outputFile, EC, llvm::sys::fs::OF_None);
        if (EC) {
            llvm::errs() << "error: could not open " << opts.outputFile << ": " << EC.message() << "\n";
            return 1;
        }
        llvm::WriteBitcodeToFile(*mod, out);
        return 0;
    }

    auto emitToFile = [&](const std::string& filename,
                          llvm::CodeGenFileType fileType) -> int {
        std::error_code EC;
        llvm::raw_fd_ostream out(filename, EC, llvm::sys::fs::OF_None);
        if (EC) {
            llvm::errs() << "error: could not open " << filename << ": " << EC.message() << "\n";
            return 1;
        }
        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, out, nullptr, fileType)) {
            llvm::errs() << "error: target cannot emit this file type\n";
            return 1;
        }
        pm.run(*mod);
        out.flush();
        return 0;
    };

    if (opts.emitAsm) {
        return emitToFile(opts.outputFile, llvm::CodeGenFileType::AssemblyFile);
    }

    if (opts.objectOnly) {
        return emitToFile(opts.outputFile, llvm::CodeGenFileType::ObjectFile);
    }

    // Cross-compilation: object ही emit करो और user से manually link करने को कहो।
    {
        std::string hostTriple = llvm::sys::getDefaultTargetTriple();
        bool isCrossCompile = !opts.targetTriple.empty() && (opts.targetTriple != hostTriple);
        if (isCrossCompile) {
            std::string objFile = opts.outputFile;
            if (objFile == "a.out") objFile = "out.o";
            if (objFile.size() < 2 || objFile.substr(objFile.size() - 2) != ".o")
                objFile += ".o";
            int ret = emitToFile(objFile, llvm::CodeGenFileType::ObjectFile);
            if (ret == 0) {
                llvm::errs() << "Cross-compilation: wrote " << objFile
                             << " for " << triple
                             << ". Link manually with a sysroot.\n";
            }
            return ret;
        }
    }

    llvm::SmallString<128> tmpObj;
    {
        std::error_code EC = llvm::sys::fs::createTemporaryFile("hcc", "o", tmpObj);
        if (EC) {
            llvm::errs() << "error: could not create temp file: " << EC.message() << "\n";
            return 1;
        }
    }

    int ret = emitToFile(std::string(tmpObj), llvm::CodeGenFileType::ObjectFile);
    if (ret != 0) {
        llvm::sys::fs::remove(tmpObj);
        return ret;
    }

    // libholyc_rt.a locate करो: executable के पास, ../lib, system paths, या CWD।
    std::string rtLib;
    {
        std::string exePath = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
        llvm::SmallString<256> exeDir(exePath);
        llvm::sys::path::remove_filename(exeDir);

        llvm::SmallString<256> candidate;

        candidate = exeDir;
        llvm::sys::path::append(candidate, "libholyc_rt.a");
        if (llvm::sys::fs::exists(candidate)) {
            rtLib = std::string(candidate);
        }
        if (rtLib.empty()) {
            candidate = exeDir;
            llvm::sys::path::append(candidate, "..", "lib", "libholyc_rt.a");
            if (llvm::sys::fs::exists(candidate)) rtLib = std::string(candidate);
        }
        if (rtLib.empty()) {
            const char* sysPaths[] = {
                "/usr/local/lib/libholyc_rt.a",
                "/usr/lib/libholyc_rt.a",
                "/usr/local/lib/holyc/libholyc_rt.a",
            };
            for (const char* sp : sysPaths) {
                if (llvm::sys::fs::exists(sp)) { rtLib = sp; break; }
            }
        }
        if (rtLib.empty() && std::getenv("HCC_ALLOW_LOCAL_RT")) {
            const char* cwdPaths[] = { "libholyc_rt.a", "./libholyc_rt.a" };
            for (const char* rp : cwdPaths) {
                if (llvm::sys::fs::exists(rp)) { rtLib = rp; break; }
            }
        }
    }

    // argv build करो — कभी shell से pass नहीं होता।
    std::vector<std::string> argvStr;
    argvStr.push_back("cc");
    if (opts.sharedLib) argvStr.push_back("-shared");
    argvStr.push_back(std::string(tmpObj));
    if (!opts.sharedLib && !rtLib.empty()) argvStr.push_back(rtLib);
    if (opts.debugInfo) argvStr.push_back("-g");
    argvStr.push_back("-o");
    argvStr.push_back(opts.outputFile);
    if (!opts.sharedLib) {
        argvStr.push_back("-lm");
        argvStr.push_back("-lstdc++");
    }
    std::vector<const char*> argv;
    for (const auto& s : argvStr) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        ret = 1;
    } else if (pid == 0) {
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    } else {
        int status = 0;
        waitpid(pid, &status, 0);
        ret = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    llvm::sys::fs::remove(tmpObj);

    return ret == 0 ? 0 : 1;
}

} // namespace holyc

#endif // HCC_HAS_LLVM
