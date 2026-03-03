// Full class: methods + inheritance के साथ field access test करो
class Shape
{
  I64 color;
  I64 visible;

  U0 Init(I64 c, I64 v) { this.color = c; this.visible = v; }
  I64 GetColor() { return this.color; }
  I64 IsVisible() { return this.visible; }
};

class Circle : Shape
{
  I64 radius;

  I64 Area() { return this.radius * this.radius; }
};

U0 Main()
{
  Shape s;
  s.Init(7, 1);
  "%d %d\n", s.GetColor(), s.IsVisible();

  Circle c;
  c.color = 3;
  c.visible = 1;
  c.radius = 5;
  "%d %d %d\n", c.color, c.visible, c.Area();
}
