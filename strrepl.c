// https://github.com/stephenmathieson/str-replace.c

#include <stdlib.h>
#include <string.h>

static size_t occurrences(const char *needle, const char *haystack)
{
  if (NULL == needle || NULL == haystack)
    return -1;

  char *pos = (char *)haystack;
  size_t i = 0;
  size_t l = strlen(needle);
  if (l == 0)
    return 0;

  while ((pos = strstr(pos, needle)))
  {
    pos += l;
    i++;
  }

  return i;
}

char *strrepl(const char *str, const char *sub, const char *replace)
{
  char *pos = (char *)str;
  int count = occurrences(sub, str);

  if (0 >= count)
    return strdup(str);

  int size = (strlen(str) - (strlen(sub) * count) + strlen(replace) * count) + 1;

  char *result = (char *)malloc(size);
  if (NULL == result)
    return NULL;
  memset(result, '\0', size);
  char *current;
  while ((current = strstr(pos, sub)))
  {
    int len = current - pos;
    strncat(result, pos, len);
    strncat(result, replace, strlen(replace));
    pos = current + strlen(sub);
  }

  if (pos != (str + strlen(str)))
  {
    strncat(result, pos, (str - pos));
  }

  return result;
}
