#ifndef __SET_HC
#define __SET_HC

#define SET_SIZE 256

I64 set_keys[SET_SIZE];
I64 set_used[SET_SIZE];

U0 SetNew()
{
  I64 i = 0;
  while (i < SET_SIZE) { set_keys[i] = 0; set_used[i] = 0; i++; }
}

I64 SetHash(I64 k)
{
  I64 h = k % SET_SIZE;
  if (h < 0) h = h + SET_SIZE;
  return h;
}

U0 SetAdd(I64 k)
{
  I64 h = SetHash(k);
  I64 i = h;
  while (set_used[i])
  {
    if (set_keys[i] == k) return;
    i = (i + 1) % SET_SIZE;
    if (i == h) return; // set भर गया है
  }
  set_keys[i] = k;
  set_used[i] = 1;
}

I64 SetHas(I64 k)
{
  I64 h = SetHash(k);
  I64 i = h;
  while (set_used[i])
  {
    if (set_keys[i] == k) return 1;
    i = (i + 1) % SET_SIZE;
    if (i == h) return 0;
  }
  return 0;
}

U0 SetRemove(I64 k)
{
  I64 h = SetHash(k);
  I64 i = h;
  while (set_used[i])
  {
    if (set_keys[i] == k) { set_used[i] = 0; return; }
    i = (i + 1) % SET_SIZE;
    if (i == h) return;
  }
}

U0 SetFree()
{
  SetNew();
}

#endif
