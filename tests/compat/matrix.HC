U0 MatMul(I64* A, I64* B, I64* C) {
  I64 i;
  I64 j;
  I64 k;
  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      C[i*4+j] = 0;
      for (k = 0; k < 4; k++) C[i*4+j] = C[i*4+j] + A[i*4+k] * B[k*4+j];
    }
  }
}
U0 Main() {
  I64 A[16];
  I64 B[16];
  I64 C[16];
  I64 i;
  for (i = 0; i < 16; i++) A[i] = i + 1;
  for (i = 0; i < 16; i++) B[i] = (i % 4 == i / 4) ? 1 : 0;
  MatMul(A, B, C);
  for (i = 0; i < 16; i++) "%d\n", C[i];
}
Main;
