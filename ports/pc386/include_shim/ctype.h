/* ports/pc386/include_shim/ctype.h — freestanding shim. */
#ifndef _PC386_CTYPE_H
#define _PC386_CTYPE_H

int isalpha (int c);
int isdigit (int c);
int isalnum (int c);
int isspace (int c);
int isxdigit(int c);
int isupper (int c);
int islower (int c);
int isprint (int c);
int isgraph (int c);
int ispunct (int c);
int iscntrl (int c);
int isascii (int c);
int toupper (int c);
int tolower (int c);

#endif
