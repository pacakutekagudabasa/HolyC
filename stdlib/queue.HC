#ifndef __QUEUE_HC
#define __QUEUE_HC

#include "defs.HH"

// I64 values का circular queue (FIFO)

class CQueue {
    I64* data;
    I64  cap;
    I64  head;
    I64  tail;
    I64  cnt;
};

U0 QueueInit(CQueue* q)
{
    q->cap  = 16;
    q->head = 0;
    q->tail = 0;
    q->cnt  = 0;
    q->data = MAlloc(sizeof(I64) * q->cap);
}

U0 QueueFree(CQueue* q)
{
    Free(q->data);
    q->data = NULL;
    q->cap  = 0;
    q->head = 0;
    q->tail = 0;
    q->cnt  = 0;
}

U0 QueueEnqueue(CQueue* q, I64 val)
{
    if (q->cnt >= q->cap) {
        if (q->cap <= 0) q->cap = 4;
        if (q->cap > 250000000) {
            "QueueEnqueue: capacity limit reached\n";
            return;
        }
        I64 new_cap = q->cap * 2;
        I64* nd = MAlloc(sizeof(I64) * new_cap);
        I64 i;
        for (i = 0; i < q->cnt; i++)
            nd[i] = q->data[(q->head + i) % q->cap];
        Free(q->data);
        q->data = nd;
        q->head = 0;
        q->tail = q->cnt;
        q->cap  = new_cap;
    }
    q->data[q->tail] = val;
    q->tail = (q->tail + 1) % q->cap;
    q->cnt++;
}

I64 QueueDequeue(CQueue* q)
{
    if (q->cnt == 0) return 0;
    I64 val = q->data[q->head];
    q->head = (q->head + 1) % q->cap;
    q->cnt--;
    return val;
}

I64  QueuePeek(CQueue* q)  { return q->cnt ? q->data[q->head] : 0; }
Bool QueueEmpty(CQueue* q) { return q->cnt == 0; }
I64  QueueSize(CQueue* q)  { return q->cnt; }

#endif
