#ifndef __REGEX_HC
#define __REGEX_HC

I64 _reg_depth = 0;
I64 _reg_iter  = 0;
#define REG_MAX_DEPTH 500
#define REG_MAX_ITER  100000

// Forward declaration — आगे define होगा
I64 RegMatchHere(U8* p, U8* s);

I64 RegMatchStar(U8 c, U8* p, U8* s)
{
  while (TRUE)
  {
    if (RegMatchHere(p, s)) return 1;
    if (*s == 0 || (*s != c && c != '.')) return 0;
    s++;
  }
  return 0;
}

I64 RegMatchClass(U8* p, U8 c)
{
  // p '[' के बाद वाले char को point करता है, c class में है तो 1 return करो
  I64 n = 0;
  while (p[n] != ']' && p[n] != 0 && n < 4096)
  {
    if (p[n] == c) return 1;
    n++;
  }
  return 0;
}

I64 ClassLen(U8* p)
{
  I64 n = 0;
  while (p[n] != ']' && p[n] != 0 && n < 4096) n++;
  if (p[n] == 0) return 0; // malformed, '[' बंद नहीं हुआ
  return n + 1; // ']' भी include करो
}

I64 RegMatchHere(U8* p, U8* s)
{
  _reg_depth++;
  _reg_iter++;
  if (_reg_depth > REG_MAX_DEPTH) { _reg_depth--; return 0; }
  if (_reg_iter > REG_MAX_ITER) { _reg_depth--; return 0; }
  if (p[0] == 0) { _reg_depth--; return 1; }
  if (p[0] == '$' && p[1] == 0) { I64 r = (*s == 0) ? 1 : 0; _reg_depth--; return r; }
  if (p[0] == '[')
  {
    U8* cls = p + 1;
    I64 clen = ClassLen(cls);
    if (clen == 0) { _reg_depth--; return 0; } // malformed class
    if (*s != 0 && RegMatchClass(cls, *s))
    {
      I64 r;
      if (p[1 + clen] == '*') r = RegMatchStar('.', p + 1 + clen + 1, s);
      else r = RegMatchHere(p + 1 + clen, s + 1);
      _reg_depth--;
      return r;
    }
    _reg_depth--;
    return 0;
  }
  if (p[1] == '*')
  {
    I64 r = RegMatchStar(p[0], p + 2, s);
    _reg_depth--;
    return r;
  }
  if (p[1] == '+')
  {
    if (*s == 0 || (p[0] != '.' && *s != p[0])) { _reg_depth--; return 0; }
    I64 r = RegMatchStar(p[0], p + 2, s + 1);
    _reg_depth--;
    return r;
  }
  if (p[1] == '?')
  {
    if (*s != 0 && (p[0] == '.' || *s == p[0]))
    {
      if (RegMatchHere(p + 2, s + 1)) { _reg_depth--; return 1; }
    }
    I64 r = RegMatchHere(p + 2, s);
    _reg_depth--;
    return r;
  }
  if (p[0] == '.' || *s == p[0])
  {
    I64 r = RegMatchHere(p + 1, s + 1);
    _reg_depth--;
    return r;
  }
  _reg_depth--;
  return 0;
}

I64 RegMatch(U8* pattern, U8* str)
{
  _reg_depth = 0;
  _reg_iter = 0;
  if (pattern[0] == '^') return RegMatchHere(pattern + 1, str);
  U8* s = str;
  while (TRUE)
  {
    if (RegMatchHere(pattern, s)) return 1;
    if (*s == 0) return 0;
    s++;
  }
  return 0;
}

#endif
