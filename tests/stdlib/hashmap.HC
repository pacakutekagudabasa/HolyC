#include "stdlib/hashmap.HC"

U0 Main()
{
    CHashMap m;
    HashMapInit(&m);

    HashMapSet(&m, "alpha", 1);
    HashMapSet(&m, "beta",  2);
    HashMapSet(&m, "gamma", 3);

    I64 v;
    if (HashMapGet(&m, "beta", &v))
        "beta=%d\n", v;

    HashMapDel(&m, "beta");
    if (!HashMapGet(&m, "beta", &v))
        "beta deleted\n";

    "count=%d\n", HashMapCount(&m);
    HashMapFree(&m);
    "ok\n";
}
