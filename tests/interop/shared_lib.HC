// skip-interpret — interpreter mode skip करो
// skip-jit — JIT mode skip करो
// Link: -shared — shared library के रूप में compile करो
public I64 Add(I64 a, I64 b) { return a + b; }
public U0 Greet(U8* name) { "Hello, %s!\n", name; }
