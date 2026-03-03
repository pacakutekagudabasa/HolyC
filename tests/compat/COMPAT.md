# TempleOS HC Compatibility

## Working Features
- Basic arithmetic, loops, conditionals
- Functions and recursion
- Arrays (fixed-size)
- Classes with fields and methods
- Inheritance (single-level)
- Bit operations

## Stubbed / Limited
- Anonymous unions inside classes (basic)
- Inline asm (interpreter mode ignores/stubs)

## Not Supported (OS-specific)
- Graphics (GrPrint, Sprite, DC)
- Audio (Beep, Sound)
- Task scheduler (Spawn, Kill, Yield)
- Hardware I/O (In, Out)
- Ring-0 execution
