#ifndef __MATH_HC
#define __MATH_HC

#include "defs.HH"

// Math constants — गणितीय स्थिरांक
#define PI    3.14159265358979323846
#define TAU   6.28318530717958647692
#define E_VAL 2.71828182845904523536
#define SQRT2 1.41421356237309504880

// Abs — integers के लिए absolute value
I64 Abs(I64 x)
{
  if (x < 0) return -x;
  return x;
}

// FAbs — floats के लिए absolute value
F64 FAbs(F64 x)
{
  if (x < 0.0) return -x;
  return x;
}

// integers के लिए Min/Max
I64 Min(I64 a, I64 b)
{
  if (a < b) return a;
  return b;
}

I64 Max(I64 a, I64 b)
{
  if (a > b) return a;
  return b;
}

// floats के लिए FMin/FMax
F64 FMin(F64 a, F64 b)
{
  if (a < b) return a;
  return b;
}

F64 FMax(F64 a, F64 b)
{
  if (a > b) return a;
  return b;
}

// Clamp — x को [lo, hi] के बीच clamp करो
I64 Clamp(I64 x, I64 lo, I64 hi)
{
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

// Sign — -1, 0, या 1 return करो
I64 Sign(I64 x)
{
  if (x < 0) return -1;
  if (x > 0) return 1;
  return 0;
}

// IsPow2 — x power of 2 है तो true
I64 IsPow2(I64 x)
{
  return x > 0 && (x & (x - 1)) == 0;
}

// NextPow2 — x से बड़ा या बराबर अगला power of 2
I64 NextPow2(I64 x)
{
  if (x <= 0) return 1;
  x--;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x |= x >> 32;
  return x + 1;
}

// DegToRad / RadToDeg — degrees और radians के बीच convert करो
F64 DegToRad(F64 deg)
{
  return deg * deg2rad;
}

F64 RadToDeg(F64 rad)
{
  return rad * rad2deg;
}

// Trigonometry wrappers — Sin/Cos/Tan के around wrapper functions
F64 SinD(F64 x)  { return Sin(x); }
F64 CosD(F64 x)  { return Cos(x); }
F64 TanD(F64 x)  { return Tan(x); }

// Hypot — right triangle का hypotenuse निकालो
F64 Hypot(F64 a, F64 b) { return Sqrt(a*a + b*b); }

// RoundI64 — float को निकटतम integer में round करो
I64 RoundI64(F64 x) { return (I64)(x + 0.5); }

// ClampF64 — float v को [lo, hi] के बीच clamp करो
F64 ClampF64(F64 v, F64 lo, F64 hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Lerp — a और b के बीच parameter t पर linear interpolation
F64 Lerp(F64 a, F64 b, F64 t)
{
  return a + t * (b - a);
}

// GCD — दो numbers का महत्तम समापवर्तक (Euclidean algorithm से)
I64 GCD(I64 a, I64 b)
{
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  while (b != 0) {
    I64 tmp = b;
    b = a % b;
    a = tmp;
  }
  return a;
}

// IsPrime — n prime है तो 1 return करो, नहीं तो 0
I64 IsPrime(I64 n)
{
  if (n < 2) return 0;
  if (n == 2) return 1;
  if (n % 2 == 0) return 0;
  I64 i = 3;
  while (i * i <= n) {
    if (n % i == 0) return 0;
    i = i + 2;
  }
  return 1;
}

#endif
