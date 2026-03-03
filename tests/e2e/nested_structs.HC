// Nested structs: दूसरे structs के pointers वाले structs, field access chains test करो

class CPoint {
    I64 x;
    I64 y;

    I64 Dist2()
    {
        return this.x * this.x + this.y * this.y;
    }

    U0 Set(I64 nx, I64 ny)
    {
        this.x = nx;
        this.y = ny;
    }
}

class CRect {
    CPoint* tl;
    CPoint* br;

    I64 Width()
    {
        return this.br->x - this.tl->x;
    }

    I64 Height()
    {
        return this.br->y - this.tl->y;
    }
}

class CNode {
    I64 val;
    CNode* next;
}

class CShape {
    I64 id;
    CRect* bounds;
}

U0 TestPointMethods()
{
    CPoint p;
    p.x = 3;
    p.y = 4;
    "dist2=%d\n", p.Dist2();
    p.Set(5, 12);
    "after set: x=%d y=%d dist2=%d\n", p.x, p.y, p.Dist2();
}

U0 TestRectWithPointers()
{
    CRect r;
    r.tl = MAlloc(sizeof(CPoint));
    r.br = MAlloc(sizeof(CPoint));
    r.tl->x = 2;
    r.tl->y = 3;
    r.br->x = 10;
    r.br->y = 9;
    I64 w = r.Width();
    I64 h = r.Height();
    I64 area = w * h;
    "tl=(%d,%d) br=(%d,%d)\n", r.tl->x, r.tl->y, r.br->x, r.br->y;
    "w=%d h=%d area=%d\n", w, h, area;
    Free(r.tl);
    Free(r.br);
}

U0 TestLinkedList()
{
    CNode* n1 = MAlloc(sizeof(CNode));
    CNode* n2 = MAlloc(sizeof(CNode));
    CNode* n3 = MAlloc(sizeof(CNode));
    n1->val = 10;
    n1->next = n2;
    n2->val = 20;
    n2->next = n3;
    n3->val = 30;
    n3->next = 0;

    // Chain access करो: n1->next->val, n1->next->next->val
    "n1=%d\n", n1->val;
    "n2=%d\n", n1->next->val;
    "n3=%d\n", n1->next->next->val;
    "n3.next=%d\n", n1->next->next->next;

    Free(n3);
    Free(n2);
    Free(n1);
}

U0 TestDeepNesting()
{
    CShape* s = MAlloc(sizeof(CShape));
    s->id = 7;
    s->bounds = MAlloc(sizeof(CRect));
    s->bounds->tl = MAlloc(sizeof(CPoint));
    s->bounds->br = MAlloc(sizeof(CPoint));
    s->bounds->tl->x = 0;
    s->bounds->tl->y = 0;
    s->bounds->br->x = 640;
    s->bounds->br->y = 480;

    "id=%d\n", s->id;
    "tl=(%d,%d)\n", s->bounds->tl->x, s->bounds->tl->y;
    "br=(%d,%d)\n", s->bounds->br->x, s->bounds->br->y;
    I64 sw = s->bounds->br->x - s->bounds->tl->x;
    I64 sh = s->bounds->br->y - s->bounds->tl->y;
    "size=%dx%d\n", sw, sh;

    Free(s->bounds->tl);
    Free(s->bounds->br);
    Free(s->bounds);
    Free(s);
}

U0 TestArrayOfPointers()
{
    // CPoint के pointers का array बनाओ
    I64 n = 4;
    I64 i;
    CPoint* pts[4];
    for (i = 0; i < n; i++) {
        pts[i] = MAlloc(sizeof(CPoint));
        pts[i]->x = i * 10;
        pts[i]->y = i * 5;
    }
    for (i = 0; i < n; i++)
        "pt[%d]=(%d,%d)\n", i, pts[i]->x, pts[i]->y;
    for (i = 0; i < n; i++)
        Free(pts[i]);
}

U0 Main()
{
    "-- point methods --\n";
    TestPointMethods();
    "-- rect with pointers --\n";
    TestRectWithPointers();
    "-- linked list --\n";
    TestLinkedList();
    "-- deep nesting --\n";
    TestDeepNesting();
    "-- array of pointers --\n";
    TestArrayOfPointers();
}
