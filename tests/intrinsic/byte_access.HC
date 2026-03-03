U0 Main()
{
  // 4 bytes pack करो, i32 की तरह read करो
  I64i v;
  v.i64 = 0;
  v.u8[0] = 10;
  v.u8[1] = 0;
  v.u8[2] = 0;
  v.u8[3] = 0;
  // Byte 0 = 10 है, तो LE पर i32[0] = 10 होना चाहिए
  Print("%d\n", v.i32[0]);

  // i16 की तरह read करो
  I64i w;
  w.i64 = 0;
  w.u8[0] = 0;
  w.u8[1] = 1;
  // i16[0] = 256 होना चाहिए (0x0100 little-endian पर)
  Print("%d\n", w.i16[0]);
}
