#ifndef __STACK_HC
#define __STACK_HC

#include "defs.HH"

// I64 values का dynamic stack (LIFO)

class CStack {
    I64* data;
    I64  cap;
    I64  top;
};

U0 StackInit(CStack* s)
{
    s->cap  = 16;
    s->top  = 0;
    s->data = MAlloc(sizeof(I64) * s->cap);
}

U0 StackFree(CStack* s)
{
    Free(s->data);
    s->data = NULL;
    s->cap  = 0;
    s->top  = 0;
}

U0 StackPush(CStack* s, I64 val)
{
    if (s->top >= s->cap) {
        if (s->cap <= 0) s->cap = 4;
        if (s->cap > 250000000) {
            "StackPush: capacity limit reached\n";
            return;
        }
        I64 new_cap = s->cap * 2;
        I64* nd = MAlloc(sizeof(I64) * new_cap);
        I64 i;
        for (i = 0; i < s->top; i++) nd[i] = s->data[i];
        Free(s->data);
        s->data = nd;
        s->cap  = new_cap;
    }
    s->data[s->top++] = val;
}

I64 StackPop(CStack* s)
{
    if (s->top == 0) return 0;
    return s->data[--s->top];
}

I64 StackPeek(CStack* s)
{
    if (s->top == 0) return 0;
    return s->data[s->top - 1];
}

Bool StackEmpty(CStack* s) { return s->top == 0; }
I64  StackSize(CStack* s)  { return s->top; }

#endif
