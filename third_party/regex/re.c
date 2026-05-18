/*
 *
 * Mini regex-module inspired by Rob Pike's regex code described in:
 *
 * http://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html
 *
 * https://github.com/gyrovorbis/tiny-regex-c
 *
 *
 * Supports:
 * ---------
 *   '.'        Dot, matches any character
 *   '^'        Start anchor, matches beginning of string
 *   '$'        End anchor, matches end of string
 *   '*'        Asterisk, match zero or more (greedy)
 *   '+'        Plus, match one or more (greedy)
 *   '?'        Question, match zero or one (non-greedy)
 *   '[abc]'    Character class, match if one of {'a', 'b', 'c'}
 *   '[^abc]'   Inverted class, match if NOT one of {'a', 'b', 'c'}
 *   '[a-zA-Z]' Character ranges, the character set of the ranges { a-z | A-Z }
 *   '\s'       Whitespace, \t \f \r \n \v and spaces
 *   '\S'       Non-whitespace
 *   '\w'       Alphanumeric, [a-zA-Z0-9_]
 *   '\W'       Non-alphanumeric
 *   '\d'       Digits, [0-9]
 *   '\D'       Non-digits
 *   '\b'       Word boundary (zero-width assertion)
 *   '\B'       Not word boundary (zero-width assertion)
 *   '\xXX'     Hex-encoded byte
 *   '|'        Branch Or, e.g. a|A, \w|\s
 *   '{n}'      Match n times
 *   '{n,}'     Match n or more times
 *   '{,m}'     Match m or less times
 *   '{n,m}'    Match n to m times
 *   '(...)'    Group

 * TODO:
 *   - multibyte support (mbtowc, esp. UTF-8. maybe hardcode UTF-8 without libc locale insanity)
 */

#include "re.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef _UNICODE
#include <stdlib.h>
#include <locale.h>
#endif

/* Definitions: */

#define MAX_CHAR_CLASS_LEN 40 /* Max length of character-class buffer in. */
#ifndef CPROVER
#define MAX_REGEXP_OBJECTS 30 /* Max number of regex symbols in expression. */
#else
#define MAX_REGEXP_OBJECTS 8 /* faster formal proofs */
#endif

#define MAX_REGEXP_LEN 70

#ifdef DEBUG
#define DEBUG_P(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_P(...)
#endif

enum regex_type_e
{
  UNUSED,
  DOT,
  BEGIN,
  END,
  QUESTIONMARK,
  STAR,
  PLUS,
  CHAR,
  CHAR_CLASS,
  INV_CHAR_CLASS,
  DIGIT,
  NOT_DIGIT,
  ALPHA,
  NOT_ALPHA,
  WHITESPACE,
  NOT_WHITESPACE,
  WORD_BOUNDARY,
  NOT_WORD_BOUNDARY,
  BRANCH,
  GROUP,
  GROUPEND,
  TIMES,
  TIMES_N,
  TIMES_M,
  TIMES_NM
};

typedef struct regex_t
{
  unsigned short type; /* CHAR, STAR, etc.                      */
  union
  {
    struct
    {
      char ch;    /*      the character itself             */
      char ccl[]; /* Character class - renamed from data[] */
    };
    unsigned char group_size;  /*  OR the number of group patterns. */
    unsigned char group_start; /*  OR for GROUPEND, the start index of the group. */
    struct
    {
      unsigned short n; /* match n times */
      unsigned short m; /* match n to m times */
    };
  } u;
} regex_t;

static unsigned getsize(regex_t *pattern)
{
  unsigned size = sizeof(unsigned short);
  switch (pattern->type)
  {
  case GROUP:
  case GROUPEND:
    size += sizeof(unsigned short) * 2;
    break;
  case TIMES:
  case TIMES_N:
  case TIMES_M:
  case TIMES_NM:
    size += sizeof(unsigned short) * 2;
    break;
  case CHAR:
    size += sizeof(unsigned short) * 2;
    break;
  case CHAR_CLASS:
  case INV_CHAR_CLASS:
    size += sizeof(unsigned short) + 1 + strlen(pattern->u.ccl);
  default:
    break;
  }

  /* Align to 2-byte boundary (minimum for ARM M0+) */
  if (size % 2)
    ++size;

  return size;
}

static re_t getnext(regex_t *pattern)
{
  return (re_t)(((unsigned char *)pattern) + getsize(pattern));
}

static re_t getindex(regex_t *pattern, int index)
{
  for (int i = 1; i <= index; ++i)
    pattern = getnext(pattern);

  return pattern;
}

