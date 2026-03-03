// skip-interpret — interpreter mode skip करो
U0 Main()
{
  I64 x = 42;
  I64 y = 0;
  asm { "movq $1, $0" : "=r"(y) : "r"(x) : }
  "%d\n", y;
}
