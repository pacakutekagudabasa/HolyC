#include "stdlib/math.HC"

U0 Main()
{
    // Lerp से linear interpolation करो
    "%g\n", Lerp(0.0, 10.0, 0.5);
    "%g\n", Lerp(2.0, 8.0, 0.25);

    // GCD निकालो
    "%d\n", GCD(12, 8);
    "%d\n", GCD(100, 75);
    "%d\n", GCD(7, 13);

    // IsPrime से prime check करो
    "%d\n", IsPrime(2);
    "%d\n", IsPrime(7);
    "%d\n", IsPrime(9);
    "%d\n", IsPrime(1);
}