/* Private function declarations: */
static int matchpattern(regex_t *pattern, const char *text, int *matchlength, int *num_patterns);
static int matchcharclass(char c, const char *str);
static int matchstar(regex_t *p, regex_t *pattern, const char *text, int *matchlength);
static int matchplus(regex_t *p, regex_t *pattern, const char *text, int *matchlength);
static int matchquestion(regex_t *p, regex_t *pattern, const char *text, int *matchlength);
static int matchbranch(regex_t *p, regex_t *pattern, const char *text, int *matchlength);
static int matchtimes(regex_t *p, regex_t *pattern, unsigned short n, const char *text, int *matchlength);
static int matchtimes_n(regex_t *p, regex_t *pattern, unsigned short n, const char *text, int *matchlength);
static int matchtimes_m(regex_t *p, regex_t *pattern, unsigned short m, const char *text, int *matchlength);
static int matchtimes_nm(regex_t *p, regex_t *pattern, unsigned short n, unsigned short m, const char *text, int *matchlength);
static int matchgroup(regex_t *p, const char *text, int *matchlength);
static int matchone(regex_t *p, char c);
static int matchdigit(char c);
static int matchalpha(char c);
static int matchwhitespace(char c);
static int matchmetachar(char c, const char *str);
static int matchrange(char c, const char *str);
static int matchdot(char c);
static int ismetachar(char c);
static int iswordchar(char c);
static int matchwordboundary(const char *text, int is_boundary);
static int hex(char c);

/* Public functions: */
int re_match(const char *pattern, const char *text, int *matchlength)
{
  return re_matchp(re_compile(pattern), text, matchlength);
}

int re_matchp(re_t pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  *matchlength = 0;
  if (pattern != 0)
  {
    if (pattern->type == BEGIN)
    {
      return ((matchpattern(getnext(pattern), text, matchlength, &num_patterns)) ? 0 : -1);
    }
    else
    {
      int idx = -1;

      do
      {
        idx += 1;

        if (matchpattern(pattern, text, matchlength, &num_patterns))
        {
          // empty branch matches null (i.e. ok, but *matchlength == 0)
          if (*matchlength && text[0] == '\0')
            return -1;

          return idx;
        }

        //  Reset match length for the next starting point
        *matchlength = 0;

      } while (*text++ != '\0');
    }
  }
  return -1;
}

