U0 Main() {
    I64 x = 0;
    lock { x = 42; }
    "%d\n", x;
}
