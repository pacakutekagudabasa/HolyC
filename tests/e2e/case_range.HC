U0 Main() {
    I64 i;
    for (i = 0; i < 6; i++) {
        switch (i) {
            case 0: "zero\n"; break;
            case 1..3: "low\n"; break;
            case 4..5: "high\n"; break;
        }
    }
}
