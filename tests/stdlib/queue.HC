#include "stdlib/queue.HC"

U0 Main()
{
    CQueue q;
    QueueInit(&q);
    QueueEnqueue(&q, 1);
    QueueEnqueue(&q, 2);
    QueueEnqueue(&q, 3);
    "size=%d\n", QueueSize(&q);
    "%d\n", QueueDequeue(&q);
    "%d\n", QueueDequeue(&q);
    QueueEnqueue(&q, 4);
    "size=%d\n", QueueSize(&q);
    "%d\n", QueueDequeue(&q);
    "%d\n", QueueDequeue(&q);
    "empty=%d\n", QueueEmpty(&q);
    QueueFree(&q);
}
