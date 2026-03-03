U0 Main()
{
  // Tan function call करो
  Print("%g\n", Tan(0.0));
  // ATan function call करो
  Print("%g\n", ATan(0.0));
  // Exp(0) = 1 होना चाहिए
  Print("%g\n", Exp(0.0));
  // Log(1) = 0 होना चाहिए
  Print("%g\n", Log(1.0));
  // Pow से घात निकालो
  Print("%g\n", Pow(2.0, 10.0));
  // RandU64 — बस check करो कि काम करता है (determinism के लिए seed)
  SeedRand(42);
  I64 r = RandU64();
  Print("%d\n", r != 0 ? 1 : 0);
}
