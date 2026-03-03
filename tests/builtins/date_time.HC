U0 Main()
{
    // एक known CDate बनाओ: 2024-01-15 13:30:45
    // sec[0:5] min[6:11] hour[12:16] mday[17:21] mon[22:25] year[26:45] — Terry Davis का bit layout
    I64 d = 45 | (30<<6) | (13<<12) | (15<<17) | (1<<22) | (2024<<26);
    "%D %T\n", d, d;
}
