// HolyC के सभी operators के comprehensive tests करो

U0 Main()
{
    // --- Arithmetic operators test करो ---
    I64 a = 17;
    I64 b = 5;
    "add:  %d\n", a + b;
    "sub:  %d\n", a - b;
    "mul:  %d\n", a * b;
    "div:  %d\n", a / b;
    "mod:  %d\n", a % b;
    "neg:  %d\n", -b;

    // --- Power operator (backtick) test करो ---
    "2`10: %f\n", 2`10;
    "3`3:  %f\n", 3`3;

    // --- Bitwise operators test करो ---
    I64 x = 0b10110100;
    I64 y = 0b11001010;
    "and:  0x%x\n", x & y;
    "or:   0x%x\n", x | y;
    "xor:  0x%x\n", x ^ y;
    "not:  0x%x\n", ~x & 0xFF;
    "shl:  %d\n", 1 << 4;
    "shr:  %d\n", 128 >> 3;

    // --- Logical operators test करो ---
    "t&&t: %d\n", 1 && 1;
    "t&&f: %d\n", 1 && 0;
    "f||t: %d\n", 0 || 1;
    "f||f: %d\n", 0 || 0;
    "!t:   %d\n", !1;
    "!f:   %d\n", !0;

    // --- Logical XOR (^^) test करो ---
    "1^^0: %d\n", 1 ^^ 0;
    "0^^1: %d\n", 0 ^^ 1;
    "1^^1: %d\n", 1 ^^ 1;
    "0^^0: %d\n", 0 ^^ 0;

    // --- Comparison operators test करो ---
    "5==5: %d\n", 5 == 5;
    "5==6: %d\n", 5 == 6;
    "5!=6: %d\n", 5 != 6;
    "3<4:  %d\n", 3 < 4;
    "4<3:  %d\n", 4 < 3;
    "3<=3: %d\n", 3 <= 3;
    "4>=5: %d\n", 4 >= 5;
    "5>=5: %d\n", 5 >= 5;

    // --- Chained comparison (HolyC-specific, left-to-right) test करो ---
    // (1 < 2) < 3  =>  1 < 3  =>  1 — left-to-right evaluate होता है
    "1<2<3:   %d\n", 1 < 2 < 3;
    // (1 < 2) < 1  =>  1 < 1  =>  0 — false निकलता है
    "1<2<1:   %d\n", 1 < 2 < 1;
    // (5 > 3) > 0  =>  1 > 0  =>  1 — true निकलता है
    "5>3>0:   %d\n", 5 > 3 > 0;
    // 18 <= 25 < 65  =>  (18<=25) < 65  =>  1 < 65  =>  1 — chained comparison है यह
    "18<=25<65: %d\n", 18 <= 25 < 65;

    // --- Ternary operator test करो ---
    I64 big = (a > b) ? 1 : 0;
    "a>b?:  %d\n", big;
    I64 small = (b > a) ? 1 : 0;
    "b>a?:  %d\n", small;

    // --- Assignment operators test करो ---
    I64 c = 10;
    c += 3;  "+=3:  %d\n", c;
    c -= 2;  "-=2:  %d\n", c;
    c *= 4;  "*=4:  %d\n", c;
    c /= 3;  "/=3:  %d\n", c;
    c %= 5;  "%%=5:  %d\n", c;

    // --- Increment / decrement assign (HolyC) test करो ---
    I64 d = 10;
    d ++= 5;  "++=5: %d\n", d;
    d --= 3;  "--=3: %d\n", d;

    // --- Pre/post increment test करो ---
    I64 e = 7;
    "e++:  %d\n", e++;
    "now:  %d\n", e;
    "++e:  %d\n", ++e;

    // --- String output syntax ("fmt", args;) test करो ---
    I64 sum = a + b;
    "sum of %d and %d is %d\n", a, b, sum;

    // --- Floating point arithmetic test करो ---
    F64 f = 3.5;
    F64 g = 1.5;
    "f+g:  %.1f\n", f + g;
    "f*g:  %.1f\n", f * g;
    "f/g:  %.4f\n", f / g;
}
