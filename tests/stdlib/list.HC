#include "stdlib/list.HC"

U0 Main()
{
  CQue head;
  QueInit(&head);

  QuePush(&head, 1);
  QuePush(&head, 2);
  QuePush(&head, 3);

  Print("cnt=%d\n", QueCnt(&head));

  // LIFO order में pop करो (front = सबसे हाल में pushed)
  Print("%d\n", QuePopFront(&head));
  Print("%d\n", QuePopFront(&head));
  Print("%d\n", QuePopFront(&head));
  Print("cnt=%d\n", QueCnt(&head));
}