re_t re_compile_to(const char *pattern, unsigned char *re_data, unsigned *size)
{
  memset(re_data, 0, *size);

  char c;    /* current char in pattern   */
  int i = 0; /* index into pattern        */
  int j = 0; /* index into re_data    */
  unsigned bytes = *size;
  *size = 0;

  regex_t *re_compiled = (regex_t *)(re_data);

  while (pattern[i] != '\0' && ((char *)re_compiled < (char *)re_data + bytes - sizeof(re_compiled)))
  {
    c = pattern[i];

    switch (c)
    {
    /* Meta-characters: */
    case '^':
    {
      re_compiled->type = BEGIN;
    }
    break;
    case '$':
    {
      re_compiled->type = END;
    }
    break;
    case '.':
    {
      re_compiled->type = DOT;
    }
    break;
    case '|':
    {
      re_compiled->type = BRANCH;
    }
    break;
    case '*':
    {
      if (j > 0)
        re_compiled->type = STAR;
      else // nothing to repeat at position 0
        return 0;
    }
    break;
    case '+':
    {
      if (j > 0)
        re_compiled->type = PLUS;
      else // nothing to repeat at position 0
        return 0;
    }
    break;
    case '?':
    {
      if (j > 0)
        re_compiled->type = QUESTIONMARK;
      else // nothing to repeat at position 0
        return 0;
    }
    break;

    case '(':
    {
      char *p = strrchr(&pattern[i], ')');
      if (p && *(p - 1) != '\\')
      {
        re_compiled->type = GROUP;
        re_compiled->u.group_size = 0;
      }
      /* '(' without matching ')' */
      else
        return 0;
      break;
    }
    case ')':
    {
      int nestlevel = 0;
      int k = j - 1;
      /* search back to next innermost groupstart */
      for (; k >= 0; k--)
      {
        regex_t *cur = getindex((regex_t *)re_data, k);
        if (k < j && cur->type == GROUPEND)
          nestlevel++;
        else if (cur->type == GROUP)
        {
          if (nestlevel == 0)
          {
            cur->u.group_size = j - k - 1;
            re_compiled->type = GROUPEND;
            re_compiled->u.group_start = k; // index of group
            break;
          }
          nestlevel--;
        }
      }
      /* ')' without matching '(' */
      if (k < 0)
        return 0;
      break;
    }
    case '{':
    {
      unsigned short n, m;
      char *p = strchr(&pattern[i + 1], '}');
      re_compiled->type = CHAR;
      re_compiled->u.ch = c;
      if (!p || j == 0) // those invalid quantifiers are compiled as is
      {
        re_compiled->type = CHAR;
        re_compiled->u.ch = c;
      }
      else if (2 != sscanf(&pattern[i], "{%hd,%hd}", &n, &m))
      {
        int o;
        if (!(1 == sscanf(&pattern[i], "{%hd,}%n", &n, &o)) ||
            n == 0 || n > 32767)
        {
          if (1 != sscanf(&pattern[i], "{,%hd}", &m) ||
              *(p - 1) == ',' || m == 0 || m > 32767)
          {
            if (1 == sscanf(&pattern[i], "{%hd}", &n) &&
                n > 0 && n <= 32767)
            {
              re_compiled->type = TIMES;
              re_compiled->u.n = n;
            }
          }
          else
          {
            re_compiled->type = TIMES_M;
            re_compiled->u.m = m;
          }
        }
        else
        {
          re_compiled->type = TIMES_N;
          re_compiled->u.n = n;
        }
      }
      else
      {
        // m must be greater than n, and none of them may be 0 or negative.
        if (!(n == 0 || m == 0 || n > 32767 || m > 32767 || m <= n || *(p - 1) == ','))
        {
          re_compiled->type = TIMES_NM;
          re_compiled->u.n = n;
          re_compiled->u.m = m;
        }
      }
      if (re_compiled->type != CHAR)
        i += (p - &pattern[i]);
      break;
    }
    /* Escaped character-classes (\s \S \w \W \d \D \* \< \>): */
    case '\\':
    {
      if (pattern[i + 1] != '\0')
      {
        /* Skip the escape-char '\\' */
        i += 1;
        /* ... and check the next */
        switch (pattern[i])
        {
        /* Meta-characters: */
        case 'd':
        {
          re_compiled->type = DIGIT;
        }
        break;
        case 'D':
        {
          re_compiled->type = NOT_DIGIT;
        }
        break;
        case 'w':
        {
          re_compiled->type = ALPHA;
        }
        break;
        case 'W':
        {
          re_compiled->type = NOT_ALPHA;
        }
        break;
        case 's':
        {
          re_compiled->type = WHITESPACE;
        }
        break;
        case 'S':
        {
          re_compiled->type = NOT_WHITESPACE;
        }
        break;
        case 'b':
        {
          re_compiled->type = WORD_BOUNDARY;
        }
        break;
        case 'B':
        {
          re_compiled->type = NOT_WORD_BOUNDARY;
        }
        break;
        case '<':
        {
          re_compiled->type = WORD_BOUNDARY;
        }
        break;
        case '>':
        {
          re_compiled->type = NOT_WORD_BOUNDARY;
        }
        break;
        case 'x':
        {
          /* \xXX */
          re_compiled->type = CHAR;
          i++;
          int h = hex(pattern[i]);
          if (h == -1)
          {
            re_compiled->u.ch = '\\';
            re_compiled->type = CHAR;

            re_compiled = getnext(re_compiled);
            re_compiled->u.ch = 'x';
            re_compiled->type = CHAR;

            re_compiled = getnext(re_compiled);
            re_compiled->u.ch = pattern[i];
            re_compiled->type = CHAR;
            break;
          }
          re_compiled->u.ch = h << 4;
          h = hex(pattern[++i]);
          if (h != -1)
            re_compiled->u.ch += h;
          else
          {
            re_compiled->u.ch = '\\';
            re_compiled->type = CHAR;

            re_compiled = getnext(re_compiled);
            re_compiled->u.ch = 'x';
            re_compiled->type = CHAR;

            re_compiled = getnext(re_compiled);
            re_compiled->u.ch = pattern[i - 1];
            re_compiled->type = CHAR;

            if (pattern[i])
            {
              re_compiled = getnext(re_compiled);
              re_compiled->u.ch = pattern[i];
              re_compiled->type = CHAR;
            }
          }
        }
        break;

        /* Escaped character, e.g. '.', '$' or '\\' */
        default:
        {
          re_compiled->type = CHAR;
          re_compiled->u.ch = pattern[i];
        }
        break;
        }
      }
      /* '\\' as last char without previous \\ -> invalid regular expression. */
      else
        return 0;
    }
    break;

    /* Character class: */
    case '[':
    {
      int charIdx = 0;

      /* Look-ahead to determine if negated */
      if (pattern[i + 1] == '^')
      {
        re_compiled->type = INV_CHAR_CLASS;
        i += 1;                  /* Increment i to avoid including '^' in the char-buffer */
        if (pattern[i + 1] == 0) /* incomplete pattern, missing non-zero char after '^' */
        {
          return 0;
        }
      }
      else
      {
        re_compiled->type = CHAR_CLASS;
      }

      /* Copy characters inside [..] to buffer */
      while ((pattern[++i] != ']') && (pattern[i] != '\0')) /* Missing ] */
      {
        if (pattern[i] == '\\')
        {

          if (&re_compiled->u.ccl[charIdx] >= (char *)re_data + bytes)
          {
            return 0;
          }

          if (pattern[i + 1] == 0) /* incomplete pattern, missing non-zero char after '\\' */
          {
            return 0;
          }
          re_compiled->u.ccl[charIdx++] = pattern[i++];
        }
        else if (&re_compiled->u.ccl[charIdx] >= (char *)re_data + bytes)
        {
          return 0;
        }

        re_compiled->u.ccl[charIdx++] = pattern[i];
      }

      if (&re_compiled->u.ccl[charIdx] >= (char *)re_data + bytes)
      {
        return 0;
      }

      /* Null-terminate string end */
      re_compiled->u.ccl[charIdx++] = '\0';
    }
    break;

    case '\0': // EOL (dead-code)
      return 0;

    /* Other characters: */
    default:
    {
      re_compiled->type = CHAR;
      re_compiled->u.ch = c;
    }
    break;
    }
    i += 1;
    j += 1;
    re_compiled = getnext(re_compiled);
  }
  /* 'UNUSED' is a sentinel used to indicate end-of-pattern */
  re_compiled->type = UNUSED;

  /* Calculate final, compressed actual size. */
  *size = (unsigned char *)getnext(re_compiled) - re_data;

  return (re_t)re_data;
}

