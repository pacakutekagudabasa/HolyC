#include "stdlib/stack.HC"

U0 Main()
{
    CStack s;
    StackInit(&s);
    StackPush(&s, 10);
    StackPush(&s, 20);
    StackPush(&s, 30);
    "size=%d\n", StackSize(&s);
    "top=%d\n",  StackPeek(&s);
    "%d\n", StackPop(&s);
    "%d\n", StackPop(&s);
    "empty=%d\n", StackEmpty(&s);
    "%d\n", StackPop(&s);
    "empty=%d\n", StackEmpty(&s);
    StackFree(&s);
}
