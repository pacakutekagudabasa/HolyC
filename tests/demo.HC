#define VERSION 1

I64 Fib(I64 n)
{
    if (n <= 1) return n;
    return Fib(n - 1) + Fib(n - 2);
}

I64 Gcd(I64 a, I64 b)
{
    while (b != 0) {
        I64 t = b;
        b = a % b;
        a = t;
    }
    return a;
}

I64 PowerI(I64 base, I64 exp)
{
    I64 result = 1;
    I64 i;
    for (i = 0; i < exp; i++)
        result *= base;
    return result;
}

I64 Sum(I64 n, I64 start = 0)
{
    I64 total = start;
    I64 i;
    for (i = 1; i <= n; i++)
        total += i;
    return total;
}

U0 Main()
{
    "=== HolyC Demo (version %d) ===\n", VERSION;

    // Types और literals देखो
    I64 x = 42;
    F64 pi = 3.14159;
    Bool flag = TRUE;
    I64 hex_val = 0xFF;
    I64 bin_val = 0b10101010;
    "x=%d  pi=%f  flag=%d\n", x, pi, flag;
    "hex=0x%x  bin=%d\n", hex_val, bin_val;

    // Multi-byte character constant है यह
    I64 ch = 'AB';
    "multi-byte char 'AB' = 0x%x\n", ch;

    // HolyC power operator (backtick) से घात निकालो
    "2^10 = %f\n", 2`10;
    "3^3 = %f\n", 3`3;

    // Logical XOR test करो
    "1 ^^ 0 = %d\n", 1 ^^ 0;
    "1 ^^ 1 = %d\n", 1 ^^ 1;

    // HolyC precedence: & का बंधन + से ज्यादा मजबूत है
    "6 & 3 + 1 = %d (HolyC: (6&3)+1=3, C would give 6&4=4)\n", 6 & 3 + 1;

    // Chained comparison check करो
    I64 age = 25;
    if (18 <= age < 65)
        "age %d: Working age\n", age;

    // Increment-assign operators देखो
    I64 counter = 10;
    counter ++= 5;
    "after ++=5: %d\n", counter;
    counter --= 3;
    "after --=3: %d\n", counter;

    // Ternary expression test करो
    I64 val = (x > 20) ? 100 : 200;
    "ternary: %d\n", val;

    // Fibonacci निकालो
    I64 i;
    for (i = 0; i < 8; i++)
        "Fib(%d)=%d  ", i, Fib(i);
    "\n";

    // GCD निकालो
    "Gcd(48, 18) = %d\n", Gcd(48, 18);

    // Default arguments test करो
    "Sum(10) = %d\n", Sum(10);
    "Sum(10, 100) = %d\n", Sum(10, 100);

    // Dynamic memory allocate करो
    I64 *arr = MAlloc(5 * sizeof(I64));
    for (i = 0; i < 5; i++)
        arr[i] = (i + 1) * 10;
    for (i = 0; i < 5; i++)
        "%d ", arr[i];
    "\n";
    Free(arr);

    // Pointers test करो
    I64 a = 99;
    I64 *p = &a;
    "*p = %d\n", *p;
    *p = 200;
    "after *p=200: a = %d\n", a;

    // Math builtins call करो
    "Sqrt(144) = %f\n", Sqrt(144.0);
    "Abs(-42) = %d\n", Abs(-42);

    // Nested loops में break/continue test करो
    I64 j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 5; j++) {
            if (j == 3) break;
            if (j % 2 == 0) continue;
            "(%d,%d) ", i, j;
        }
    }
    "\n";

    // Switch statement test करो
    I64 day = 3;
    switch (day) {
    case 1: "Mon\n"; break;
    case 2: "Tue\n"; break;
    case 3: "Wed\n"; break;
    default: "Other\n"; break;
    }

    // Do-while loop test करो
    I64 n = 1;
    do {
        n *= 2;
    } while (n < 100);
    "First power of 2 >= 100: %d\n", n;

    // Format specifiers देखो
    "hex: 0x%X  char: '%c'  unsigned: %u\n", 255, 65, 42;
    "padded: [%10d]\n", 42;
    "left:   [%-10d]\n", 42;
    "zero:   [%010d]\n", 42;
    "float:  %.3f\n", 3.14159;

    "=== Done ===\n";
}
