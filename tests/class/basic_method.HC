class CPoint {
  I64 x;
  I64 y;

  I64 Sum()
  {
    return this.x + this.y;
  }

  U0 Set(I64 nx, I64 ny)
  {
    this.x = nx;
    this.y = ny;
  }
}

U0 Main()
{
  CPoint p;
  p.x = 3;
  p.y = 7;
  Print("%d\n", p.Sum());

  p.Set(10, 20);
  Print("%d\n", p.x);
  Print("%d\n", p.y);
  Print("%d\n", p.Sum());
}