re_t re_compile(const char *pattern)
{
  /* Align buffer to 4 bytes for ARM M0+ compatibility */
  static regex_t buffer[MAX_REGEXP_OBJECTS] __attribute__((aligned(4)));
  unsigned size = sizeof(buffer);
  return re_compile_to(pattern, (unsigned char *)buffer, &size);
}

unsigned re_size(re_t pattern)
{
  unsigned bytes = 0;

  while (pattern)
  {
    bytes += getsize(pattern);

    if (pattern->type == UNUSED)
      break;

    pattern = getnext(pattern);
  }

  return bytes;
}

int re_compare(re_t pattern1, re_t pattern2)
{
  int result = 0;

  const unsigned totalSize1 = re_size(pattern1);
  const unsigned totalSize2 = re_size(pattern2);

  if (totalSize1 > totalSize2)
    return 1;
  else if (totalSize2 > totalSize1)
    return -1;

  while (pattern1 && pattern2)
  {
    unsigned size1 = getsize(pattern1);
    unsigned size2 = getsize(pattern2);

    if (size1 > size2)
      return 1;
    else if (size2 > size1)
      return -1;

    result = memcmp(pattern1, pattern2, size1);

    if (result != 0)
      return result;

    if (pattern1->type == UNUSED)
      break;

    pattern1 = getnext(pattern1);
    pattern2 = getnext(pattern2);
  }

  return result;
}

#define re_string_cat_fmt_(buff, ...)           \
  do                                            \
  {                                             \
    sprintf(tmp_buff, __VA_ARGS__);             \
    strncat(buff, tmp_buff, count - *size - 1); \
    *size = strlen(buff);                       \
    if (*size >= count)                         \
      return;                                   \
  } while (0)

void re_string(regex_t *pattern, char *buffer, unsigned *size)
{
  unsigned count = *size;
  unsigned char i = 0;
  int j;
  unsigned char group_end = 0;
  char c;
  char tmp_buff[128];

  *size = 0;
  buffer[0] = '\0';

  if (!pattern)
    return;
  while (*size < count)
  {
    if (pattern->type == UNUSED)
    {
      break;
    }

    if (pattern->type == CHAR_CLASS || pattern->type == INV_CHAR_CLASS)
    {
      re_string_cat_fmt_(buffer, "[");
      if (pattern->type == INV_CHAR_CLASS)
        re_string_cat_fmt_(buffer, "^");
      re_string_cat_fmt_(buffer, "%c", pattern->u.ch);
      j = 0;
      while ((c = pattern->u.ccl[j]))
      {
        if (c == ']')
        {
          break;
        }
        re_string_cat_fmt_(buffer, "%c", c);
        ++j;
      }
      re_string_cat_fmt_(buffer, "]");
    }
    else if (pattern->type == CHAR)
    {
      re_string_cat_fmt_(buffer, "%c", pattern->u.ch);
    }
    else if (pattern->type == TIMES)
    {
      re_string_cat_fmt_(buffer, "{%hu}", pattern->u.n);
    }
    else if (pattern->type == TIMES_N)
    {
      re_string_cat_fmt_(buffer, "{%hu,}", pattern->u.n);
    }
    else if (pattern->type == TIMES_M)
    {
      re_string_cat_fmt_(buffer, "{,%hu}", pattern->u.m);
    }
    else if (pattern->type == TIMES_NM)
    {
      re_string_cat_fmt_(buffer, "{%hu,%hu}", pattern->u.n, pattern->u.m);
    }
    else if (pattern->type == GROUP)
    {
      group_end = i + pattern->u.group_size;
      if (group_end >= MAX_REGEXP_OBJECTS)
        return;
      re_string_cat_fmt_(buffer, " (");
    }
    else if (pattern->type == GROUPEND)
    {
      re_string_cat_fmt_(buffer, " )");
    }
    else if (pattern->type == BEGIN)
    {
      re_string_cat_fmt_(buffer, "^");
    }
    else if (pattern->type == END)
    {
      re_string_cat_fmt_(buffer, "$");
    }
    else if (pattern->type == QUESTIONMARK)
    {
      re_string_cat_fmt_(buffer, "?");
    }
    else if (pattern->type == DIGIT)
    {
      re_string_cat_fmt_(buffer, "\\d");
    }
    ++i;
    pattern = getnext(pattern);
  }
}

