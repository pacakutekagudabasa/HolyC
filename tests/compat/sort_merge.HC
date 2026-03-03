U0 Merge(I64* arr, I64 l, I64 m, I64 r) {
  I64 n1 = m - l + 1;
  I64 n2 = r - m;
  I64 L[64];
  I64 R[64];
  I64 i;
  I64 j;
  I64 k;
  for (i = 0; i < n1; i++) L[i] = arr[l + i];
  for (j = 0; j < n2; j++) R[j] = arr[m + 1 + j];
  i = 0; j = 0; k = l;
  while (i < n1 && j < n2) {
    if (L[i] <= R[j]) { arr[k] = L[i]; i++; }
    else { arr[k] = R[j]; j++; }
    k++;
  }
  while (i < n1) { arr[k] = L[i]; i++; k++; }
  while (j < n2) { arr[k] = R[j]; j++; k++; }
}
U0 MergeSort(I64* arr, I64 l, I64 r) {
  if (l < r) {
    I64 m = l + (r - l) / 2;
    MergeSort(arr, l, m);
    MergeSort(arr, m + 1, r);
    Merge(arr, l, m, r);
  }
}
U0 Main() {
  I64 arr[6];
  arr[0] = 38; arr[1] = 27; arr[2] = 43; arr[3] = 3; arr[4] = 9; arr[5] = 82;
  MergeSort(arr, 0, 5);
  I64 i;
  for (i = 0; i < 6; i++) "%d\n", arr[i];
}
Main;
