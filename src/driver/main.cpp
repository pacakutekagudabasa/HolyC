#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <filesystem>

#include "support/SourceManager.h"
#include "support/Diagnostics.h"
#include "support/Arena.h"
#include "lexer/Lexer.h"
#include "lexer/TokenKind.h"
#include "lexer/Token.h"
#include "preprocessor/Preprocessor.h"
#include "parser/Parser.h"
#include "ast/ASTPrinter.h"
#include "sema/Sema.h"
#include "interpreter/Interpreter.h"
#include "driver/Formatter.h"
#include "lsp/LSPServer.h"
#ifdef HCC_HAS_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef HCC_HAS_LLVM
#include "codegen/LLVMCodegen.h"
#include "codegen/JIT.h"
#include "codegen/AOTCompiler.h"
#include "driver/JITCache.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#endif

/// hcc command-line usage summary stdout पर print करता है।
static void print_usage() {
    std::puts(
        "HolyC Compiler v0.1\n"
        "Usage: hcc [options] [file]\n"
        "\n"
        "Options:\n"
        "  --print-tokens   Lex and print tokens\n"
        "  --print-ast      Parse and print AST\n"
        "  --preprocess     Run preprocessor only\n"
        "  --sema-only      Run semantic analysis only\n"
        "  --interpret       Interpret the program\n"
        "  --emit-llvm       Emit LLVM IR\n"
        "  --jit             JIT compile and execute\n"
        "  --repl            Start interactive REPL\n"
        "  -g, --debug       Emit DWARF debug info\n"
        "  -o <file>         Output file\n"
        "  -O0/-O1/-O2/-O3  Optimization level\n"
        "  -Os               Optimize for size\n"
        "  -c                Compile to object file only\n"
        "  -S                Emit assembly\n"
        "  --emit-bc         Emit LLVM bitcode\n"
        "  --target <triple> Set target triple\n"
        "  -D NAME[=value]   Define preprocessor macro\n"
        "  -Werror           Treat warnings as errors\n"
        "  -w                Suppress all warnings\n"
        "  -fmax-errors=N    Stop after N errors\n"
        "  --no-color        Disable colorized output\n"
        "  --help            Show this help\n"
    );
}

/// input_file को lex करके हर token stdout पर print करता है; error पर 1 लौटाता है।
static int run_print_tokens(const std::string& input_file) {
    holyc::SourceManager sm;
    holyc::Diagnostics diag(&sm);

    int fid = sm.loadFile(input_file);
    if (fid < 0) {
        std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
        return 1;
    }

    holyc::Lexer lexer(sm, fid, diag);
    while (true) {
        holyc::Token tok = lexer.next();
        auto loc = tok.loc;
        std::printf("%s:%u:%u  %-20s  '%.*s'",
            loc.file_path ? loc.file_path : "<unknown>",
            loc.line, loc.col,
            holyc::tokenKindToString(tok.kind),
            (int)tok.text.size(), tok.text.data());

        if (tok.kind == holyc::TokenKind::IntLiteral)
            std::printf("  val=%ld", (long)tok.intVal);
        else if (tok.kind == holyc::TokenKind::FloatLiteral)
            std::printf("  val=%g", tok.floatVal);

        std::putchar('\n');

        if (tok.kind == holyc::TokenKind::Eof)
            break;
    }
    return diag.hasErrors() ? 1 : 0;
}

/// input_file पर preprocessor run करके resulting token stream print करता है।
static int run_preprocess(const std::string& input_file) {
    holyc::SourceManager sm;
    holyc::Diagnostics diag(&sm);

    int fid = sm.loadFile(input_file);
    if (fid < 0) {
        std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
        return 1;
    }

    holyc::Preprocessor pp(sm, diag, fid);
    while (true) {
        holyc::Token tok = pp.next();
        auto loc = tok.loc;
        std::printf("%s:%u:%u  %-20s  '%.*s'",
            loc.file_path ? loc.file_path : "<unknown>",
            loc.line, loc.col,
            holyc::tokenKindToString(tok.kind),
            (int)tok.text.size(), tok.text.data());

        if (tok.kind == holyc::TokenKind::IntLiteral)
            std::printf("  val=%ld", (long)tok.intVal);
        else if (tok.kind == holyc::TokenKind::FloatLiteral)
            std::printf("  val=%g", tok.floatVal);

        std::putchar('\n');

        if (tok.kind == holyc::TokenKind::Eof)
            break;
    }
    return diag.hasErrors() ? 1 : 0;
}

