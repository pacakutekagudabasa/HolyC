// Benchmark: Fibonacci(40) iterative method से निकालो
// Result: 102334155 होना चाहिए

I64 Fib(I64 n)
{
    if (n <= 1) return n;
    I64 a = 0;
    I64 b = 1;
    I64 i;
    I64 tmp;
    for (i = 2; i <= n; i++) {
        tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

U0 Main()
{
    I64 result = Fib(40);
    "Fib(40) = %d\n", result;
}
