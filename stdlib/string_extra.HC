#ifndef __STRING_EXTRA_HC
#define __STRING_EXTRA_HC

#include "strings.HC"

// StrTrimInPlace: leading/trailing whitespace in-place हटाओ (string को modify करता है)
U0 StrTrimInPlace(U8* s)
{
  // आगे की whitespace हटाओ
  I64 start = 0;
  while (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')
    start++;
  // end ढूंढो
  I64 len = StrLen(s);
  I64 end = len - 1;
  while (end > start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\n' || s[end] == '\r'))
    end--;
  // Shift करो
  I64 new_len = end - start + 1;
  if (start > 0)
  {
    I64 i = 0;
    while (i < new_len) { s[i] = s[start + i]; i++; }
  }
  s[new_len] = 0;
}

#endif
