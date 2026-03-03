#define STR(x) #x
U0 Main() {
    U8* s = STR(hello);
    "%s\n", s;
}
