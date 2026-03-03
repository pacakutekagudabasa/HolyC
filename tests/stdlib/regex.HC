#include "stdlib/regex.HC"

U0 Main()
{
  "%d\n", RegMatch("hel+o", "hello");
  "%d\n", RegMatch("hel+o", "helo");
  "%d\n", RegMatch("hel+o", "helllo");
  "%d\n", RegMatch("^abc$", "abc");
  "%d\n", RegMatch("^abc$", "abcd");
  "%d\n", RegMatch("a.c", "abc");
  "%d\n", RegMatch("a.c", "ac");
}
