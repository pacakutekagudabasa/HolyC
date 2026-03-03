#include "stdlib/tree.HC"

U0 Main()
{
  TreeNew();
  I64 root = 0;
  root = TreeInsert(root, 5);
  root = TreeInsert(root, 3);
  root = TreeInsert(root, 7);
  root = TreeInsert(root, 1);
  root = TreeInsert(root, 4);
  TreeWalk(root);
  "\n";
  "%d\n", TreeFind(root, 3);
  "%d\n", TreeFind(root, 9);
  TreeFree();
}
