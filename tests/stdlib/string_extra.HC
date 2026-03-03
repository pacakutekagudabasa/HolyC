#include "stdlib/strings.HC"
#include "stdlib/string_extra.HC"

U0 Main()
{
  U8 s[32];
  s[0] = ' '; s[1] = ' '; s[2] = 'h'; s[3] = 'e'; s[4] = 'l';
  s[5] = 'l'; s[6] = 'o'; s[7] = ' '; s[8] = ' '; s[9] = 0;
  StrTrimInPlace(s);
  "%s\n", s;
  "%d\n", StrStartsWith("hello world", "hello");
  "%d\n", StrStartsWith("hello world", "world");
  "%d\n", StrEndsWith("hello world", "world");
  "%d\n", StrEndsWith("hello world", "hello");
}