/// input_file को parse करके ASTPrinter से AST stdout पर print करता है।
static int run_print_ast(const std::string& input_file) {
    holyc::SourceManager sm;
    holyc::Diagnostics diag(&sm);

    int fid = sm.loadFile(input_file);
    if (fid < 0) {
        std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
        return 1;
    }

    holyc::Preprocessor pp(sm, diag, fid);
    holyc::Arena arena;
    holyc::Parser parser(pp, diag, arena);
    holyc::TranslationUnit* tu = parser.parse();

    holyc::ASTPrinter printer(std::cout);
    for (auto* node : tu->decls) {
        printer.print(node);
    }

    return diag.hasErrors() ? 1 : 0;
}

/// Command-line -D macro definitions preprocessor के macro table में inject करता है।
static void injectDefines(holyc::Preprocessor& pp,
                          const std::vector<std::pair<std::string,std::string>>& defines) {
    for (auto& [name, value] : defines) {
        holyc::MacroDef def;
        def.name = name;
        if (!value.empty()) {
            // Value के लिए token create करो
            holyc::Token tok;
            tok.kind = holyc::TokenKind::IntLiteral;
            tok.loc = {};
            bool isInt = true;
            for (char c : value) {
                if (c < '0' || c > '9') { isInt = false; break; }
            }
            if (isInt && !value.empty()) {
                tok.intVal = std::stoll(value);
                tok.uintVal = static_cast<uint64_t>(tok.intVal);
                tok.text = std::string_view();
            } else {
                tok.kind = holyc::TokenKind::Identifier;
            }
            def.body.push_back(tok);
        }
        pp.macroTable().define(name, std::move(def));
    }
}

/// Given prompt के साथ input line read करता है; GNU readline available हो तो उसका use करता है।
static std::string repl_readline_line(const char* prompt) {
#ifdef HCC_HAS_READLINE
    char* line = readline(prompt);
    if (!line) return "";
    if (*line) add_history(line);
    std::string s(line);
    free(line);
    return s;
#else
    std::fputs(prompt, stdout); std::fflush(stdout);
    std::string s;
    if (!std::getline(std::cin, s)) return "";
    return s;
#endif
}

#ifdef HCC_HAS_READLINE
static std::vector<std::string> completion_candidates;

static char* completion_generator(const char* text, int state) {
    static size_t idx;
    if (!state) idx = 0;
    while (idx < completion_candidates.size()) {
        const std::string& candidate = completion_candidates[idx++];
        if (candidate.rfind(text, 0) == 0) {
            return strdup(candidate.c_str());
        }
    }
    return nullptr;
}

static char** holyc_completion(const char* text, int start, int end) {
    (void)start; (void)end;
    completion_candidates.clear();
    static const char* keywords[] = {
        "U0", "U8", "U16", "U32", "U64", "I8", "I16", "I32", "I64",
        "F32", "F64", "Bool", "class", "union", "return", "if", "else",
        "while", "for", "do", "break", "continue", "switch", "case",
        "default", "goto", "typedef", "extern", "static", "inline",
        "sizeof", "typeof", "reg", "noreg", "interrupt", "public", nullptr
    };
    for (int i = 0; keywords[i]; i++)
        completion_candidates.push_back(keywords[i]);
    if (text[0] == ':') {
        completion_candidates.push_back(":help");
        completion_candidates.push_back(":vars");
        completion_candidates.push_back(":funcs");
        completion_candidates.push_back(":type");
        completion_candidates.push_back(":load");
        completion_candidates.push_back(":reset");
        completion_candidates.push_back(":memory");
        completion_candidates.push_back(":import");
        completion_candidates.push_back(":quit");
        completion_candidates.push_back(":time");
    }
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, completion_generator);
}
#endif // HCC_HAS_READLINE

/// REPL multiline accumulator के लिए lines में delimiter depth track करता है।
struct ScanState {
    int brace_depth = 0;
    int paren_depth = 0;
    bool in_string = false;
    bool in_char = false;
    bool in_block_comment = false;
};

