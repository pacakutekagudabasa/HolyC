U0 Main() {
  I64 i = 0;
loop:
  "%d\n", i;
  i++;
  if (i < 3) goto loop;
  "done\n";
}
