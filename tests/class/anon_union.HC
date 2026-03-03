class CVal { union { I64 i; F64 f; }; }
U0 Main() { CVal v; v.i = 0x4000000000000000; "%g\n", v.f; }
