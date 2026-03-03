U0 Main()
{
  U8 buf[10];
  U8* p = &buf[0];
  U8* q = &buf[4];
  I64 dist = q - p;
  "%d\n", dist;
}
