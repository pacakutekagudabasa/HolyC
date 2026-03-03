U0 Main()
{
  I64 arr[5];
  arr[0] = 10;
  arr[1] = 20;
  arr[2] = 30;
  arr[3] = 40;
  arr[4] = 50;

  I64* p = &arr[0];
  Print("%d\n", p[0]);
  Print("%d\n", p[2]);
  Print("%d\n", p[4]);

  // Direct array access करो
  Print("%d\n", arr[1]);
  Print("%d\n", arr[3]);
}