static int hex(char c)
{
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  else if (c >= '0' && c <= '9')
    return c - '0';
  else
    return -1;
}

/* Private functions: */
static int matchdigit(char c)
{
  return isdigit((unsigned char)c);
}
static int matchalpha(char c)
{
  return isalpha((unsigned char)c);
}
static int matchwhitespace(char c)
{
  return isspace((unsigned char)c);
}
static int matchalphanum(char c)
{
  return ((c == '_') || matchalpha(c) || matchdigit(c));
}
static int matchrange(char c, const char *str)
{
  return ((c != '-') && (str[0] != '\0') && (str[0] != '-') && (str[1] == '-') && (str[2] != '\0') && ((c >= str[0]) && (c <= str[2])));
}
static int matchdot(char c)
{
#if defined(RE_DOT_MATCHES_NEWLINE) && (RE_DOT_MATCHES_NEWLINE == 1)
  (void)c;
  return 1;
#else
  return c != '\n' && c != '\r';
#endif
}
static int ismetachar(char c)
{
  return ((c == 's') || (c == 'S') || (c == 'w') || (c == 'W') || (c == 'd') || (c == 'D') || (c == 'b') || (c == 'B'));
}

static int iswordchar(char c)
{
  return matchalphanum(c);
}

static int matchwordboundary(const char *text, int is_boundary)
{
  /* A word boundary is a position where:
   * - We're at the start/end of string and the adjacent char is a word char, OR
   * - One side is a word char and the other side is not
   */
  int before_is_word = 0;
  int after_is_word = 0;

  /* Check character before current position */
  if (text > (const char *)0 && text[-1] != '\0')
  {
    before_is_word = iswordchar(text[-1]);
  }

  /* Check character at current position */
  if (text[0] != '\0')
  {
    after_is_word = iswordchar(text[0]);
  }

  /* We're at a word boundary if exactly one side is a word character */
  int at_boundary = (before_is_word != after_is_word);

  return is_boundary ? at_boundary : !at_boundary;
}

static int matchmetachar(char c, const char *str)
{
  switch (str[0])
  {
  case 'd':
    return matchdigit(c);
  case 'D':
    return !matchdigit(c);
  case 'w':
    return matchalphanum(c);
  case 'W':
    return !matchalphanum(c);
  case 's':
    return matchwhitespace(c);
  case 'S':
    return !matchwhitespace(c);
  default:
    return (c == str[0]);
  }
}

static int matchcharclass(char c, const char *str)
{
  do
  {
    if (matchrange(c, str))
    {
      DEBUG_P("%c matches %s\n", c, str);
      return 1;
    }
    else if (str[0] == '\\')
    {
      /* Escape-char: increment str-ptr and match on next char */
      str += 1;
      if (matchmetachar(c, str))
      {
        return 1;
      }
      else if ((c == str[0]) && !ismetachar(c))
      {
        return 1;
      }
    }
    else if (c == str[0])
    {
      if (c == '-')
      {
        if ((str[-1] == '\0') || (str[1] == '\0'))
          return 1;
        // else continue
      }
      else
      {
        return 1;
      }
    }
  } while (*str++ != '\0');

  DEBUG_P("%c did not match prev. ccl\n", c);
  return 0;
}

static int matchone(regex_t *p, char c)
{
  DEBUG_P("ONE %d matches %c?\n", p->type, c);
  switch (p->type)
  {
  case DOT:
    return matchdot(c);
  case CHAR_CLASS:
    return matchcharclass(c, (const char *)p->u.ccl);
  case INV_CHAR_CLASS:
    return !matchcharclass(c, (const char *)p->u.ccl);
  case DIGIT:
    return matchdigit(c);
  case NOT_DIGIT:
    return !matchdigit(c);
  case ALPHA:
    return matchalphanum(c);
  case NOT_ALPHA:
    return !matchalphanum(c);
  case WHITESPACE:
    return matchwhitespace(c);
  case NOT_WHITESPACE:
    return !matchwhitespace(c);
  case GROUPEND:
    return 1;
  case BEGIN:
    return 0;
  default:
    return (p->u.ch == c);
  }
}

