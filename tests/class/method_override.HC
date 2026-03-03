class Animal { I64 legs; U0 Speak() { "generic\n"; } }
class Dog : Animal { U0 Speak() { "woof\n"; } }
U0 Main() { Dog d; d.Speak(); }
Main;
