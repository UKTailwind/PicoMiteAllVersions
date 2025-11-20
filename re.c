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
 *   '\xXX'     Hex-encoded byte
 *   '|'        Branch Or, e.g. a|A, \w|\s
 *   '{n}'      Match n times
 *   '{n,}'     Match n or more times
 *   '{,m}'     Match m or less times
 *   '{n,m}'    Match n to m times

 * FIXME:
 *   '(...)'    Group
 *
 * TODO:
 *   - multibyte support (mbtowc, esp. UTF-8. maybe hardcode UTF-8 without libc locale insanity)
 *   - \b word boundary support
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
    /* Original: sizeof(ushort) + strlen(&data[-1])
     * data[-1] is ch, so strlen counts: ch + ccl string (without null)
     * Example: ch='a', ccl="bc\0" -> strlen("abc")=3
     * New: sizeof(ushort) + 1 (for ch) + strlen(ccl)
     * ccl="bc\0" -> 1 + strlen("bc") = 1 + 2 = 3
     * Both give same result */
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
static int matchtimes(regex_t *p, unsigned short n, const char *text, int *matchlength);
static int matchtimes_n(regex_t *p, unsigned short n, const char *text, int *matchlength);
static int matchtimes_m(regex_t *p, unsigned short m, const char *text, int *matchlength);
static int matchtimes_nm(regex_t *p, unsigned short n, unsigned short m, const char *text, int *matchlength);
static int matchgroup(regex_t *p, const char *text, int *matchlength);
static int matchone(regex_t *p, char c);
static int matchdigit(char c);
static int matchalpha(char c);
static int matchwhitespace(char c);
static int matchmetachar(char c, const char *str);
static int matchrange(char c, const char *str);
static int matchdot(char c);
static int ismetachar(char c);
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
      // re_compiled->u.data_len = 1;
      if (!p || j == 0) // those invalid quantifiers are compiled as is
      {                 // (in python and perl)
        re_compiled->type = CHAR;
        re_compiled->u.ch = c;
        // re_compiled->u.data_len = 1;
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
    /* Escaped character-classes (\s \S \w \W \d \D \*): */
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
      /* Changed: charIdx now starts at 0 for ccl[0], was -1 for data[-1] */
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
            // fputs("exceeded internal buffer!\n", stderr);
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
          // fputs("exceeded internal buffer!\n", stderr);
          return 0;
        }

        re_compiled->u.ccl[charIdx++] = pattern[i];
      }

      if (&re_compiled->u.ccl[charIdx] >= (char *)re_data + bytes)
      {
        /* Catches cases such as [00000000000000000000000000000000000000][ */
        // fputs("exceeded internal buffer!\n", stderr);
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
      // cbmc: arithmetic overflow on signed to unsigned type conversion in c
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
#if 0
  const char *const types[] = { "UNUSED", "DOT", "BEGIN", "END", "QUESTIONMARK", "STAR", "PLUS", "CHAR", "CHAR_CLASS", "INV_CHAR_CLASS", "DIGIT", "NOT_DIGIT", "ALPHA", "NOT_ALPHA", "WHITESPACE", "NOT_[...]"
}
#endif
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

    // if (group_end && i == group_end)
    //   printf("      )\n");
#if 0
    if (pattern->type <= TIMES_NM)
      re_string_cat_fmt_(buffer, "type: %s", types[pattern->type]);
    else
      re_string_cat_fmt_(buffer, "invalid type: %d", pattern->type);
#endif
    if (pattern->type == CHAR_CLASS || pattern->type == INV_CHAR_CLASS)
    {
      re_string_cat_fmt_(buffer, "[");
      if (pattern->type == INV_CHAR_CLASS)
        re_string_cat_fmt_(buffer, "^");
      /* Changed: output ch first (was data[-1]), then ccl starting at [0] */
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
    // re_string_cat_fmt_(buffer, "\n");
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
  return ((c == 's') || (c == 'S') || (c == 'w') || (c == 'W') || (c == 'd') || (c == 'D'));
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

static int matchstar(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  return matchplus(p, pattern, text, matchlength) ||
         matchpattern(pattern, text, matchlength, &num_patterns);
}

static int matchplus(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  const char *prepoint = text;
  while ((text[0] != '\0') && matchone(p, *text))
  {
    DEBUG_P("+ matches %s\n", text);
    text++;
  }
  for (; text > prepoint; text--)
  {
    if (matchpattern(pattern, text, matchlength, &num_patterns))
    {
      *matchlength += text - prepoint;
      return 1;
    }
    DEBUG_P("+ pattern does not match %s\n", &text[1]);
  }
  DEBUG_P("+ pattern did not match %s\n", prepoint);
  return 0;
}

static int matchquestion(regex_t *p, regex_t *pattern, const char *text, int *matchlength)
{
  int num_patterns = 0;
  if (p->type == UNUSED)
    return 1;
  if (matchpattern(pattern, text, matchlength, &num_patterns))
  {
#ifdef DEBUG
    re_print(pattern);
    DEBUG_P("? matched %s\n", text);
#endif
    return 1;
  }
  if (*text && matchone(p, *text++))
  {
    if (matchpattern(pattern, text, matchlength, &num_patterns))
    {
      (*matchlength)++;
#ifdef DEBUG
      re_print(pattern);
      DEBUG_P("? matched %s\n", text);
#endif
      return 1;
    }
  }
  return 0;
}

static int matchtimes(regex_t *p, unsigned short n, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  /* Match the pattern n times */
  while (*text && matchone(p, *text++) && i < n)
  {
    (*matchlength)++;
    i++;
  }
  if (i == n)
    return 1;
  *matchlength = pre;
  return 0;
}

static int matchtimes_n(regex_t *p, unsigned short n, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  /* Match the pattern n or more times */
  while (*text && matchone(p, *text++))
  {
    i++;
    ++(*matchlength);
  }
  if (i >= n)
    return 1;
  *matchlength = pre;
  return 0;
}

static int matchtimes_m(regex_t *p, unsigned short m, const char *text, int *matchlength)
{
  unsigned short i = 0;
  /* Match the pattern max m times */
  while (*text && matchone(p, *text++) && i < m)
  {
    (*matchlength)++;
    i++;
  }
  return 1;
}

static int matchtimes_nm(regex_t *p, unsigned short n, unsigned short m, const char *text, int *matchlength)
{
  unsigned short i = 0;
  int pre = *matchlength;
  /* Match the pattern n to m times */
  while (*text && matchone(p, *text++) && i < m)
  {
    (*matchlength)++;
    i++;
  }
  if (i >= n && i <= m)
    return 1;
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

    regex_t *next_pattern = getnext(pattern);

    if (next_pattern->type == QUESTIONMARK)
    {
      return matchquestion(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (next_pattern->type == STAR)
    {
      // int i = (pattern[1].type == GROUPEND) ? pattern[1].u.group_start : 0;
      return matchstar(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (next_pattern->type == PLUS)
    {
      DEBUG_P("PLUS match %s?\n", text);
      // int i = (pattern[1].type == GROUPEND) ? pattern[1].u.group_start : 0;
      return matchplus(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (ismultimatch(next_pattern->type))
    {
      int retval = 0;
      if (next_pattern->type == TIMES)
      {
        // int i = (pattern[1].type == GROUPEND) ? pattern[1].u.group_start : 0;
        retval = matchtimes(pattern, next_pattern->u.n, text, matchlength);
      }
      else if (next_pattern->type == TIMES_N)
      {
        retval = matchtimes_n(pattern, next_pattern->u.n, text, matchlength);
      }
      else if (next_pattern->type == TIMES_M)
      {
        retval = matchtimes_m(pattern, next_pattern->u.m, text, matchlength);
      }
      else if (next_pattern->type == TIMES_NM)
      {
        // int i = (pattern[1].type == GROUPEND) ? pattern[1].u.group_start : 0;
        retval = matchtimes_nm(pattern, next_pattern->u.n, next_pattern->u.m, text,
                               matchlength);
      }

      if (!retval)
        return 0;
      else
      {
        pre = *matchlength;
        (*num_patterns)++;
        pattern = getnext(next_pattern);
        text += *matchlength;
        if (*text == '\0')
          return retval;
        continue;
      }
    }
    else if (next_pattern->type == BRANCH)
    {
      // int i = (pattern[1].type == GROUPEND) ? pattern[1].u.group_start : 0;
      return matchbranch(pattern, getnext(next_pattern), text, matchlength);
    }
    else if (pattern->type == GROUPEND)
    {
      (*num_patterns)++;
      DEBUG_P("GROUPEND matches %.*s (len %d, patterns %d)\n", *matchlength, text - *matchlength, *matchlength, *num_patterns);
      return 1;
    }
    else if (pattern->type == GROUP)
    {
      const int beforelen = *matchlength;
      const int retval = matchgroup(pattern, text, matchlength);

      if (!retval)
        return 0;
      else
      {
        text += (*matchlength - beforelen);
        pre = *matchlength;
        (*num_patterns) += pattern->u.group_size + 2;
        pattern = getindex(pattern, pattern->u.group_size + 2);
        if (*text == '\0')
          return retval;
        continue;
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

#ifdef CPROVER
#define N 24

/* Formal verification with cbmc: */
/* cbmc -DCPROVER --64 --depth 200 --bounds-check --pointer-check --memory-leak-check --div-by-zero-check --signed-overflow-check --unsigned-overflow-check --pointer-overflow-check --conversion-check [...]
 */

void verify_re_compile()
{
  /* test input - ten chars used as a regex-pattern input */
  char arr[N];
  /* make input symbolic, to search all paths through the code */
  /* i.e. the input is checked for all possible ten-char combinations */
  for (int i = 0; i < sizeof(arr) - 1; i++)
  {
    // arr[i] = nondet_char();
    assume(arr[i] > -127 && arr[i] < 128);
  }
  /* assume proper NULL termination */
  assume(arr[sizeof(arr) - 1] == 0);
  /* verify abscence of run-time errors - go! */
  re_compile(arr);
}

void verify_re_print()
{
  regex_t pattern[MAX_REGEXP_OBJECTS];
  for (unsigned char i = 0; i < MAX_REGEXP_OBJECTS; i++)
  {
    // pattern[i].type = nondet_uchar();
    assume(pattern[i].type >= 0 && pattern[i].type <= 255);
    pattern[i].u.ccl = nondet_long();
  }
  re_print(&pattern);
}

void verify_re_match()
{
  int length;
  regex_t pattern[MAX_REGEXP_OBJECTS];
  char arr[N];

  for (unsigned char i = 0; i < MAX_REGEXP_OBJECTS; i++)
  {
    // pattern[i].type = nondet_uchar();
    // pattern[i].u.ch = nondet_int();
    assume(pattern[i].type >= 0 && pattern[i].type <= 255);
    assume(pattern[i].u.ccl >= 0 && pattern[i].u.ccl <= ~1);
  }
  for (int i = 0; i < sizeof(arr) - 1; i++)
  {
    assume(arr[i] > -127 && arr[i] < 128);
  }
  /* assume proper NULL termination */
  assume(arr[sizeof(arr) - 1] == 0);

  re_match(&pattern, arr, &length);
}

int main(int argc, char *argv[])
{
  verify_re_compile();
  verify_re_print();
  verify_re_match();
  return 0;
}
#endif