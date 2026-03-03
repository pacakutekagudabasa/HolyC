U0 Main() {
  I64 m[3][4];
  I64 i;
  I64 j;
  for (i = 0; i < 3; i++)
    for (j = 0; j < 4; j++)
      m[i][j] = i * 4 + j;
  "%d\n", m[0][0];
  "%d\n", m[2][3];
}
