#include "stdlib/strings.HC"

U0 Main()
{
    U8* s = StrTrim("  hello  ");
    "%s\n", s;
    Free(s);

    s = StrReplace("hello world", "world", "HolyC");
    "%s\n", s;
    Free(s);

    "%d\n", StrStartsWith("hello", "hel");
    "%d\n", StrEndsWith("hello", "llo");
    "%d\n", StrStartsWith("hello", "world");
}
