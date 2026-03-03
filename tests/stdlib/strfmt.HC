#include "stdlib/strings.HC"

U0 Main()
{
    // StrRepeat से string दोहराओ
    U8* r = StrRepeat("ab", 3);
    "%s\n", r;
    Free(r);

    r = StrRepeat("x", 1);
    "%s\n", r;
    Free(r);

    // I64Str से integer को string में बदलो
    U8* s = I64Str(42);
    "%s\n", s;
    Free(s);

    s = I64Str(-7);
    "%s\n", s;
    Free(s);

    // F64Str से float को string में बदलो
    s = F64Str(3.14);
    "%s\n", s;
    Free(s);

    // StrPrintf से formatted string बनाओ
    s = StrPrintf("hello %s", "world");
    "%s\n", s;
    Free(s);

    s = StrPrintf("val=%d", 99);
    "%s\n", s;
    Free(s);
}
