U0 Main() {
  U8 s[16];
  s[0]='h'; s[1]='e'; s[2]='l'; s[3]='l'; s[4]='o'; s[5]=0;
  StrUpr(s);
  "%s\n", s;
  StrLwr(s);
  "%s\n", s;
}
