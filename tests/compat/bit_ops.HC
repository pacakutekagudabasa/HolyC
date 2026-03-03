U0 Main() {
  I64 x = 0xFF;
  "%d\n", x & 0x0F;
  "%d\n", x | 0x100;
  "%d\n", x ^ 0x0F;
  "%d\n", x << 4;
  "%d\n", x >> 4;
  "%d\n", ~x & 0xFFFF;
}
Main;
