#ifndef VM_CORE_H
#define VM_CORE_H

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#include "configuration.h"

/* MMBasic value type flags needed by the VM/frontend without dragging in
 * the full interpreter header surface.
 */
#define T_NOTYPE   0
#define T_NBR      0x01
#define T_STR      0x02
#define T_INT      0x04
#define T_PTR      0x08
#define T_IMPLIED  0x10
#define T_CONST    0x20
#define T_BLOCKED  0x40
#define T_STRUCT   0x80                  // structure-typed variable (upstream 6.02)

#define TypeMask(a) ((a) & (T_NBR | T_INT | T_STR | T_STRUCT))

#ifndef isnamestart
#define isnamestart(c)  (isalpha((unsigned char)(c)) || (c) == '_')
#endif

#ifndef isnamechar
#define isnamechar(c)   (isalnum((unsigned char)(c)) || (c) == '_' || (c) == '.')
#endif

#endif
