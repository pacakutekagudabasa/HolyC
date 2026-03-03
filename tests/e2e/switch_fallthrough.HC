// Switch statement tests: cases, fallthrough, default — सब test करो

U0 TestBasic()
{
    I64 i;
    for (i = 0; i <= 4; i++) {
        switch (i) {
        case 0:
            "zero\n";
            break;
        case 1:
            "one\n";
            break;
        case 2:
            "two\n";
            break;
        case 3:
            "three\n";
            break;
        default:
            "other: %d\n", i;
            break;
        }
    }
}

U0 TestFallthrough()
{
    // cases के बीच break नहीं: fallthrough होगा
    I64 x = 1;
    switch (x) {
    case 1:
        "case1\n";
    case 2:
        "case2\n";
    case 3:
        "case3\n";
        break;
    case 4:
        "case4\n";
        break;
    }
}

U0 TestDefault()
{
    I64 i;
    for (i = 7; i <= 9; i++) {
        switch (i) {
        case 7:
            "seven\n";
            break;
        case 8:
            "eight\n";
            break;
        default:
            "default: %d\n", i;
            break;
        }
    }
}

U0 TestFallthroughToDefault()
{
    I64 x = 5;
    switch (x) {
    case 1:
        "one\n";
        break;
    case 5:
        "five-then-fall\n";
    default:
        "default-reached\n";
        break;
    }
}

U0 TestNestedSwitch()
{
    I64 outer = 2;
    I64 inner = 3;
    switch (outer) {
    case 1:
        "outer=1\n";
        break;
    case 2:
        "outer=2\n";
        switch (inner) {
        case 3:
            "inner=3\n";
            break;
        default:
            "inner=other\n";
            break;
        }
        break;
    default:
        "outer=other\n";
        break;
    }
}

U0 Main()
{
    "-- basic --\n";
    TestBasic();
    "-- fallthrough --\n";
    TestFallthrough();
    "-- default --\n";
    TestDefault();
    "-- fallthrough to default --\n";
    TestFallthroughToDefault();
    "-- nested switch --\n";
    TestNestedSwitch();
}
