U0 Main() {
  I64 val = 42;
  I64* p = &val;
  I64** pp = &p;
  I64*** ppp = &pp;
  "%d\n", ***ppp;
}
Main;
