try {
    try {
        throw(10);
    } catch {
        Print("inner=%d\n", __except_code);
    }
    Print("after inner\n");
} catch {
    Print("should not reach outer\n");
}
Print("done\n");
