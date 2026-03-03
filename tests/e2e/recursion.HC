// Recursive algorithms: Fibonacci, Factorial, Ackermann test करो

I64 Fib(I64 n)
{
    if (n <= 1) return n;
    return Fib(n - 1) + Fib(n - 2);
}

I64 Factorial(I64 n)
{
    if (n <= 1) return 1;
    return n * Factorial(n - 1);
}

I64 Ackermann(I64 m, I64 n)
{
    if (m == 0) return n + 1;
    if (n == 0) return Ackermann(m - 1, 1);
    return Ackermann(m - 1, Ackermann(m, n - 1));
}

U0 Main()
{
    // Fibonacci(0..20) print करो
    I64 i;
    for (i = 0; i <= 20; i++)
        "Fib(%d) = %d\n", i, Fib(i);

    // Factorial(0..10) print करो
    for (i = 0; i <= 10; i++)
        "Factorial(%d) = %d\n", i, Factorial(i);

    // Ackermann(3,3) -- known result = 61 होना चाहिए
    "Ackermann(3,3) = %d\n", Ackermann(3, 3);
}
