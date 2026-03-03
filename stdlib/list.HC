#ifndef __LIST_HC
#define __LIST_HC

// CQue — doubly-linked circular queue का एक node
class CQue
{
  CQue* next;
  CQue* prev;
  I64   val;
};

// QueInit — head node को empty list की तरह initialize करो
U0 QueInit(CQue* head)
{
  head->next = head;
  head->prev = head;
  head->val  = 0;
}

// QueInsAfter — node 'n' को 'pos' के बाद insert करो
U0 QueInsAfter(CQue* pos, CQue* n)
{
  n->next    = pos->next;
  n->prev    = pos;
  pos->next->prev = n;
  pos->next  = n;
}

// QueRem — list से node 'n' को हटाओ
U0 QueRem(CQue* n)
{
  n->prev->next = n->next;
  n->next->prev = n->prev;
  n->next = n;
  n->prev = n;
}

// QueCnt — list में nodes गिनो (head को छोड़कर)
I64 QueCnt(CQue* head)
{
  I64 cnt = 0;
  CQue* cur = head->next;
  while (cur != head) {
    cnt++;
    cur = cur->next;
  }
  return cnt;
}

// QuePush — front पर (head के बाद) नई value push करो
U0 QuePush(CQue* head, I64 val)
{
  CQue* n = MAlloc(sizeof(CQue));
  n->val  = val;
  QueInsAfter(head, n);
}

// QuePopFront — front से value निकालो और return करो
I64 QuePopFront(CQue* head)
{
  if (head->next == head) return 0;
  CQue* n = head->next;
  I64   v = n->val;
  QueRem(n);
  Free(n);
  return v;
}

#endif
