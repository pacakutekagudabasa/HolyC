I64 SumAll(I64 n, ..)
{
    I64 i;
    I64 total = 0;
    for (i = 0; i < __vararg_count(); i++)
        total += __vararg_get(i);
    return total;
}

U0 Main()
{
    "%d\n", SumAll(0, 1, 2, 3, 4, 5);
}