/// st को input की एक line scan करके delimiter/comment depth changes से update करता है।
static void scan_line(const std::string& line, ScanState& st) {
    size_t n = line.size();
    for (size_t i = 0; i < n; ++i) {
        char c = line[i];
        char next = (i + 1 < n) ? line[i + 1] : '\0';

        if (st.in_block_comment) {
            if (c == '*' && next == '/') {
                st.in_block_comment = false;
                ++i;
            }
            continue;
        }

        if (st.in_string) {
            if (c == '\\') { ++i; continue; }
            if (c == '"')  { st.in_string = false; }
            continue;
        }

        if (st.in_char) {
            if (c == '\\') { ++i; continue; }
            if (c == '\'') { st.in_char = false; }
            continue;
        }

        if (c == '/' && next == '/') break;

        if (c == '/' && next == '*') {
            st.in_block_comment = true;
            ++i;
            continue;
        }

        if (c == '"') { st.in_string = true; continue; }
        if (c == '\'') { st.in_char = true; continue; }

        if (c == '{') { ++st.brace_depth; }
        else if (c == '}') { --st.brace_depth; }
        else if (c == '(') { ++st.paren_depth; }
        else if (c == ')') { --st.paren_depth; }
    }
}

/// True लौटाता है अगर accumulated input syntactically incomplete है (open braces, strings, आदि)।
static bool needs_more_input(const std::string& line, ScanState& st) {
    scan_line(line, st);
    if (st.brace_depth > 0 || st.paren_depth > 0) return true;
    if (st.in_block_comment || st.in_string || st.in_char) return true;
    if (!line.empty() && line.back() == '\\') return true;
    return false;
}

