#define LOG(fmt, ...) Print(fmt, __VA_ARGS__)
U0 Main() {
    LOG("%d %d\n", 1, 2);
}
