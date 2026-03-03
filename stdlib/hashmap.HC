#ifndef __HASHMAP_HC
#define __HASHMAP_HC

#include "defs.HH"

// Open-addressing hash map: string (U8*) key से I64 value तक
// Struct-array stride की दिक्कत से बचने के लिए parallel arrays use करते हैं।
// Load factor 0.75 से नीचे रखते हैं rehashing से।

#define HASHMAP_INIT_CAP 16
#define HASHMAP_EMPTY    0
#define HASHMAP_DEL      1
#define HASHMAP_FULL     2

class CHashMap {
    U8**  keys;    // key strings
    I64*  vals;    // values
    I64*  state;   // हर slot पर EMPTY/DEL/FULL
    I64   cap;
    I64   cnt;
    I64   dcnt;
};

// Multiplicative hash — Fibonacci hashing variant, 64-bit में
I64 _HMHash(U8* key)
{
    I64 h = 0x517CC1B727220A95;
    while (*key) {
        h ^= (I64)*key++;
        h = (h << 13) | ((I64)((U64)h >> 51));
        h *= 0x9E3779B97F4A7C15;
    }
    if (h < 0) h = -h;
    return h;
}

// index access से दो strings बराबर हैं या नहीं देखो
I64 _HMEq(U8* a, U8* b)
{
    I64 la = StrLen(a);
    I64 lb = StrLen(b);
    if (la != lb) return FALSE;
    I64 i;
    for (i = 0; i < la; i++)
        if (a[i] != b[i]) return FALSE;
    return TRUE;
}

// index access से string का duplicate बनाओ
U8* _HMDup(U8* s)
{
    I64 len = StrLen(s);
    U8* d = MAlloc(len + 1);
    I64 i;
    for (i = 0; i <= len; i++) d[i] = s[i];
    return d;
}

// key के लिए slot index ढूंढो (insert या lookup के लिए)
I64 _HMSlot(CHashMap* m, U8* key)
{
    I64 mask = m->cap - 1;
    I64 h = _HMHash(key) & mask;
    if (h < 0) h = (-h) & mask;
    I64 i = h;
    I64 first_del = -1;
    I64 iters = 0;
    while (iters < m->cap) {
        if (m->state[i] == HASHMAP_EMPTY) {
            if (first_del >= 0) return first_del;
            return i;
        }
        if (m->state[i] == HASHMAP_DEL) {
            if (first_del < 0) first_del = i;
        } else {
            if (_HMEq(m->keys[i], key)) return i;
        }
        i = (i + 1) & mask;
        iters++;
    }
    if (first_del >= 0) return first_del;
    return i & mask;
}

// rehash -> set dependency के लिए forward declaration
U0 HashMapSet(CHashMap* m, U8* key, I64 val);

U0 HashMapRehash(CHashMap* m, I64 new_cap)
{
    if (new_cap > 134217728) {
        "HashMapRehash: requested capacity too large\n";
        return;
    }
    U8**  old_keys  = m->keys;
    I64*  old_vals  = m->vals;
    I64*  old_state = m->state;
    I64   old_cap   = m->cap;

    m->keys  = MAlloc(sizeof(U8*) * new_cap);
    m->vals  = MAlloc(sizeof(I64) * new_cap);
    m->state = CAlloc(sizeof(I64) * new_cap);  // zero = EMPTY
    if (!m->keys || !m->vals || !m->state) {
        "HashMapRehash: allocation failed\n";
        Free(old_keys); Free(old_vals); Free(old_state);
        m->keys = 0; m->vals = 0; m->state = 0;
        m->cap = 0; m->cnt = 0; m->dcnt = 0;
        return;
    }
    m->cap   = new_cap;
    m->cnt   = 0;
    m->dcnt  = 0;

    I64 i;
    for (i = 0; i < old_cap; i++) {
        if (old_state[i] == HASHMAP_FULL)
            HashMapSet(m, old_keys[i], old_vals[i]);
    }
    Free(old_keys);
    Free(old_vals);
    Free(old_state);
}

U0 HashMapInit(CHashMap* m)
{
    m->cap   = HASHMAP_INIT_CAP;
    m->cnt   = 0;
    m->dcnt  = 0;
    m->keys  = MAlloc(sizeof(U8*) * m->cap);
    m->vals  = MAlloc(sizeof(I64) * m->cap);
    m->state = CAlloc(sizeof(I64) * m->cap);
    if (!m->keys || !m->vals || !m->state) {
        "HashMapInit: allocation failed\n";
        m->cap = 0; m->cnt = 0; m->dcnt = 0;
        return;
    }
}

U0 HashMapFree(CHashMap* m)
{
    I64 i;
    for (i = 0; i < m->cap; i++) {
        if (m->state[i] == HASHMAP_FULL && m->keys[i])
            Free(m->keys[i]);
    }
    Free(m->keys);
    Free(m->vals);
    Free(m->state);
    m->keys  = NULL;
    m->vals  = NULL;
    m->state = NULL;
    m->cap = 0;
    m->cnt = 0;
    m->dcnt = 0;
}

U0 HashMapSet(CHashMap* m, U8* key, I64 val)
{
    if ((m->cnt + m->dcnt + 1) * 4 > m->cap * 3)
        HashMapRehash(m, m->cap * 2);
    I64 i = _HMSlot(m, key);
    if (m->state[i] == HASHMAP_FULL) {
        m->vals[i] = val;
    } else {
        if (m->state[i] == HASHMAP_DEL) m->dcnt--;
        m->keys[i]  = _HMDup(key);
        m->vals[i]  = val;
        m->state[i] = HASHMAP_FULL;
        m->cnt++;
    }
}

Bool HashMapGet(CHashMap* m, U8* key, I64* out)
{
    I64 i = _HMSlot(m, key);
    if (m->state[i] == HASHMAP_FULL) {
        *out = m->vals[i];
        return TRUE;
    }
    return FALSE;
}

Bool HashMapDel(CHashMap* m, U8* key)
{
    I64 i = _HMSlot(m, key);
    if (m->state[i] == HASHMAP_FULL) {
        Free(m->keys[i]);
        m->keys[i]  = NULL;
        m->state[i] = HASHMAP_DEL;
        m->cnt--;
        m->dcnt++;
        return TRUE;
    }
    return FALSE;
}

I64 HashMapCount(CHashMap* m) { return m->cnt; }

#endif
