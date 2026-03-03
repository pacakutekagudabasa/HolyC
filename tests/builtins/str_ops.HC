U0 Main() {
    U8* s = "hello world";
    "%d\n", StrOcc(s, 'l');
    U8* p = StrFirst(s, 'o');
    "%s\n", p;
    U8* q = StrLast(s, 'o');
    "%s\n", q;
}
