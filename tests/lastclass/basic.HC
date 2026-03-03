class CPoint
{
  I32 x;
  I32 y;
};

U0 Main()
{
  // lastclass CPoint की declaration के बाद उसी को refer करता है
  Print("%d\n", offset(lastclass, x));
  Print("%d\n", offset(lastclass, y));
}
