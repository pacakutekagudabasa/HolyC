#ifndef __TREE_HC
#define __TREE_HC

// Binary Search Tree — index-based storage में nodes रखते हैं
// Nodes parallel arrays में store होते हैं; index 0 = null/empty sentinel

#define TREE_MAX 1024

I64 tree_key[TREE_MAX];
I64 tree_left[TREE_MAX];
I64 tree_right[TREE_MAX];
I64 tree_size;

U0 TreeNew()
{
  I64 i = 0;
  while (i < TREE_MAX) { tree_key[i] = 0; tree_left[i] = 0; tree_right[i] = 0; i++; }
  tree_size = 1; // index 0 null sentinel है
}

I64 TreeAlloc(I64 k)
{
  if (tree_size >= TREE_MAX) {
    "TreeAlloc: tree is full (max %d nodes)\n", TREE_MAX;
    return 0;
  }
  I64 idx = tree_size;
  tree_size++;
  tree_key[idx] = k;
  tree_left[idx] = 0;
  tree_right[idx] = 0;
  return idx;
}

U0 TreeInsertAt(I64 root, I64 k)
{
  I64 cur = root;
  while (TRUE)
  {
    if (k < tree_key[cur])
    {
      if (tree_left[cur] == 0) { tree_left[cur] = TreeAlloc(k); return; }
      cur = tree_left[cur];
    }
    else if (k > tree_key[cur])
    {
      if (tree_right[cur] == 0) { tree_right[cur] = TreeAlloc(k); return; }
      cur = tree_right[cur];
    }
    else { return; } // duplicate है, छोड़ दो
  }
}

// root index return करो (root delete होने पर बदल सकता है)
I64 TreeInsert(I64 root, I64 k)
{
  if (root == 0) return TreeAlloc(k);
  TreeInsertAt(root, k);
  return root;
}

I64 TreeFind(I64 root, I64 k)
{
  I64 cur = root;
  while (cur != 0)
  {
    if (k == tree_key[cur]) return 1;
    if (k < tree_key[cur]) cur = tree_left[cur];
    else cur = tree_right[cur];
  }
  return 0;
}

// In-order walk: keys को spaces से अलग करके print करो
U0 TreeWalk(I64 node)
{
  if (node == 0) return;
  TreeWalk(tree_left[node]);
  "%d ", tree_key[node];
  TreeWalk(tree_right[node]);
}

U0 TreeFree()
{
  tree_size = 1;
}

#endif
