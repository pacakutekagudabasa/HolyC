// skip-interpret skip-jit skip-aot — formatter mode में test करो
#define LONG_MACRO(x) \
  ((x) * (x) + 1)
U0 Main() { "%d\n", LONG_MACRO(3); }