/* Helper to match one instance of a pattern element (character or group) */
static int matchonepattern(regex_t *p, const char *text, int *length)
{
  if (p->type == GROUP)
  {
    int num_patterns = 0;
    *length = 0;
    regex_t *group_pattern = getnext(p);
    if (matchpattern(group_pattern, text, length, &num_patterns))
    {
      return 1;
    }
    return 0;
  }
  else
  {
    *length = 1;
    return matchone(p, *text);
  }
}

static int matchstar(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  const char *prepoint = text;
  int len;
  unsigned int count = 0;

  /* Greedy: match as many as possible and count them */
  while (text[0] != '\0' && matchonepattern(p, text, &len))
  {
    text += len;
    count++;
  }

  /* Then backtrack and try to match the rest */
  while (1)
  {
    int restlen = 0;
    if (matchpattern(pattern, text, &restlen, &num_patterns))
    {
      *matchlength += (text - prepoint) + restlen;
      return 1;
    }

    /* Backtrack by one instance */
    if (count == 0)
      break;

    /* Re-scan from start to find position after (count-1) matches */
    text = prepoint;
    for (unsigned int j = 0; j < count - 1; j++)
    {
      if (!matchonepattern(p, text, &len))
        break;
      text += len;
    }
    count--;
  }

  return 0;
}

static int matchplus(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  const char *prepoint = text;
  int len;
  unsigned int count = 0;

  /* Match at least one */
  if (!text[0] || !matchonepattern(p, text, &len))
    return 0;

  text += len;
  count = 1;

  /* Greedy: match as many as possible */
  while (text[0] != '\0' && matchonepattern(p, text, &len))
  {
    text += len;
    count++;
  }

  /* Then backtrack and try to match the rest */
  while (count > 0)
  {
    int restlen = 0;
    if (matchpattern(pattern, text, &restlen, &num_patterns))
    {
      *matchlength += (text - prepoint) + restlen;
      return 1;
    }

    /* Backtrack by one instance */
    if (count == 1)
      break;

    /* Re-scan from start to find position after (count-1) matches */
    text = prepoint;
    for (unsigned int j = 0; j < count - 1; j++)
    {
      if (!matchonepattern(p, text, &len))
        break;
      text += len;
    }
    count--;
  }

  return 0;
}

static int matchquestion(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  int len;

  if (p->type == UNUSED)
    return 1;

  /* If the pattern after '?' is UNUSED, be greedy (prefer matching one) */
  if (pattern->type == UNUSED)
  {
    /* Try matching one instance first */
    if (*text && matchonepattern(p, text, &len))
    {
      *matchlength += len;
      return 1;
    }
    /* Else match zero (always succeeds) */
    return 1;
  }

  /* Non-greedy: try without consuming first */
  if (matchpattern(pattern, text, matchlength, &num_patterns))
  {
    return 1;
  }

  /* Try consuming one instance */
  if (*text && matchonepattern(p, text, &len))
  {
    int restlen = *matchlength + len;
    if (matchpattern(pattern, text + len, &restlen, &num_patterns))
    {
      *matchlength = restlen;
      return 1;
    }
  }

  return 0;
}

static int matchtimes(regex_t *p, regex_t *pattern, unsigned short n, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  int len;
  const char *start = text;

  /* Match exactly n times */
  while (*text && i < n && matchonepattern(p, text, &len))
  {
    text += len;
    i++;
  }

  if (i == n)
  {
    /* Now try to match the rest of the pattern */
    int num_patterns = 0;
    int restlen = pre + (text - start);
    if (matchpattern(pattern, text, &restlen, &num_patterns))
    {
      *matchlength = restlen;
      return 1;
    }
  }

  *matchlength = pre;
  return 0;
}

static int matchtimes_n(regex_t *p, regex_t *pattern, unsigned short n, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  int len;
  const char *start = text;

  /* Match n or more times (greedy) */
  while (*text && matchonepattern(p, text, &len))
  {
    text += len;
    i++;
  }

  /* Backtrack and try matching the rest */
  while (i >= n)
  {
    int num_patterns = 0;
    int restlen = pre + (text - start);
    if (matchpattern(pattern, text, &restlen, &num_patterns))
    {
      *matchlength = restlen;
      return 1;
    }

    /* Backtrack one match */
    if (i == n)
      break;

    /* Re-scan from start to find position after (i-1) matches */
    text = start;
    for (unsigned short j = 0; j < i - 1; j++)
    {
      if (!matchonepattern(p, text, &len))
        break;
      text += len;
    }
    i--;
  }

  *matchlength = pre;
  return 0;
}