/// Readline support, REPL commands, और auto-print के साथ interactive REPL run करता है।
static int run_repl() {
    std::puts("HolyC REPL v0.1 \xe2\x80\x94 type :help for commands, :quit or Ctrl+D to exit");

    std::string histPath;
    if (const char* home = getenv("HOME")) {
        namespace fs = std::filesystem;
        fs::path histDir = fs::path(home) / ".local" / "share" / "hcc";
        std::error_code ec;
        fs::create_directories(histDir, ec);
        histPath = (histDir / "repl_history").string();
    }
#ifdef HCC_HAS_READLINE
    if (!histPath.empty()) read_history(histPath.c_str());
    rl_attempted_completion_function = holyc_completion;
#endif

    holyc::SourceManager sm;
    holyc::Diagnostics diag(&sm);
    holyc::Interpreter interp(diag);
    holyc::Arena arena;

    int line_num = 0;
    std::string accumulated;
    ScanState scan_st;

    while (true) {
        const char* prompt = accumulated.empty() ? "hc> " : "..> ";
        std::string line = repl_readline_line(prompt);

        if (line.empty() && accumulated.empty() && std::cin.eof()) break;

        if (accumulated.empty()) {
            if (line == ":quit" || line == ":q") break;

            if (line == ":help") {
                std::puts(
                    "  :help             Show this message\n"
                    "  :quit / :q        Exit the REPL\n"
                    "  :reset            Reset interpreter state (functions/globals)\n"
                    "  :memory           Show heap memory statistics\n"
                    "  :import <name>    Import a stdlib module (e.g. :import strings)\n"
                    "  :load <file>      Load and execute a .HC file\n"
                    "  :type <expr>      Show the type of an expression\n"
                    "  :vars             List all global variables\n"
                    "  :funcs            List all defined functions\n"
                    "  :time <expr>      Evaluate and print elapsed wall-clock time"
                );
                continue;
            }

            if (line == ":vars") {
                if (interp.globals().empty()) {
                    std::puts("  (no globals)");
                } else {
                    for (auto& [name, val] : interp.globals()) {
                        std::printf("  %-20s = ", name.c_str());
                        switch (val.kind) {
                        case holyc::Value::Int:  std::printf("%lld\n", (long long)val.as_int()); break;
                        case holyc::Value::UInt: std::printf("%llu\n", (unsigned long long)val.as_uint()); break;
                        case holyc::Value::Float: std::printf("%g\n", val.as_float()); break;
                        case holyc::Value::Ptr: {
                            auto p = val.ptr;
                            if (p) std::printf("0x%llx\n", (unsigned long long)(uintptr_t)p);
                            else   std::printf("NULL\n");
                            break;
                        }
                        default: std::printf("?\n"); break;
                        }
                    }
                }
                continue;
            }

            if (line == ":funcs") {
                if (interp.functions().empty()) {
                    std::puts("  (no functions)");
                } else {
                    for (auto& [name, fd] : interp.functions()) {
                        std::string sig = "  " + name + "(";
                        bool first = true;
                        for (auto* p : fd->params) {
                            if (!first) sig += ", ";
                            first = false;
                            if (p->type) sig += p->type->toString();
                            sig += " ";
                            sig += p->name;
                        }
                        if (fd->is_vararg) {
                            if (!first) sig += ", ";
                            sig += "..";
                        }
                        sig += ") -> ";
                        sig += (fd->return_type ? fd->return_type->toString() : "U0");
                        std::puts(sig.c_str());
                    }
                }
                continue;
            }

            if (line.rfind(":type ", 0) == 0) {
                std::string expr_str = line.substr(6);
                std::string wrapped = "U0 __type_helper__() { " + expr_str + "; }\n";
                holyc::SourceManager sm2;
                holyc::Diagnostics diag2(&sm2);
                diag2.setSuppressWarnings(true);
                int fid2 = sm2.loadString("<type>", wrapped);
                holyc::Preprocessor pp2(sm2, diag2, fid2);
                holyc::Arena arena2;
                holyc::Parser parser2(pp2, diag2, arena2);
                holyc::TranslationUnit* tu2 = parser2.parse();
                if (!tu2->decls.empty()) {
                    holyc::Sema sema2(diag2, arena2);
                    sema2.analyze(tu2);
                    auto* fd = dynamic_cast<holyc::FuncDecl*>(tu2->decls[0]);
                    if (fd && fd->body && !fd->body->stmts.empty()) {
                        auto* es = dynamic_cast<holyc::ExprStmt*>(fd->body->stmts[0]);
                        if (es && es->expr && es->expr->resolved_type) {
                            std::puts(es->expr->resolved_type->toString().c_str());
                        } else {
                            std::puts("<unknown>");
                        }
                    } else {
                        std::puts("<unknown>");
                    }
                } else {
                    std::puts("<parse error>");
                }
                continue;
            }

            if (line == ":reset") {
                interp.reset();
                std::puts("Interpreter state reset.");
                continue;
            }

            if (line == ":memory") {
                auto [bytes, blocks] = interp.heapStats();
                std::printf("Heap: %zu bytes in %zu blocks\n", bytes, blocks);
                continue;
            }

            if (line.rfind(":import ", 0) == 0) {
                std::string modname = line.substr(8);
                while (!modname.empty() && std::isspace((unsigned char)modname.back()))
                    modname.pop_back();

                bool validName = !modname.empty() && modname.size() <= 64;
                for (char c : modname)
                    if (!std::isalnum((unsigned char)c) && c != '_') { validName = false; break; }

                if (!validName) {
                    std::cerr << "Error: invalid module name '" << modname << "'\n";
                    continue;
                }

                std::vector<std::string> candidates;
                if (const char* home = getenv("HOME")) {
                    candidates.push_back(std::string(home) + "/.local/share/hcc/stdlib/" + modname + ".HC");
                }
                candidates.push_back("/usr/share/holyc/stdlib/" + modname + ".HC");
                candidates.push_back("stdlib/" + modname + ".HC");
                std::string found;
                for (auto& c : candidates) {
                    if (std::filesystem::exists(c)) { found = c; break; }
                }
                if (found.empty()) {
                    std::fprintf(stderr, "import: module '%s' not found\n", modname.c_str());
                } else {
                    int fid2 = sm.loadFile(found);
                    if (fid2 >= 0) {
                        holyc::Preprocessor pp2(sm, diag, fid2);
                        holyc::Parser parser2(pp2, diag, arena);
                        holyc::TranslationUnit* tu2 = parser2.parse();
                        if (!diag.hasErrors()) {
                            holyc::Sema sema2(diag, arena);
                            sema2.analyze(tu2);
                            if (!diag.hasErrors()) {
                                interp.run(tu2);
                                std::printf("Imported '%s'\n", modname.c_str());
                            }
                        }
                        diag.clearErrors();
                    }
                }
                continue;
            }

            if (line.rfind(":load ", 0) == 0) {
                std::string file = line.substr(6);
                int fid = sm.loadFile(file);
                if (fid < 0) {
                    std::fprintf(stderr, "error: cannot open file '%s'\n", file.c_str());
                    continue;
                }
                holyc::Preprocessor pp(sm, diag, fid);
                holyc::Parser parser(pp, diag, arena);
                holyc::TranslationUnit* tu = parser.parse();
                if (!diag.hasErrors()) {
                    holyc::Sema sema(diag, arena);
                    sema.analyze(tu);
                    if (!diag.hasErrors()) {
                        interp.run(tu);
                    }
                }
                continue;
            }

            if (line.rfind(":time ", 0) == 0) {
                std::string expr = line.substr(6);
                std::string tBuf = "<repl:time:" + std::to_string(++line_num) + ">";
                int tfid = sm.loadString(tBuf, expr);
                holyc::Preprocessor tpp(sm, diag, tfid);
                holyc::Parser tparser(tpp, diag, arena);
                holyc::TranslationUnit* ttu = tparser.parse();
                if (ttu && !ttu->decls.empty()) {
                    holyc::Sema tsema(diag, arena);
                    tsema.analyze(ttu);
                    if (!diag.hasErrors()) {
                        auto t0 = std::chrono::steady_clock::now();
                        interp.run(ttu);
                        auto t1 = std::chrono::steady_clock::now();
                        double secs = std::chrono::duration<double>(t1 - t0).count();
                        std::printf("[%.3fs]\n", secs);
                    }
                }
                diag.clearErrors();
                continue;
            }

            if (line.empty()) continue;
        }

        accumulated += line + "\n";

        if (needs_more_input(line, scan_st)) {
            continue; // accumulate करते रहो
        }

        {
            diag.clearErrors();
            std::string bufName = "<repl:" + std::to_string(++line_num) + ">";
            int fid = sm.loadString(bufName, accumulated);
            holyc::Preprocessor pp(sm, diag, fid);
            holyc::Parser parser(pp, diag, arena);
            holyc::TranslationUnit* tu = parser.parse();

            if (tu && !tu->decls.empty()) {
                holyc::Sema sema(diag, arena);
                sema.analyze(tu);

                bool did_print = false;
                if (!diag.hasErrors() && tu->decls.size() == 1) {
                    if (auto* es = dynamic_cast<holyc::ExprStmt*>(tu->decls[0])) {
                        holyc::Value result = interp.eval(es->expr);
                        switch (result.kind) {
                        case holyc::Value::Int:
                            std::printf("= %lld\n", (long long)result.as_int());
                            did_print = true;
                            break;
                        case holyc::Value::UInt:
                            std::printf("= %llu\n", (unsigned long long)result.as_uint());
                            did_print = true;
                            break;
                        case holyc::Value::Float:
                            std::printf("= %g\n", result.as_float());
                            did_print = true;
                            break;
                        case holyc::Value::Ptr:
                            if (result.ptr)
                                std::printf("= 0x%llx\n", (unsigned long long)(uintptr_t)result.ptr);
                            else
                                std::printf("= NULL\n");
                            did_print = true;
                            break;
                        default: break;
                        }
                    }
                }

                if (!did_print) {
                    interp.run(tu);
                }
            }

            accumulated.clear();
            scan_st = ScanState{};
        }
    }

#ifdef HCC_HAS_READLINE
    if (!histPath.empty()) {
        stifle_history(500);
        write_history(histPath.c_str());
    }
#endif

    std::puts("");
    return 0;
}


