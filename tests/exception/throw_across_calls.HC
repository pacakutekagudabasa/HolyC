U0 Thrower() {
    throw(42);
}

try {
    Thrower();
    Print("should not reach\n");
} catch {
    Print("caught=%d\n", __except_code);
}
