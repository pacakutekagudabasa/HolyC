// goto और label tests करो

U0 TestForwardSkip()
{
    "before\n";
    goto done;
    "skipped\n";
done:
    "after\n";
}

U0 TestConditionalBig()
{
    I64 x = 10;
    if (x > 5) goto big;
    "small\n";
    goto finish;
big:
    "big: %d\n", x;
finish:
    "finish\n";
}

U0 TestConditionalSmall()
{
    I64 x = 3;
    if (x > 5) goto big;
    "small: %d\n", x;
    goto finish;
big:
    "big\n";
finish:
    "finish\n";
}

U0 TestLabelV1()
{
    I64 v = 1;
    if (v == 1) goto lbl1;
    "not one\n";
    goto end;
lbl1:
    "one\n";
end:
    "end\n";
}

U0 TestLabelV2()
{
    I64 v = 2;
    if (v == 1) goto lbl1;
    "not one: %d\n", v;
    goto end;
lbl1:
    "one\n";
end:
    "end\n";
}

U0 TestBackwardGoto()
{
    // Backward goto: एक बार loop back करता है
    I64 i = 0;
loop:
    "iter=%d\n", i;
    i++;
    if (i < 2) goto loop;
    "loop_done\n";
}

U0 Main()
{
    "-- forward skip --\n";
    TestForwardSkip();
    "-- conditional big --\n";
    TestConditionalBig();
    "-- conditional small --\n";
    TestConditionalSmall();
    "-- label v=1 --\n";
    TestLabelV1();
    "-- label v=2 --\n";
    TestLabelV2();
    "-- backward goto --\n";
    TestBackwardGoto();
}
