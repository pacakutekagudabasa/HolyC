#ifndef __STRINGS_HC
#define __STRINGS_HC

#include "defs.HH"

// StrLen — null-terminated string की length return करो
I64 StrLen(U8* s)
{
  I64 n = 0;
  while (s[n])
    n++;
  return n;
}

// StrCmp — दो strings compare करो, negative/zero/positive return करो
I64 StrCmp(U8* a, U8* b)
{
  I64 i = 0;
  while (a[i] && a[i] == b[i])
    i++;
  return a[i] - b[i];
}

// StrCopy — src को dst में copy करो (bounds check नहीं), dst return करो
U8* StrCopy(U8* dst, U8* src)
{
  I64 i = 0;
  while (src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
  return dst;
}

// StrNCopy — src से अधिकतम n bytes dst में copy करो
U8* StrNCopy(U8* dst, U8* src, I64 n)
{
  I64 i = 0;
  while (i < n && src[i]) {
    dst[i] = src[i];
    i++;
  }
  while (i < n) {
    dst[i] = 0;
    i++;
  }
  return dst;
}

// StrDup — s की copy heap पर allocate करो
U8* StrDup(U8* s)
{
  I64 n = StrLen(s) + 1;
  U8* d = MAlloc(n);
  StrNCopy(d, s, n);
  return d;
}

// StrFindCh — s में ch की पहली occurrence का index ढूंढो, नहीं मिला तो -1
I64 StrFindCh(U8* s, U8 ch)
{
  I64 i = 0;
  while (s[i]) {
    if (s[i] == ch) return i;
    i++;
  }
  return -1;
}

// StrReverse — string को in place reverse करो
U0 StrReverse(U8* s)
{
  I64 l = 0;
  I64 r = StrLen(s) - 1;
  while (l < r) {
    U8 tmp = s[l];
    s[l] = s[r];
    s[r] = tmp;
    l++;
    r--;
  }
}

// I64ToStr — integer को decimal string में convert करो buf में, buf return करो
U8* I64ToStr(I64 val, U8* buf)
{
  I64 i = 0;
  I64 neg = FALSE;
  if (val < 0) {
    neg = TRUE;
    val = -val;
  }
  if (val == 0) {
    buf[i++] = '0';
  } else {
    while (val > 0) {
      buf[i++] = '0' + val % 10;
      val /= 10;
    }
  }
  if (neg) buf[i++] = '-';
  buf[i] = 0;
  StrReverse(buf);
  return buf;
}


// StrFind — haystack में substring needle की पहली occurrence का pointer ढूंढो।
// पहला match मिला तो उसका pointer return करो, नहीं मिला तो 0 (NULL)।
U8* StrFind(U8* hay, U8* needle)
{
    I64 hlen = StrLen(hay);
    I64 nlen = StrLen(needle);
    if (nlen == 0) return hay;
    I64 hi;
    for (hi = 0; hi + nlen <= hlen; hi++) {
        I64 ni;
        I64 ok = 1;
        for (ni = 0; ni < nlen; ni++) {
            if (hay[hi + ni] != needle[ni]) { ok = 0; break; }
        }
        if (ok) return hay + hi;
    }
    return 0;
}

// StrTrim — leading/trailing whitespace हटाकर नई MAlloc'd string return करो
U8* StrTrim(U8* s)
{
    I64 start = 0;
    while (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')
        start++;
    I64 end = StrLen(s);   // original pointer पर StrLen, फिर adjust करो
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' ||
                           s[end-1] == '\n' || s[end-1] == '\r'))
        end--;
    I64 len = end - start;
    U8* out = MAlloc(len + 1);
    I64 i;
    for (i = 0; i < len; i++) out[i] = s[start + i];
    out[len] = 0;
    return out;
}

// StrReplace — s में find की सभी occurrences को repl से replace करो।
// नई MAlloc'd string return करो।
U8* StrReplace(U8* s, U8* find, U8* repl)
{
    I64 flen = StrLen(find);
    I64 rlen = StrLen(repl);
    I64 slen = StrLen(s);
    if (flen == 0) {
        U8* copy = MAlloc(slen + 1);
        I64 k;
        for (k = 0; k <= slen; k++) copy[k] = s[k];
        return copy;
    }
    // occurrences गिनो
    I64 cnt = 0;
    I64 si = 0;
    while (si + flen <= slen) {
        I64 fi;
        I64 match = 1;
        for (fi = 0; fi < flen; fi++) {
            if (s[si + fi] != find[fi]) { match = 0; break; }
        }
        if (match) { cnt++; si += flen; }
        else si++;
    }
    // result के लिए allocate करो और बनाओ
    I64 olen = slen + cnt * (rlen - flen) + 1;
    U8* out = MAlloc(olen);
    I64 di = 0;
    si = 0;
    while (si < slen) {
        I64 match = 0;
        I64 fi;
        if (si + flen <= slen) {
            match = 1;
            for (fi = 0; fi < flen; fi++) {
                if (s[si + fi] != find[fi]) { match = 0; break; }
            }
        }
        if (match) {
            for (fi = 0; fi < rlen; fi++) {
                out[di] = repl[fi];
                di++;
            }
            si += flen;
        } else {
            out[di] = s[si];
            di++;
            si++;
        }
    }
    out[di] = 0;
    return out;
}

// StrStartsWith — s prefix से शुरू होता है तो 1 return करो, नहीं तो 0
Bool StrStartsWith(U8* s, U8* prefix)
{
    I64 i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return FALSE;
        i++;
    }
    return TRUE;
}

// StrEndsWith — s suffix से खत्म होता है तो 1 return करो, नहीं तो 0
Bool StrEndsWith(U8* s, U8* suffix)
{
    I64 slen   = StrLen(s);
    I64 suflen = StrLen(suffix);
    if (suflen > slen) return FALSE;
    I64 off = slen - suflen;
    I64 i;
    for (i = 0; i < suflen; i++) {
        if (s[off + i] != suffix[i]) return FALSE;
    }
    return TRUE;
}

// StrRepeat — s को n बार repeat करके नई MAlloc'd string return करो
U8* StrRepeat(U8* s, I64 n)
{
    I64 slen = StrLen(s);
    I64 total = slen * n;
    U8* out = MAlloc(total + 1);
    I64 i;
    I64 j;
    I64 pos = 0;
    for (i = 0; i < n; i++) {
        for (j = 0; j < slen; j++) {
            out[pos] = s[j];
            pos++;
        }
    }
    out[pos] = 0;
    return out;
}

// I64Str — I64 को नई MAlloc'd string में convert करो
U8* I64Str(I64 n)
{
    return MStrPrint("%d", n);
}

// F64Str — F64 को नई MAlloc'd string में convert करो
U8* F64Str(F64 f)
{
    return MStrPrint("%g", f);
}

// StrPrintf — MStrPrint का alias (format करके नई MAlloc'd string बनाओ)
#define StrPrintf MStrPrint

#endif