extern int    g_argc;
extern char** g_argv;

/// Entry point: CLI flags parse करता है, फिर selected compilation mode पर dispatch करता है।
int main(int argc, char *argv[]) {
    g_argc = argc;
    g_argv = argv;

    enum class Mode {
        Compile,
        PrintTokens,
        PrintAst,
        Preprocess,
        SemaOnly,
        Interpret,
        Repl,
        EmitLLVM,
        JIT,
        Format,
        LSP,
    };

    Mode mode = Mode::Compile;
    std::string input_file;
    std::string output_file;
    int optLevel = 0;
    bool optSize = false;
    bool flagDebug = false;
    bool flagC = false;
    bool flagS = false;
    bool flagEmitBC = false;
    bool flagShared = false;
    std::string targetTriple;
    bool noCache = false;
    std::string cacheDir;

    bool warnAsErr = false;
    bool suppressWarn = false;
    int maxErrors = 0;
    bool noColor = false;

    std::vector<std::pair<std::string,std::string>> cmdDefines;
    std::vector<std::string> includePaths;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (std::strcmp(arg, "--print-tokens") == 0) {
            mode = Mode::PrintTokens;
        } else if (std::strcmp(arg, "--print-ast") == 0) {
            mode = Mode::PrintAst;
        } else if (std::strcmp(arg, "--preprocess") == 0) {
            mode = Mode::Preprocess;
        } else if (std::strcmp(arg, "--sema-only") == 0) {
            mode = Mode::SemaOnly;
        } else if (std::strcmp(arg, "--interpret") == 0) {
            mode = Mode::Interpret;
        } else if (std::strcmp(arg, "--emit-llvm") == 0) {
            mode = Mode::EmitLLVM;
        } else if (std::strcmp(arg, "--emit-bc") == 0) {
            flagEmitBC = true;
        } else if (std::strcmp(arg, "--jit") == 0) {
            mode = Mode::JIT;
        } else if (std::strcmp(arg, "--repl") == 0) {
            mode = Mode::Repl;
        } else if (std::strcmp(arg, "--format") == 0) {
            mode = Mode::Format;
        } else if (std::strcmp(arg, "--lsp") == 0) {
            mode = Mode::LSP;
        } else if (std::strcmp(arg, "-shared") == 0) {
            flagShared = true;
        } else if (std::strcmp(arg, "--no-cache") == 0) {
            noCache = true;
        } else if (std::strcmp(arg, "--cache-dir") == 0) {
            if (++i < argc) cacheDir = argv[i];
            else { std::fputs("error: --cache-dir requires an argument\n", stderr); return 1; }
        } else if (std::strcmp(arg, "-o") == 0) {
            if (++i < argc) {
                output_file = argv[i];
            } else {
                std::fputs("error: -o requires an argument\n", stderr);
                return 1;
            }
        } else if (std::strcmp(arg, "-g") == 0 || std::strcmp(arg, "--debug") == 0) {
            flagDebug = true;
        } else if (std::strcmp(arg, "-c") == 0) {
            flagC = true;
        } else if (std::strcmp(arg, "-S") == 0) {
            flagS = true;
        } else if (std::strcmp(arg, "-O0") == 0) {
            optLevel = 0;
        } else if (std::strcmp(arg, "-O1") == 0) {
            optLevel = 1;
        } else if (std::strcmp(arg, "-O2") == 0) {
            optLevel = 2;
        } else if (std::strcmp(arg, "-O3") == 0) {
            optLevel = 3;
        } else if (std::strcmp(arg, "-Os") == 0) {
            optSize = true; optLevel = 2;
        } else if (std::strcmp(arg, "--target") == 0) {
            if (++i < argc) {
                targetTriple = argv[i];
            } else {
                std::fputs("error: --target requires an argument\n", stderr);
                return 1;
            }
        } else if (std::strcmp(arg, "-Werror") == 0) {
            warnAsErr = true;
        } else if (std::strcmp(arg, "-w") == 0) {
            suppressWarn = true;
        } else if (std::strcmp(arg, "--no-color") == 0) {
            noColor = true;
        } else if (std::strncmp(arg, "-fmax-errors=", 13) == 0) {
            maxErrors = std::atoi(arg + 13);
        } else if (std::strcmp(arg, "-D") == 0) {
            if (++i < argc) {
                std::string def = argv[i];
                auto eq = def.find('=');
                if (eq != std::string::npos) {
                    cmdDefines.push_back({def.substr(0, eq), def.substr(eq + 1)});
                } else {
                    cmdDefines.push_back({def, "1"});
                }
            } else {
                std::fputs("error: -D requires an argument\n", stderr);
                return 1;
            }
        } else if (std::strncmp(arg, "-D", 2) == 0 && arg[2] != '\0') {
            std::string def = arg + 2;
            auto eq = def.find('=');
            if (eq != std::string::npos) {
                cmdDefines.push_back({def.substr(0, eq), def.substr(eq + 1)});
            } else {
                cmdDefines.push_back({def, "1"});
            }
        } else if (std::strcmp(arg, "-I") == 0) {
            if (++i < argc) {
                includePaths.push_back(argv[i]);
            } else {
                std::fputs("error: -I requires an argument\n", stderr);
                return 1;
            }
        } else if (std::strncmp(arg, "-I", 2) == 0 && arg[2] != '\0') {
            includePaths.push_back(arg + 2);
        } else if (arg[0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg);
            return 1;
        } else {
            input_file = arg;
        }
    }

    auto configureDiag = [&](holyc::Diagnostics& diag) {
        if (warnAsErr)    diag.setWarningsAsErrors(true);
        if (suppressWarn) diag.setSuppressWarnings(true);
        if (maxErrors)    diag.setMaxErrors(maxErrors);
        if (!noColor && isatty(STDERR_FILENO))
            diag.setColor(true);
    };

    auto configureIncludePaths = [&](holyc::Preprocessor& pp) {
        for (auto& p : includePaths)
            pp.addIncludePath(p);
    };

    switch (mode) {
        case Mode::PrintTokens:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            return run_print_tokens(input_file);

        case Mode::PrintAst:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            return run_print_ast(input_file);

        case Mode::Preprocess:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            return run_preprocess(input_file);

        case Mode::Interpret:
        case Mode::SemaOnly:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            {
                holyc::SourceManager sm;
                holyc::Diagnostics diag(&sm);
                configureDiag(diag);
                int fid = sm.loadFile(input_file);
                if (fid < 0) {
                    std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
                    return 1;
                }
                holyc::Preprocessor pp(sm, diag, fid);
                injectDefines(pp, cmdDefines);
                configureIncludePaths(pp);
                holyc::Arena arena;
                holyc::Parser parser(pp, diag, arena);
                holyc::TranslationUnit* tu = parser.parse();
                if (diag.hasErrors()) return 1;

                holyc::Sema sema(diag, arena);
                sema.analyze(tu);
                if (mode == Mode::SemaOnly) return diag.hasErrors() ? 1 : 0;

                holyc::Interpreter interp(diag);
                return interp.run(tu);
            }

        case Mode::EmitLLVM:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            {
#ifdef HCC_HAS_LLVM
                holyc::SourceManager sm;
                holyc::Diagnostics diag(&sm);
                configureDiag(diag);
                int fid = sm.loadFile(input_file);
                if (fid < 0) {
                    std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
                    return 1;
                }
                holyc::Preprocessor pp(sm, diag, fid);
                injectDefines(pp, cmdDefines);
                configureIncludePaths(pp);
                holyc::Arena arena;
                holyc::Parser parser(pp, diag, arena);
                holyc::TranslationUnit* tu = parser.parse();
                if (diag.hasErrors()) return 1;

                holyc::Sema sema(diag, arena);
                sema.analyze(tu);
                if (diag.hasErrors()) return 1;

                holyc::LLVMCodegen cg(diag, flagDebug);
                auto mod = cg.generate(tu, input_file);
                if (!mod) return 1;
                mod->print(llvm::outs(), nullptr);
                return 0;
#else
                std::fputs("error: LLVM backend not available (compiled without LLVM)\n", stderr);
                return 1;
#endif
            }

        case Mode::JIT:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            {
#ifdef HCC_HAS_LLVM
                holyc::SourceManager sm;
                holyc::Diagnostics diag(&sm);
                configureDiag(diag);
                int fid = sm.loadFile(input_file);
                if (fid < 0) {
                    std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
                    return 1;
                }
                holyc::Preprocessor pp(sm, diag, fid);
                injectDefines(pp, cmdDefines);
                configureIncludePaths(pp);
                holyc::Arena arena;
                holyc::Parser parser(pp, diag, arena);
                holyc::TranslationUnit* tu = parser.parse();
                if (diag.hasErrors()) return 1;

                holyc::Sema sema(diag, arena);
                sema.analyze(tu);
                if (diag.hasErrors()) return 1;

#ifndef __SANITIZE_ADDRESS__
                // JIT cache: ASAN active होने पर skip करो (memory model conflicts)
                if (!noCache) {
                    std::string cd = cacheDir.empty()
                        ? holyc::JITCache::defaultDir()
                        : cacheDir;
                    holyc::JITCache jcache(cd);
                    std::string triple = targetTriple.empty()
                        ? llvm::sys::getDefaultTargetTriple()
                        : targetTriple;
                    const std::string& src = sm.getBuffer(fid);
                    std::string cached = jcache.lookup(src, triple);
                    if (!cached.empty()) {
                        auto buf = llvm::MemoryBuffer::getFile(cached);
                        if (buf) {
                            auto ctx = std::make_unique<llvm::LLVMContext>();
                            auto m = llvm::parseBitcodeFile(*buf.get(), *ctx);
                            if (m) {
                                return holyc::JIT::run(std::move(*m), std::move(ctx));
                            }
                        }
                    }
                    holyc::LLVMCodegen cg(diag, flagDebug);
                    auto mod = cg.generate(tu, input_file);
                    if (!mod) return 1;
                    jcache.store(src, triple, *mod);
                    return holyc::JIT::run(std::move(mod), cg.takeContext());
                } // !noCache
#endif
                holyc::LLVMCodegen cg(diag, flagDebug);
                auto mod = cg.generate(tu, input_file);
                if (!mod) return 1;
                return holyc::JIT::run(std::move(mod), cg.takeContext());
#else
                std::fputs("error: LLVM backend not available (compiled without LLVM)\n", stderr);
                return 1;
#endif
            }

        case Mode::Repl:
            return run_repl();

        case Mode::Compile:
            if (input_file.empty()) {
                print_usage();
                return 0;
            }
            {
#ifdef HCC_HAS_LLVM
                holyc::SourceManager sm;
                holyc::Diagnostics diag(&sm);
                configureDiag(diag);
                int fid = sm.loadFile(input_file);
                if (fid < 0) {
                    std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
                    return 1;
                }
                holyc::Preprocessor pp(sm, diag, fid);
                injectDefines(pp, cmdDefines);
                configureIncludePaths(pp);
                holyc::Arena arena;
                holyc::Parser parser(pp, diag, arena);
                holyc::TranslationUnit* tu = parser.parse();
                if (diag.hasErrors()) return 1;

                holyc::Sema sema(diag, arena);
                sema.analyze(tu);
                if (diag.hasErrors()) return 1;

                holyc::LLVMCodegen cg(diag, flagDebug);
                auto mod = cg.generate(tu, input_file);
                if (!mod) return 1;

                holyc::AOTCompiler::Options aotOpts;
                aotOpts.outputFile = output_file.empty() ? "a.out" : output_file;
                aotOpts.objectOnly = flagC;
                aotOpts.emitAsm = flagS;
                aotOpts.emitBC = flagEmitBC;
                aotOpts.sharedLib = flagShared;
                aotOpts.optLevel = optLevel;
                aotOpts.optSize = optSize;
                aotOpts.debugInfo = flagDebug;
                aotOpts.targetTriple = targetTriple;
                return holyc::AOTCompiler::compile(std::move(mod), cg.getContext(), aotOpts);
#else
                std::fputs("error: LLVM backend not available (compiled without LLVM)\n", stderr);
                return 1;
#endif
            }

        case Mode::Format:
            if (input_file.empty()) {
                std::fputs("error: no input file\n", stderr);
                return 1;
            }
            {
                holyc::SourceManager sm;
                holyc::Diagnostics diag(&sm);
                int fid = sm.loadFile(input_file);
                if (fid < 0) {
                    std::fprintf(stderr, "error: cannot open file '%s'\n", input_file.c_str());
                    return 1;
                }
                holyc::Lexer lexer(sm, fid, diag);
                std::vector<holyc::Token> tokens;
                while (true) {
                    holyc::Token tok = lexer.next();
                    tokens.push_back(tok);
                    if (tok.kind == holyc::TokenKind::Eof) break;
                }
                holyc::Formatter fmt(tokens);
                fmt.setSource(sm.getBuffer(fid));
                std::string formatted = fmt.format();
                if (output_file.empty()) {
                    std::fwrite(formatted.data(), 1, formatted.size(), stdout);
                } else {
                    FILE* f = std::fopen(output_file.c_str(), "w");
                    if (!f) {
                        std::fprintf(stderr, "error: cannot open output file '%s'\n", output_file.c_str());
                        return 1;
                    }
                    std::fwrite(formatted.data(), 1, formatted.size(), f);
                    std::fclose(f);
                }
                return 0;
            }

        case Mode::LSP:
            {
                holyc::lsp::LSPServer server;
                server.run();
                return 0;
            }
    }
}
