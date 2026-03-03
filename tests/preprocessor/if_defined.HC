#define FEATURE 1
#if defined(FEATURE)
U0 Main() { "has_feature\n"; }
#else
U0 Main() { "no_feature\n"; }
#endif
