// skip-jit — JIT mode skip करो
// skip-aot — AOT mode skip करो
U0 Main() {
  I64 lo;
  I64 hi;
  asm { "rdtsc" : "=r"(lo), "=r"(hi) : : }
  I64 nonzero = (lo != 0 || hi != 0);
  "%d\n", nonzero;
}
