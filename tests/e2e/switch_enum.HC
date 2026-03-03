#define RED 1
#define GREEN 2
#define BLUE 3
U0 Main() {
  I64 c = GREEN;
  switch (c) {
    case RED: "red\n"; break;
    case GREEN: "green\n"; break;
    case BLUE: "blue\n"; break;
  }
}
Main;
