#define VER 3
#if VER >= 2
U0 Main() { "new\n"; }
#else
U0 Main() { "old\n"; }
#endif
