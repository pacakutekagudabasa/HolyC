#include "stdlib/defs.HH"

U0 Main()
{
  Print("%d\n", NULL);
  Print("%d\n", TRUE);
  Print("%d\n", FALSE);
  // pi लगभग 3.14 होता है
  Print("%d\n", pi > 3.14 && pi < 3.15);
}
