try {
    throw('E');
} catch {
    Print("caught=%d\n", __except_code);
}
Print("after\n");
