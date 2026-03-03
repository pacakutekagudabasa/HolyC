class Animal
{
  I64 legs;
  I64 GetLegs() { return this.legs; }
}

class Dog : Animal
{
  I64 tail;
}

U0 Main()
{
  Dog d;
  d.legs = 4;
  d.tail = 1;
  "%d\n", d.GetLegs();
}
