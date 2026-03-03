// arrays से simple linked list simulation करो
U0 Main() {
  I64 vals[3];
  I64 next[3];
  vals[0] = 10; next[0] = 1;
  vals[1] = 20; next[1] = 2;
  vals[2] = 30; next[2] = -1;
  I64 cur = 0;
  while (cur != -1) {
    "%d\n", vals[cur];
    cur = next[cur];
  }
}
Main;
