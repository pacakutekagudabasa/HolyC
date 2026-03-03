U0 Main() {
  U8 sieve[50];
  I64 i;
  I64 j;
  for (i = 0; i < 50; i++) sieve[i] = 1;
  sieve[0] = 0; sieve[1] = 0;
  for (i = 2; i < 50; i++) {
    if (sieve[i]) {
      for (j = i * i; j < 50; j = j + i) sieve[j] = 0;
    }
  }
  for (i = 2; i < 50; i++) {
    if (sieve[i]) "%d\n", i;
  }
}
Main;
