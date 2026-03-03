#ifndef __VECTOR_HC
#define __VECTOR_HC

// CVec — I64 values का dynamic array
class CVec
{
  I64*  data;    // heap पर allocate किया हुआ array
  I64   len;     // अभी कितने elements हैं
  I64   cap;     // allocate की हुई capacity
};

U0 VecInit(CVec* v)
{
  v->cap  = 8;
  v->len  = 0;
  v->data = MAlloc(v->cap * 8);
}

U0 VecFree(CVec* v)
{
  Free(v->data);
  v->data = 0;
  v->len  = 0;
  v->cap  = 0;
}

U0 VecGrow(CVec* v)
{
  if (v->cap <= 0) v->cap = 4;
  if (v->cap > 250000000) {
    "VecGrow: capacity limit reached\n";
    return;
  }
  I64 new_cap = v->cap * 2;
  I64* new_data = MAlloc(new_cap * 8);
  I64 i = 0;
  while (i < v->len) {
    new_data[i] = v->data[i];
    i++;
  }
  Free(v->data);
  v->data = new_data;
  v->cap  = new_cap;
}

U0 VecPush(CVec* v, I64 val)
{
  if (v->len >= v->cap)
    VecGrow(v);
  v->data[v->len] = val;
  v->len++;
}

I64 VecPop(CVec* v)
{
  if (v->len <= 0) return 0;
  v->len--;
  return v->data[v->len];
}

I64 VecGet(CVec* v, I64 i)
{
  if (i < 0 || i >= v->len) return 0;
  return v->data[i];
}

U0 VecSet(CVec* v, I64 i, I64 val)
{
  if (i < 0 || i >= v->len) return;
  v->data[i] = val;
}

I64 VecLen(CVec* v)
{
  return v->len;
}

#endif
