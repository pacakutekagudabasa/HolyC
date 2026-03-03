// skip-jit — JIT mode skip करो
// skip-aot — AOT mode skip करो
U0 Main() {
  I64 n = ArgC();
  I64 ok = (n > 0);
  "%d\n", ok;
}
