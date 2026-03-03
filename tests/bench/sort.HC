// Benchmark: 10 elements को insertion sort करो, sorted result print करो

U0 InsertionSort(I64* arr, I64 n)
{
    I64 i;
    I64 j;
    I64 key;
    for (i = 1; i < n; i++) {
        key = arr[i];
        j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = key;
    }
}

U0 Main()
{
    I64 arr[10];
    arr[0] = 64;
    arr[1] = 25;
    arr[2] = 12;
    arr[3] = 22;
    arr[4] = 11;
    arr[5] = 90;
    arr[6] = 43;
    arr[7] = 7;
    arr[8] = 55;
    arr[9] = 36;

    "before: ";
    I64 i;
    for (i = 0; i < 10; i++)
        "%d ", arr[i];
    "\n";

    InsertionSort(arr, 10);

    "after:  ";
    for (i = 0; i < 10; i++)
        "%d ", arr[i];
    "\n";
}
