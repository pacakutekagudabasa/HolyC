// skip-interpret skip-jit skip-aot — formatter mode में test करो
U0 Outer() {
I64 x = 1;
if (x > 0) {
I64 y = 2;
if (y > 0) {
"%d\n", y;
}
}
}
