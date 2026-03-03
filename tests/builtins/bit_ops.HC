U0 Main()
{
    I64 val = 0;
    I64 old;
    old = Bts(&val, 3);
    "bts old=%d val=%d\n", old, val;
    old = Bts(&val, 3);
    "bts2 old=%d val=%d\n", old, val;
    old = Btr(&val, 3);
    "btr old=%d val=%d\n", old, val;
    val = 0xFF;
    old = Btc(&val, 0);
    "btc old=%d val=%d\n", old, val;
}
