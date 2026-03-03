#include "stdlib/sort.HC"

U0 Main()
{
    I64 arr[8];
    arr[0] = 5; arr[1] = 3; arr[2] = 8; arr[3] = 1;
    arr[4] = 9; arr[5] = 2; arr[6] = 7; arr[7] = 4;
    SortI64(arr, 8);
    I64 i;
    for (i = 0; i < 8; i++) {
        if (i > 0) " ";
        "%d", arr[i];
    }
    "\n";
}
