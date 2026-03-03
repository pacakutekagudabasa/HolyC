class CFlags {
    I64 a:4;
    I64 b:4;
    I64 c:8;
    I64 pad:48;
};

U0 Main()
{
    CFlags f;
    f.a = 5;
    f.b = 3;
    f.c = 255;
    "%d %d %d\n", f.a, f.b, f.c;
}
