U0 Main() {
  U8* s = StrNew("hello");
  "%s\n", s;
  U8* t = StrDup("world");
  "%s\n", t;
}