static int matchtimes_m(regex_t *p, regex_t *pattern, unsigned short m, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  int len;
  const char *start = text;

  /* Match max m times (greedy) */
  while (*text && i < m && matchonepattern(p, text, &len))
  {
    text += len;
    i++;
  }

  /* Backtrack and try matching the rest */
  while (1)
  {
    int num_patterns = 0;
    int restlen = pre + (text - start);
    if (matchpattern(pattern, text, &restlen, &num_patterns))
    {
      *matchlength = restlen;
      return 1;
    }

    /* Backtrack one match */
    if (i == 0)
      break;

    /* Re-scan from start to find position after (i-1) matches */
    text = start;
    for (unsigned short j = 0; j < i - 1; j++)
    {
      if (!matchonepattern(p, text, &len))
        break;
      text += len;
    }
    i--;
  }

  *matchlength = pre;
  return 0;
}

static int matchtimes_nm(regex_t *p, regex_t *pattern, unsigned short n, unsigned short m, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  int len;
  const char *start = text;

  /* Match up to m times (greedy) */
  while (*text && i < m && matchonepattern(p, text, &len))
  {
    text += len;
    i++;
  }

  /* Backtrack and try matching the rest, as long as we have at least n */
  while (i >= n)
  {
    int num_patterns = 0;
    int restlen = pre + (text - start);
    if (matchpattern(pattern, text, &restlen, &num_patterns))
    {
      *matchlength = restlen;
      return 1;
    }

    /* Backtrack one match */
    if (i == n)
      break;

    /* Re-scan from start to find position after (i-1) matches */
    text = start;
    for (unsigned short j = 0; j < i - 1; j++)
    {
      if (!matchonepattern(p, text, &len))
        break;
      text += len;
    }
    i--;
  }

  *matchlength = pre;
  return 0;
}

static int matchbranch(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  const char *prepoint = text;
  if (p->type == UNUSED)
    return 1;
  /* Match the current p (previous) */
  if (*text && matchone(p, *text++))
  {
    (*matchlength)++;
    return 1;
  }
  if (pattern->type == UNUSED)
    // empty branch "0|" allows NULL text
    return 1;
  /* or the next branch */
  if (matchpattern(pattern, prepoint, matchlength, &num_patterns))
    return 1;
  return 0;
}

static int matchgroup(regex_t *p, const char *text, int *matchlength)
{
  int pre = *matchlength;
  int num_patterns = 0, length = pre;
  regex_t *group_pattern = getnext(p);

  DEBUG_P("does GROUP (%u) match %s?\n", (unsigned)p->u.group_size, text);

  /* Match the group contents as a sequence */
  if (matchpattern(group_pattern, text, &length, &num_patterns))
  {
    *matchlength = length;
    DEBUG_P("GROUP matched (len %d)\n", length - pre);
    return 1;
  }

  *matchlength = pre;
  return 0;
}

static inline int ismultimatch(unsigned char type)
{
  switch (type)
  {
  case TIMES:
  case TIMES_N:
  case TIMES_M:
  case TIMES_NM:
    return 1;
  default:
    return 0;
  }
}

/* Helper function to find if there's a BRANCH ahead in the pattern */
static regex_t *findbranch(regex_t *pattern)
{
  regex_t *p = pattern;
  int depth = 0;

  while (p->type != UNUSED)
  {
    if (p->type == GROUP)
      depth++;
    else if (p->type == GROUPEND)
    {
      if (depth == 0)
        return NULL; /* Hit end of current group without finding BRANCH */
      depth--;
    }
    else if (p->type == BRANCH && depth == 0)
      return p; /* Found BRANCH at same nesting level */

    /* Don't look past operators that consume the previous element */
    if (p->type == STAR || p->type == PLUS || p->type == QUESTIONMARK ||
        ismultimatch(p->type))
      return NULL;

    p = getnext(p);
  }

  return NULL;
}

