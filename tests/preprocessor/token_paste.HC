#define CONCAT(a, b) a##b
U0 Main() {
    I64 xy = 42;
    "%d\n", CONCAT(x, y);
}
