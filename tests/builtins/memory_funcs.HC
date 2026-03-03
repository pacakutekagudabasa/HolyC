U0 Main()
{
  // ReAlloc से memory बढ़ाओ
  U8* p = MAlloc(8);
  MemSet(p, 0, 8);
  p = ReAlloc(p, 16);
  MemSet(p, 0xAB, 4);
  Print("%d\n", p != 0 ? 1 : 0);
  // MAllocIdent — clone बनाओ
  U8* q = MAllocIdent(p);
  Print("%d\n", q != 0 ? 1 : 0);
  Free(p);
  Free(q);
  // MSize — बस check करो कि non-zero value return करे
  U8* r = MAlloc(32);
  I64 sz = MSize(r);
  Print("%d\n", sz >= 32 ? 1 : 0);
  Free(r);
}
