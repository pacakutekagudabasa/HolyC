class A { I64 x; U0 GetX() { "%d\n", x; } }
class B : A { I64 y; }
class C : B { I64 z; }
U0 Main() { C c; c.x = 1; c.y = 2; c.z = 3; c.GetX(); "%d\n", c.y; "%d\n", c.z; }
Main;
