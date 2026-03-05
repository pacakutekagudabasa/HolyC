# HolyC

Compiler and interpreter for [HolyC](https://pacakutekagudabasa.github.io/HolyC/docs.html), the language created by Terry A. Davis for [TempleOS](https://templeos.org/).

Supports three execution modes: tree-walk interpretation, LLVM JIT compilation, and ahead-of-time native compilation. The project also includes a REPL, a formatter, and an LSP server.

## Features

* **Execution modes**

  * interpreter (`-i`)
  * LLVM JIT (`-j`)
  * ahead-of-time compilation (`-o`)
* **REPL**

  * interactive shell with readline
  * automatic printing of expression results
  * commands: `:reset`, `:memory`, `:time`, `:import`
* **Preprocessor**

  * `#define`, `#undef`
  * `#include`
  * `#if`, `#ifdef`, `#ifndef`
  * variadic macros (`__VA_ARGS__`)
  * token pasting (`##`)
  * stringification (`#`)
  * `__LINE__`, `__FILE__`, `__DATE__`, `__TIME__`
* **Type system**

  * `I8`, `I16`, `I32`, `I64`
  * `U8`, `U16`, `U32`, `U64`
  * `F32`, `F64`
  * `Bool`, `U0`
* **Classes**

  * single inheritance
  * method dispatch
  * `this` pointer
* **Standard library**

  * data structures and utilities such as `vector`, `hashmap`, `queue`, `stack`, `tree`, `regex`, `sort`, `math`
  * see `stdlib/`
* **Built-ins**

  * examples: `Print`, `MAlloc`, `CAlloc`, `Free`
  * string functions: `StrLen`, `StrCmp`, `StrCpy`
  * math: `Sin`, `Cos`, `Sqrt`
  * `Exit`
  * more functions listed in the documentation
* **C interop**

  * `#import "header.h"` allows calling libc or system functions
* **LSP server**

  * hover information
  * completion
  * go-to-definition
  * diagnostics
  * run with `--lsp`
* **GDB support**

  * pretty-printers available in `tools/holyc_gdb.py`
* **Cross-compilation**

  * AOT builds for `aarch64-linux-gnu` and `x86_64-w64-mingw32` if cross-compilers are installed

## Requirements

| Dependency   | Version                                |
| ------------ | -------------------------------------- |
| C++ compiler | GCC 11+ or Clang 14+                   |
| CMake        | 3.20+                                  |
| LLVM         | 14–18 (optional, required for JIT/AOT) |
| readline     | optional                               |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
make -C build -j$(nproc)
```

The executable will be located at `build/hcc`.

If LLVM is not detected, JIT and AOT modes are disabled. The interpreter and REPL still work.

Debug build with sanitizers:

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug
make -C build_asan -j$(nproc)
```

## Usage

```bash
# Run a file with the interpreter
hcc file.HC

# Start the REPL
hcc

# JIT compile and run
hcc -j file.HC

# Compile to a native binary
hcc -o output file.HC

# Format source
hcc --format file.HC

# Start the LSP server
hcc --lsp
```

### REPL commands

| Command          | Description                                    |
| ---------------- | ---------------------------------------------- |
| `:reset`         | Restart the environment and remove definitions |
| `:memory`        | Display heap allocation statistics             |
| `:time`          | Toggle timing information per statement        |
| `:import <file>` | Load a `.HC` file into the session             |

## Example

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

The `stdlib/` directory contains implementations of common data structures and algorithms written in HolyC.

Include them using `#include`:

```c
#include "stdlib/vector.HC"
#include "stdlib/hashmap.HC"
```

## Tests

```bash
ASAN_OPTIONS=detect_leaks=0 bash tests/run_tests.sh
```

The test suite contains 352 tests covering interpreter, JIT, and AOT execution.

## License

MIT
