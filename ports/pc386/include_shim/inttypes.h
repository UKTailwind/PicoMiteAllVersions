/* ports/pc386/include_shim/inttypes.h — freestanding shim.
 *
 * printf format-specifier macros for intN_t / uintN_t types. ILP32
 * (i686) sizes: int = long = 32-bit, long long = 64-bit.
 */
#ifndef _PC386_INTTYPES_H
#define _PC386_INTTYPES_H

#include <stdint.h>

#define PRId8        "d"
#define PRId16       "d"
#define PRId32       "d"
#define PRId64       "lld"
#define PRIdLEAST8   "d"
#define PRIdLEAST16  "d"
#define PRIdLEAST32  "d"
#define PRIdLEAST64  "lld"
#define PRIdFAST8    "d"
#define PRIdFAST16   "d"
#define PRIdFAST32   "d"
#define PRIdFAST64   "lld"
#define PRIdMAX      "lld"
#define PRIdPTR      "ld"

#define PRIi8        "i"
#define PRIi16       "i"
#define PRIi32       "i"
#define PRIi64       "lli"

#define PRIu8        "u"
#define PRIu16       "u"
#define PRIu32       "u"
#define PRIu64       "llu"
#define PRIuLEAST8   "u"
#define PRIuLEAST16  "u"
#define PRIuLEAST32  "u"
#define PRIuLEAST64  "llu"
#define PRIuFAST8    "u"
#define PRIuFAST16   "u"
#define PRIuFAST32   "u"
#define PRIuFAST64   "llu"
#define PRIuMAX      "llu"
#define PRIuPTR      "lu"

#define PRIo8        "o"
#define PRIo16       "o"
#define PRIo32       "o"
#define PRIo64       "llo"

#define PRIx8        "x"
#define PRIx16       "x"
#define PRIx32       "x"
#define PRIx64       "llx"
#define PRIxMAX      "llx"
#define PRIxPTR      "lx"

#define PRIX8        "X"
#define PRIX16       "X"
#define PRIX32       "X"
#define PRIX64       "llX"

#define SCNd8        "hhd"
#define SCNd16       "hd"
#define SCNd32       "d"
#define SCNd64       "lld"

#define SCNu8        "hhu"
#define SCNu16       "hu"
#define SCNu32       "u"
#define SCNu64       "llu"

#define SCNx8        "hhx"
#define SCNx16       "hx"
#define SCNx32       "x"
#define SCNx64       "llx"

#endif
