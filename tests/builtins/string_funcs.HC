U0 Main()
{
  // StrICmp से case-insensitive compare करो
  Print("%d\n", StrICmp("Hello", "hello"));
  Print("%d\n", StrICmp("abc", "ABC"));
  // Str2I64 से string को integer में बदलो
  Print("%d\n", Str2I64("42"));
  Print("%d\n", Str2I64("-7"));
  // Str2F64 से string को float में बदलो
  Print("%g\n", Str2F64("3.14"));
  // MStrPrint से formatted string बनाओ
  U8* s = MStrPrint("num=%d", 99);
  Print("%s\n", s);
  Free(s);
  // CatPrint से string जोड़ो
  U8* buf = MAlloc(64);
  StrCpy(buf, "hello");
  CatPrint(buf, " %s", "world");
  Print("%s\n", buf);
  Free(buf);
}
