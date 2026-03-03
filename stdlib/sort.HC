#ifndef __SORT_HC
#define __SORT_HC

// HolyC के लिए sort algorithms
// SortI64(I64* arr, I64 n)  — n-element I64 array को ascending order में sort करो (in-place)

U0 _I64Swap(I64* a, I64 i, I64 j)
{
    I64 tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
}

U0 _I64InsSort(I64* arr, I64 lo, I64 hi)
{
    I64 i;
    I64 j;
    I64 key;
    for (i = lo + 1; i <= hi; i++) {
        key = arr[i];
        j = i - 1;
        while (j >= lo && arr[j] > key) {
            arr[j+1] = arr[j];
            j--;
        }
        arr[j+1] = key;
    }
}

I64 _I64Partition(I64* arr, I64 lo, I64 hi)
{
    I64 pivot = arr[hi];
    I64 i = lo - 1;
    I64 j;
    for (j = lo; j < hi; j++) {
        if (arr[j] <= pivot) {
            i++;
            _I64Swap(arr, i, j);
        }
    }
    _I64Swap(arr, i+1, hi);
    return i + 1;
}

U0 _I64QSort(I64* arr, I64 lo, I64 hi)
{
    I64 p;
    while (lo < hi) {
        if (hi - lo < 16) {
            _I64InsSort(arr, lo, hi);
            break;
        }
        p = _I64Partition(arr, lo, hi);
        if (p - lo < hi - p) {
            _I64QSort(arr, lo, p - 1);
            lo = p + 1;
        } else {
            _I64QSort(arr, p + 1, hi);
            hi = p - 1;
        }
    }
}

// n-element I64 array को ascending order में sort करो (in-place)
U0 SortI64(I64* arr, I64 n)
{
    if (n > 1) _I64QSort(arr, 0, n - 1);
}

#endif
