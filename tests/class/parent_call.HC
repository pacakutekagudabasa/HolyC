class Vehicle
{
  I64 speed;
  U0 SetSpeed(I64 s) { speed = s; }
}

class Car : Vehicle
{
  I64 doors;
  U0 Init(I64 s, I64 d) { Vehicle::SetSpeed(s); doors = d; }
}

U0 Main()
{
  Car c;
  c.Init(100, 4);
  "%d %d\n", c.speed, c.doors;
}
