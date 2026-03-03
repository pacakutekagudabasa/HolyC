# HolyC

A compiler and interpreter for [HolyC](https://holyc-lang.com/), the language
designed by Terry A. Davis for TempleOS. Supports three execution modes — tree-walk
interpretation, LLVM JIT compilation, and ahead-of-time native code generation —
plus a REPL, a formatter, and an LSP server.

## Features

- **Three execution modes** — interpret (`-i`), JIT (`-j`), AOT (`-o`)
- **REPL** — interactive shell with readline, auto-print of expressions, `:reset`, `:memory`, `:time`, `:import`
- **Full preprocessor** — `#define`/`#undef`, `#include`, `#if`/`#ifdef`/`#ifndef`, variadic macros (`__VA_ARGS__`), token paste (`##`), stringification (`#`), `__LINE__`/`__FILE__`/`__DATE__`/`__TIME__`
- **Type system** — `I8`/`I16`/`I32`/`I64`, `U8`/`U16`/`U32`/`U64`, `F32`/`F64`, `Bool`, `U0`
- **Classes** — single inheritance, method dispatch, `this` pointer
- **Standard library** — `vector`, `hashmap`, `queue`, `stack`, `tree`, `regex`, `sort`, `math`, and more (see `stdlib/`)
- **Built-ins** — `Print`, `MAlloc`/`CAlloc`/`Free`, `StrLen`/`StrCmp`/`StrCpy`, `Sin`/`Cos`/`Sqrt`, `Exit`, and 100+ more (see [documentation](website/docs.html#builtins))
- **C interop** — `#import "header.h"` for calling libc and system functions
- **LSP server** — hover, completion, go-to-definition, diagnostics (`--lsp`)
- **GDB integration** — pretty-printers in `tools/holyc_gdb.py`
- **Cross-compilation** — AOT targets `aarch64-linux-gnu` and `x86_64-w64-mingw32` when cross-compilers are present

## Requirements

| Dependency | Version |
|------------|---------|
| C++ compiler | GCC 11+ or Clang 14+ |
| CMake | 3.20+ |
| LLVM | 14–18 (optional; required for JIT/AOT) |
| readline | optional (better REPL experience) |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
make -C build -j$(nproc)
```

The binary is at `build/hcc`. LLVM is auto-detected; if not found, JIT/AOT are
disabled but the interpreter and REPL still work.

**Debug build with ASAN/UBSAN:**
```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug
make -C build_asan -j$(nproc)
```

## Usage

```bash
# Run a file (interpreter)
hcc file.HC

# REPL
hcc

# JIT compile and run
hcc -j file.HC

# AOT compile to native binary
hcc -o output file.HC

# Format source
hcc --format file.HC

# Start LSP server
hcc --lsp
```

### REPL commands

| Command | Description |
|---------|-------------|
| `:reset` | Clear all definitions and restart the environment |
| `:memory` | Show heap allocation statistics |
| `:time` | Toggle per-statement timing |
| `:import <file>` | Load a `.HC` file into the current session |

## Quick example

```c
// hello.HC
U0 Greet(U8 *name) {
    Print("Hello, %s!\n", name);
}

Greet("world");
```

```bash
$ hcc hello.HC
Hello, world!
```

```c
// fib.HC
I64 Fib(I64 n) {
    if (n <= 1) return n;
    return Fib(n - 1) + Fib(n - 2);
}

I64 i;
for (i = 0; i <= 10; i++)
    Print("%d ", Fib(i));
Print("\n");
```

## Standard library

The `stdlib/` directory contains HolyC implementations of common data structures
and algorithms. Include them with `#include`:

```c
#include "stdlib/vector.HC"
#include "stdlib/hashmap.HC"
```

## Tests

```bash
ASAN_OPTIONS=detect_leaks=0 bash tests/run_tests.sh
```

352 tests across interpreter, JIT, and AOT modes.

## License

MIT
