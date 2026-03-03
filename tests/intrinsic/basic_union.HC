U0 Main()
{
  I64i val;
  val.i64 = 0;

  // bytes अलग-अलग write करो
  val.u8[0] = 1;
  val.u8[1] = 2;
  val.u8[2] = 3;
  val.u8[3] = 4;

  // वापस read करो
  Print("%d %d %d %d\n", val.u8[0], val.u8[1], val.u8[2], val.u8[3]);

  // i32 halves write करो
  val.i64 = 0;
  val.i32[0] = 100;
  val.i32[1] = 200;
  Print("%d %d\n", val.i32[0], val.i32[1]);
}