/* Iterative matching */
static int matchpattern(regex_t *pattern, const char *text, int *matchlength, int *num_patterns)
{
  int pre = *matchlength;
  while (1)
  {
    if (pattern->type == UNUSED)
    {
      return 1;
    }

    /* Check for GROUPEND first - it should just return without checking for quantifiers after it.
       Quantifiers after GROUPEND are handled by the GROUP handler in the outer call. */
    if (pattern->type == GROUPEND)
    {
      (*num_patterns)++;
      DEBUG_P("GROUPEND matches %.*s (len %d, patterns %d)\n", *matchlength, text - *matchlength, *matchlength, *num_patterns);
      return 1;
    }

    /* Handle word boundaries - zero-width assertions */
    if (pattern->type == WORD_BOUNDARY || pattern->type == NOT_WORD_BOUNDARY)
    {
      int is_boundary = (pattern->type == WORD_BOUNDARY);
      if (!matchwordboundary(text, is_boundary))
      {
        return 0;
      }
      /* Word boundary matched - continue with next pattern without consuming text */
      (*num_patterns)++;
      pattern = getnext(pattern);
      continue;
    }

    regex_t *next_pattern = getnext(pattern);

    if (next_pattern->type == QUESTIONMARK)
    {
      return matchquestion(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (next_pattern->type == STAR)
    {
      return matchstar(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (next_pattern->type == PLUS)
    {
      DEBUG_P("PLUS match %s?\n", text);
      return matchplus(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (ismultimatch(next_pattern->type))
    {
      int retval = 0;
      if (next_pattern->type == TIMES)
      {
        retval = matchtimes(pattern, getnext(next_pattern), next_pattern->u.n, text, matchlength);
      }
      else if (next_pattern->type == TIMES_N)
      {
        retval = matchtimes_n(pattern, getnext(next_pattern), next_pattern->u.n, text, matchlength);
      }
      else if (next_pattern->type == TIMES_M)
      {
        retval = matchtimes_m(pattern, getnext(next_pattern), next_pattern->u.m, text, matchlength);
      }
      else if (next_pattern->type == TIMES_NM)
      {
        retval = matchtimes_nm(pattern, getnext(next_pattern), next_pattern->u.n, next_pattern->u.m, text,
                               matchlength);
      }

      /* The matchtimes* functions now handle continuing to the rest of the pattern */
      return retval;
    }
    else if (next_pattern->type == BRANCH)
    {
      return matchbranch(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (pattern->type == GROUP)
    {
      const int beforelen = *matchlength;
      regex_t *after_groupend = getindex(pattern, pattern->u.group_size + 2);

      /* Check if there's a quantifier after the GROUPEND */
      if (after_groupend->type == STAR)
      {
        return matchstar(pattern, getnext(after_groupend), text, matchlength);
      }
      else if (after_groupend->type == PLUS)
      {
        return matchplus(pattern, getnext(after_groupend), text, matchlength);
      }
      else if (after_groupend->type == QUESTIONMARK)
      {
        return matchquestion(pattern, getnext(after_groupend), text, matchlength);
      }
      else if (ismultimatch(after_groupend->type))
      {
        int retval = 0;
        if (after_groupend->type == TIMES)
        {
          retval = matchtimes(pattern, getnext(after_groupend), after_groupend->u.n, text, matchlength);
        }
        else if (after_groupend->type == TIMES_N)
        {
          retval = matchtimes_n(pattern, getnext(after_groupend), after_groupend->u.n, text, matchlength);
        }
        else if (after_groupend->type == TIMES_M)
        {
          retval = matchtimes_m(pattern, getnext(after_groupend), after_groupend->u.m, text, matchlength);
        }
        else if (after_groupend->type == TIMES_NM)
        {
          retval = matchtimes_nm(pattern, getnext(after_groupend), after_groupend->u.n, after_groupend->u.m, text, matchlength);
        }

        /* The matchtimes* functions now handle everything */
        return retval;
      }
      else
      {
        /* No quantifier after group - match it normally */
        const int retval = matchgroup(pattern, text, matchlength);

        if (!retval)
          return 0;
        else
        {
          text += (*matchlength - beforelen);
          pre = *matchlength;
          (*num_patterns) += pattern->u.group_size + 2;
          pattern = after_groupend;
          /* Always continue to check what comes after the group */
          continue;
        }
      }
    }
    else if ((pattern->type == END) && next_pattern->type == UNUSED)
    {
      return (text[0] == '\0');
    }

    /* Before trying to match, check if there's a BRANCH ahead */
    /* This handles the case where the left branch fails to match */
    regex_t *branch = findbranch(next_pattern);

    (*matchlength)++;
    (*num_patterns)++;

    if (text[0] == '\0')
    {
      *matchlength = pre;
      /* If we have a branch, try the alternate */
      if (branch)
      {
        *matchlength = pre;
        *num_patterns = 0;
        return matchpattern(getnext(branch), text - (pre > 0 ? pre : 0), matchlength, num_patterns);
      }
      return 0;
    }

    if (!matchone(pattern, *text))
    {
      *matchlength = pre;
      /* If we have a branch, try the alternate */
      if (branch)
      {
        *matchlength = pre;
        *num_patterns = 0;
        return matchpattern(getnext(branch), text, matchlength, num_patterns);
      }
      return 0;
    }

    text++;
    pattern = next_pattern;
  }

  *matchlength = pre;
  return 0;
}