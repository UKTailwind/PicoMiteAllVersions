/***********************************************************************************************************************
PicoMite MMBasic

Sprite.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/
/**
 * @file Sprite.c
 * @author Geoff Graham, Peter Mather
 * Thanks to ksinger for the ideas behind the SPRITE(B function
 * @brief Source for the SPRITE MMBasic command and function: the sprite
 *        engine (chunked buffer pool, LIFO layering, collision detection
 *        incl. static objects, load/scroll). Split out of Draw.c.
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Memory.h"
#include "DrawInternal.h"
#if PICOMITERP2350
#include "VGA222.h"
#endif

#define LONG long

// Magic number to indicate sprite position is not in use
#define SPRITE_POS_INACTIVE 10000

typedef struct _BMPDECODER
{
    LONG lWidth;
    LONG lHeight;
    LONG lImageOffset;
    WORD wPaletteEntries;
    BYTE bBitsPerPixel;
    BYTE bHeaderType;
    BYTE blBmMarkerFlag : 1;
    BYTE blCompressionType : 3;
    BYTE bNumOfPlanes : 3;
    BYTE b16bit565flag : 1;
    BYTE aPalette[256][3]; /* Each palette entry has RGB */
} BMPDECODER;

// Sprite buffer management - dynamically allocated in chunks of 3 sprites per 256-byte page
struct spritebuffer *spritebuff[MAXBLITBUF + 1] = {NULL};
// Chunk tracking: each chunk holds SPRITES_PER_CHUNK sprites
// We need (MAXBLITBUF + SPRITES_PER_CHUNK) / SPRITES_PER_CHUNK chunks max
#define SPRITE_CHUNK_COUNT ((MAXBLITBUF + SPRITES_PER_CHUNK) / SPRITES_PER_CHUNK)
static struct spritebuffer *sprite_chunks[SPRITE_CHUNK_COUNT] = {NULL};
static uint8_t sprite_chunk_used[SPRITE_CHUNK_COUNT] = {0}; // Count of sprites used in each chunk

int layer_in_use[MAXLAYER + 1];
unsigned char LIFO[MAXBLITBUF];
unsigned char zeroLIFO[MAXBLITBUF];
uint8_t LIFOpointer = 0;
uint8_t zeroLIFOpointer = 0;
uint8_t sprites_in_use = 0;
char *COLLISIONInterrupt = NULL;
bool CollisionFound = false;
int sprite_which_collided = -1;
static bool hideall = 0;
uint8_t sprite_transparent = 0;

// Static object collision detection
struct stobject stobjects[MAXSTOBJECTS + 1] = {0};
char *STCollisionInterrupt = NULL;
bool STCollisionFound = false;
int sprite_hit_st = -1;
int st_which_collided = -1;

extern BYTE BDEC_bReadHeader(BMPDECODER *pBmpDec, int fnbr);
extern BYTE BMP_bDecode_memory(int x, int y, int xlen, int ylen, int fnbr, char *p);

/* The sprite palette tables are defined earlier in this file and shared
   with the GUI CURSOR overlay in Pointer.c via DrawInternal.h, which
   also provides the spriteCharToColorIndex() helper. */

// Sprite pool allocator - allocates sprites in chunks of 3 per 256-byte page
// Returns pointer to allocated sprite structure, or NULL on failure
static struct spritebuffer *allocSpriteBuff(int bnbr)
{
    if (spritebuff[bnbr] != NULL)
        return spritebuff[bnbr]; // Already allocated

    int chunk_idx = bnbr / SPRITES_PER_CHUNK;
    int slot_in_chunk = bnbr % SPRITES_PER_CHUNK;

    // Allocate chunk if not already allocated
    if (sprite_chunks[chunk_idx] == NULL)
    {
        sprite_chunks[chunk_idx] = (struct spritebuffer *)GetMemory(sizeof(struct spritebuffer) * SPRITES_PER_CHUNK);
        memset(sprite_chunks[chunk_idx], 0, sizeof(struct spritebuffer) * SPRITES_PER_CHUNK);
        sprite_chunk_used[chunk_idx] = 0;
    }

    spritebuff[bnbr] = &sprite_chunks[chunk_idx][slot_in_chunk];
    sprite_chunk_used[chunk_idx]++;

    // Also allocate spritebuff[0] if this is the first sprite being allocated (and bnbr != 0)
    if (bnbr != 0 && spritebuff[0] == NULL)
    {
        allocSpriteBuff(0);
    }

    return spritebuff[bnbr];
}

// Helper function to initialize a sprite buffer with default inactive values

// Helper function to initialize a sprite buffer with default inactive values
// Allocates the sprite if not already allocated
static void initSpriteBuff(int bnbr, int w, int h)
{
    if (spritebuff[bnbr] == NULL)
    {
        allocSpriteBuff(bnbr);
    }
    spritebuff[bnbr]->w = w;
    spritebuff[bnbr]->h = h;
    spritebuff[bnbr]->master = 0;
    spritebuff[bnbr]->mymaster = -1;
    spritebuff[bnbr]->x = SPRITE_POS_INACTIVE;
    spritebuff[bnbr]->y = SPRITE_POS_INACTIVE;
    spritebuff[bnbr]->layer = -1;
    spritebuff[bnbr]->next_x = SPRITE_POS_INACTIVE;
    spritebuff[bnbr]->next_y = SPRITE_POS_INACTIVE;
    spritebuff[bnbr]->active = false;
    spritebuff[bnbr]->lastcollisions = 0;
    spritebuff[bnbr]->edges = 0;
    spritebuff[bnbr]->boundsleft = NULL;
    spritebuff[bnbr]->boundsright = NULL;
    spritebuff[bnbr]->boundstop = NULL;
    spritebuff[bnbr]->boundsbottom = NULL;
}

void LIFOadd(int n)
{
    int i, j = 0;
    for (i = 0; i < LIFOpointer; i++)
    {
        if (LIFO[i] != n)
        {
            LIFO[j] = LIFO[i];
            j++;
        }
    }
    LIFO[j] = n;
    LIFOpointer = j + 1;
}
void LIFOremove(int n)
{
    int i, j = 0;
    for (i = 0; i < LIFOpointer; i++)
    {
        if (LIFO[i] != n)
        {
            LIFO[j] = LIFO[i];
            j++;
        }
    }
    LIFOpointer = j;
}
void LIFOswap(int n, int m)
{
    int i;
    for (i = 0; i < LIFOpointer; i++)
    {
        if (LIFO[i] == n)
            LIFO[i] = m;
    }
}
void zeroLIFOadd(int n)
{
    int i, j = 0;
    for (i = 0; i < zeroLIFOpointer; i++)
    {
        if (zeroLIFO[i] != n)
        {
            zeroLIFO[j] = zeroLIFO[i];
            j++;
        }
    }
    zeroLIFO[j] = n;
    zeroLIFOpointer = j + 1;
}
void zeroLIFOremove(int n)
{
    int i, j = 0;
    for (i = 0; i < zeroLIFOpointer; i++)
    {
        if (zeroLIFO[i] != n)
        {
            zeroLIFO[j] = zeroLIFO[i];
            j++;
        }
    }
    zeroLIFOpointer = j;
}
void zeroLIFOswap(int n, int m)
{
    int i;
    for (i = 0; i < zeroLIFOpointer; i++)
    {
        if (zeroLIFO[i] == n)
            zeroLIFO[i] = m;
    }
}
void getspritebounds(int bnbr)
{
    if (spritebuff[bnbr]->boundsleft != NULL)
    {
        return;
    }
    int w = spritebuff[bnbr]->w;
    int h = spritebuff[bnbr]->h;
    int sprite_transparent2 = sprite_transparent << 4;
    char *c = (char *)spritebuff[bnbr]->spritebuffptr;

    // Free old bounds if they exist (only free boundsleft as others are part of same allocation)

    // Allocate all bounds arrays in a single block to minimize memory page usage
    // Layout: [boundsleft: h shorts][boundsright: h shorts][boundstop: w shorts][boundsbottom: w shorts]
    int bounds_size = (h + h + w + w) * sizeof(short);
    short *bounds_block = (short *)GetMemory(bounds_size);
    spritebuff[bnbr]->boundsleft = bounds_block;
    spritebuff[bnbr]->boundsright = bounds_block + h;
    spritebuff[bnbr]->boundstop = bounds_block + h + h;
    spritebuff[bnbr]->boundsbottom = bounds_block + h + h + w;
    for (int x = 0; x < w; ++x)
    {
        spritebuff[bnbr]->boundstop[x] = SHRT_MAX;
        spritebuff[bnbr]->boundsbottom[x] = -1;
    }
    int nib = 1;
    for (int y = 0; y < h; ++y)
    {
        int x;
        spritebuff[bnbr]->boundsleft[y] = w;
        spritebuff[bnbr]->boundsright[y] = -1;
        for (x = 0; x < w; ++x)
        {
            nib ^= 1;
            if (nib)
            { // odd x upper nipple
                if ((*c++ & 0xf0) == sprite_transparent2)
                    continue;
            }
            else
            { // even x lower nipple
                if ((*c & 0x0f) == sprite_transparent)
                    continue;
            }
            if (spritebuff[bnbr]->boundsleft[y] == w)
                spritebuff[bnbr]->boundsleft[y] = x;
            spritebuff[bnbr]->boundsright[y] = x;
            spritebuff[bnbr]->boundsbottom[x] = y;
            if (spritebuff[bnbr]->boundstop[x] == SHRT_MAX)
                spritebuff[bnbr]->boundstop[x] = y;
        }
    }
}
void MIPS16 closeallsprites(void)
{
    int i;

    // Free sub-allocations for all sprites
    for (i = 0; i <= MAXBLITBUF; i++)
    {
        if (i <= MAXLAYER)
            layer_in_use[i] = 0;

        if (spritebuff[i] != NULL)
        {
            if (i)
            {
                if (spritebuff[i]->mymaster == -1)
                {
                    // Master sprite: spritebuffptr and blitstoreptr are in one block
                    FreeMemory((unsigned char *)spritebuff[i]->spritebuffptr);
                    // boundsleft, boundsright, boundstop, boundsbottom are all in one block
                    FreeMemory((unsigned char *)spritebuff[i]->boundsleft);
                }
                else
                {
                    // Copy sprite: only blitstoreptr is allocated separately
                    if (spritebuff[i]->blitstoreptr != NULL)
                    {
                        FreeMemory((unsigned char *)spritebuff[i]->blitstoreptr);
                    }
                }
            }
            spritebuff[i] = NULL;
        }
    }

    // Free all sprite chunks
    for (i = 0; i < SPRITE_CHUNK_COUNT; i++)
    {
        if (sprite_chunks[i] != NULL)
        {
            FreeMemory((unsigned char *)sprite_chunks[i]);
            sprite_chunks[i] = NULL;
            sprite_chunk_used[i] = 0;
        }
    }

    LIFOpointer = 0;
    zeroLIFOpointer = 0;
    sprites_in_use = 0;
    hideall = 0;
}

void MIPS16 closeallstobjects(void)
{
    int i;
    for (i = 1; i <= MAXSTOBJECTS; i++)
    {
        stobjects[i].x = 0;
        stobjects[i].y = 0;
        stobjects[i].w = 0;
        stobjects[i].h = 0;
        stobjects[i].active = 0;
    }
    STCollisionInterrupt = NULL;
    STCollisionFound = false;
    sprite_hit_st = -1;
    st_which_collided = -1;
}

// Check for collisions between a sprite and all active static objects
// Called from ProcessCollisions after sprite-to-sprite collision detection
// Stores collision info in spritebuff[bnbr]->collisions array
void CheckSTCollisions(int bnbr, int *n)
{
    struct spritebuffer *sb = spritebuff[bnbr];
    int sw = sb->w;
    int sh = sb->h;
    int sx = sb->x;
    int sy = sb->y;

    for (int i = 1; i <= MAXSTOBJECTS; i++)
    {
        if (!stobjects[i].active)
            continue;

        // AABB intersection test
        if (!(sx + sw <= stobjects[i].x ||
              sx >= stobjects[i].x + stobjects[i].w ||
              sy + sh <= stobjects[i].y ||
              sy >= stobjects[i].y + stobjects[i].h))
        {
            // Collision detected - store in collisions array with high bit marker (0x80 + static object number)
            if (*n < MAXCOLLISIONS)
            {
                sb->collisions[(*n)++] = (char)(0x80 | i); // Mark as ST collision with object number
            }
            STCollisionFound = true;
            sprite_hit_st = bnbr;
            st_which_collided = i;
        }
    }
}

void checklimits(int bnbr, int *n)
{
    int maxW = HRes;
    int maxH = VRes;
    struct spritebuffer *sb = spritebuff[bnbr]; // Cache pointer for performance
    sb->collisions[*n] = 0;
    if (sb->x < 0)
    {
        if (!(sb->edges & 1))
        {
            sb->edges |= 1;
            sb->collisions[*n] = (char)0xF1;
            (*n)++;
        }
    }
    else
        sb->edges &= ~1;

    if (sb->y < 0)
    {
        if (!(sb->edges & 2))
        {
            sb->edges |= 2;
            if (sb->collisions[*n] & 0xF0)
                sb->collisions[*n] |= 0xF2;
            else
            {
                sb->collisions[*n] = (char)0xF2;
                (*n)++;
            }
        }
    }
    else
        sb->edges &= ~2;

    if (sb->x + sb->w > maxW)
    {
        if (!(sb->edges & 4))
        {
            sb->edges |= 4;
            if (sb->collisions[*n] & 0xF0)
                sb->collisions[*n] |= 0xF4;
            else
            {
                sb->collisions[*n] = (char)0xF4;
                (*n)++;
            }
        }
    }
    else
        sb->edges &= ~4;

    if (sb->y + sb->h > maxH)
    {
        if (!(sb->edges & 8))
        {
            sb->edges |= 8;
            if (sb->collisions[*n] & 0xF0)
                sb->collisions[*n] |= 0xF8;
            else
            {
                sb->collisions[*n] = (char)0xF8;
                (*n)++;
            }
        }
    }
    else
        sb->edges &= ~8;
}
void ProcessCollisions(int bnbr)
{
    int k, j = 1, n = 1, bcol = 1;
    // We know that any collision is caused by movement of sprite bnbr
    //  a value of zero indicates that we are processing movement of layer 0 and any
    //  sprites on that layer
    CollisionFound = false;
    sprite_which_collided = -1;
    uint64_t mask, mymask = (uint64_t)1 << ((uint64_t)bnbr - (uint64_t)1);
    struct spritebuffer *sb = spritebuff[bnbr]; // Cache pointer for performance
    memset(spritebuff[0]->collisions, 0, MAXCOLLISIONS);
    if (bnbr != 0)
    {                                             // a specific sprite has moved
        memset(sb->collisions, 0, MAXCOLLISIONS); // clear our previous collisions
        if (sb->layer != 0)
        {
            if (layer_in_use[sb->layer] + layer_in_use[0] > 1)
            { // other sprites in this layer
                for (k = 1; k <= MAXBLITBUF; k++)
                {
                    struct spritebuffer *sk = spritebuff[k]; // Cache inner loop pointer
                    if (sk == NULL)
                        continue;
                    mask = (uint64_t)1 << ((uint64_t)k - (uint64_t)1);
                    if (!(sk->active))
                    {
                        sb->lastcollisions &= ~mask;
                        continue;
                    }
                    if (k == bnbr)
                        continue;
                    if (j == layer_in_use[sb->layer] + layer_in_use[0])
                        break; // nothing left to process
                    if ((sk->layer == sb->layer || sk->layer == 0))
                    {
                        j++;
                        if (!(sk->x + sk->w < sb->x ||
                              sk->x > sb->x + sb->w ||
                              sk->y + sk->h < sb->y ||
                              sk->y > sb->y + sb->h))
                        {
                            if (n < MAXCOLLISIONS && !(sb->lastcollisions & mask))
                                sb->collisions[n++] = k;
                            sb->lastcollisions |= mask;
                            sk->lastcollisions |= mymask;
                        }
                        else
                        {
                            sb->lastcollisions &= ~mask;
                            sk->lastcollisions &= ~mymask;
                        }
                    }
                }
            }
        }
        else
        {
            for (k = 1; k <= MAXBLITBUF; k++)
            {
                struct spritebuffer *sk = spritebuff[k]; // Cache inner loop pointer
                if (sk == NULL)
                    continue;
                if (j == sprites_in_use)
                    break; // nothing left to process
                if (k == bnbr)
                    continue;
                mask = (uint64_t)1 << ((uint64_t)k - (uint64_t)1);
                if (!(sk->active))
                {
                    sb->lastcollisions &= ~mask;
                    continue;
                }
                else
                    j++;
                if (!(sk->x + sk->w < sb->x ||
                      sk->x > sb->x + sb->w ||
                      sk->y + sk->h < sb->y ||
                      sk->y > sb->y + sb->h))
                {
                    if (n < MAXCOLLISIONS && !(sb->lastcollisions & mask))
                        sb->collisions[n++] = k;
                    sb->lastcollisions |= mask;
                    sk->lastcollisions |= mymask;
                }
                else
                {
                    sb->lastcollisions &= ~mask;
                    sk->lastcollisions &= ~mymask;
                }
            }
        }
        // now look for collisions with the edge of the screen
        checklimits(bnbr, &n);
        // now look for collisions with static objects
        CheckSTCollisions(bnbr, &n);
        if (n > 1)
        {
            CollisionFound = true;
            sprite_which_collided = bnbr;
            spritebuff[bnbr]->collisions[0] = n - 1;
        }
    }
    else
    { // the background layer has moved
        j = 0;
        for (k = 1; k <= MAXBLITBUF; k++)
        {                                            // loop through all sprites
            struct spritebuffer *sk = spritebuff[k]; // Cache pointer for performance
            if (sk == NULL)
                continue;
            mask = (uint64_t)1 << ((uint64_t)k - (uint64_t)1);
            n = 1;
            int kk, jj = 1;
            if (j == sprites_in_use)
                break; // nothing left to process
            if (sk->active)
            { // sprite found
                memset(sk->collisions, 0, MAXCOLLISIONS);
                j++;
                if (layer_in_use[sk->layer] + layer_in_use[0] > 1)
                { // other sprites in this layer
                    for (kk = 1; kk <= MAXBLITBUF; kk++)
                    {
                        struct spritebuffer *skk = spritebuff[kk]; // Cache inner loop pointer
                        if (skk == NULL)
                            continue;
                        if (kk == k)
                            continue;
                        if (jj == layer_in_use[sk->layer] + layer_in_use[0])
                            break; // nothing left to process
                        if ((skk->layer == sk->layer || skk->layer == 0))
                        {
                            jj++;
                            if (!(skk->x + skk->w < sk->x ||
                                  skk->x > sk->x + sk->w ||
                                  skk->y + skk->h < sk->y ||
                                  skk->y > sk->y + sk->h))
                            {
                                if (n < MAXCOLLISIONS && !(sk->lastcollisions & mask))
                                    sk->collisions[n++] = kk;
                                sk->lastcollisions |= mask;
                            }
                            else
                            {
                                sk->lastcollisions &= ~mask;
                            }
                        }
                    }
                }
                checklimits(k, &n);
                CheckSTCollisions(k, &n);
                if (n > 1 && n < MAXCOLLISIONS && bcol < MAXCOLLISIONS)
                {
                    spritebuff[0]->collisions[bcol] = k;
                    bcol++;
                    sk->collisions[0] = n - 1;
                }
            }
        }
        if (bcol > 1)
        {
            CollisionFound = true;
            sprite_which_collided = 0;
            spritebuff[0]->collisions[0] = bcol - 1;
        }
    }
}
void blithide(int bnbr)
{
    struct spritebuffer *sb = spritebuff[bnbr]; // Cache pointer for performance
    int w = sb->w;
    int h = sb->h;
    int x1 = sb->x;
    int y1 = sb->y;
    sb->active = 0;
    DrawBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, -1, (unsigned char *)sb->blitstoreptr);
}
void expandpixel(volatile unsigned char *ii, volatile unsigned char *oo, int n, int mode)
{
    volatile unsigned char *o = oo, *i = ii;
    int toggle = 0;
    if (mode == 0)
    {
        while (n--)
        {
            if (toggle)
            {
                *o++ = (*i++ >> 4);
            }
            else
            {
                *o++ = *i & 0xF;
            }
            toggle = !toggle;
        }
    }
    else
    {
        while (n--)
        {
            *o++ = (*i >> toggle++) & 0x1;
            if (toggle == 8)
            {
                i++;
                toggle = 0;
            }
        }
    }
}
void contractpixel(volatile unsigned char *ii, volatile unsigned char *oo, int n, int mode)
{
    int toggle = 0;
    volatile unsigned char *o = oo, *i = ii;
    if (mode == 0)
    {
        while (n--)
        {
            if (toggle)
            {
                *o++ |= (*i++ << 4);
            }
            else
            {
                *o = *i++ & 0xF;
            }
            toggle = !toggle;
        }
    }
    else
    {
        while (n--)
        {
            if (toggle == 0)
                *o = 0;
            *o |= (*i++ << toggle++);
            if (toggle == 8)
            {
                toggle = 0;
                o++;
            }
        }
    }
}

void BlitShowBuffer(int bnbr, int x1, int y1, int mode)
{
    struct spritebuffer *sb = spritebuff[bnbr]; // Cache pointer for performance
    char *current;
    int x, xx, y, yy, rotation, fullmode = mode;
    mode &= 7;
    rotation = sb->rotation;
    current = sb->blitstoreptr;
    int w, h;
    if (sb->spritebuffptr != NULL)
    {
        w = sb->w;
        h = sb->h;
        if (!(mode == 0 || mode & 4) && sb->active)
        {
            DrawBufferFast(sb->x, sb->y, sb->x + w - 1, sb->y + h - 1, -1, (unsigned char *)current);
        }
        sb->x = x1;
        sb->y = y1;
        if (!(mode == 2))
            ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, (unsigned char *)current);
        // we now have the old screen image stored together with the coordinates
        if (rotation)
        {
            unsigned char *d = GetTempMainMemory(w * h);
            unsigned char *r = GetTempMainMemory((w * h + 1) >> 1);
            expandpixel((unsigned char *)sb->spritebuffptr, d, w * h, 0);
            if (rotation & 1)
            { // swap left/write
                for (y = 0; y < h; y++)
                {
                    for (x = 0, xx = w - 1; x < (w >> 1); x++, xx--)
                    {
                        swap(d[y * w + x], d[y * w + xx]);
                    }
                }
            }
            if (rotation & 2)
            {
                for (x = 0; x < w; x++)
                {
                    for (y = 0, yy = h - 1; y < (h >> 1); y++, yy--)
                    {
                        swap(d[x + y * w], d[x + yy * w]);
                    }
                }
            }
            contractpixel(d, r, w * h, 0);
            DrawBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, ((fullmode & 8) == 0 ? 0 : -1), (unsigned char *)r);
        }
        else
        {
            DrawBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, ((fullmode & 8) == 0 ? 0 : -1), (unsigned char *)sb->spritebuffptr);
        }
        if (!(mode & 4))
            sb->active = 1;
    }
}
int sumlayer(void)
{
    int i, j = 0;
    for (i = 0; i <= MAXLAYER; i++)
        j += layer_in_use[i];
    return j;
}
void hidesafe(int bnbr)
{
    struct spritebuffer *sb = spritebuff[bnbr]; // Cache pointer for performance
    int found = INT_MAX;
    int zerolifo = 0;
    int i;
    for (i = LIFOpointer - 1; i >= 0; i--)
    {
        if (LIFO[i] == bnbr)
        {
            blithide(LIFO[i]);
            found = i;
            break;
        }
        blithide(LIFO[i]);
    }
    if (found == INT_MAX)
    {
        for (i = zeroLIFOpointer - 1; i >= 0; i--)
        {
            if (zeroLIFO[i] == bnbr)
            {
                blithide(zeroLIFO[i]);
                found = -i;
                zerolifo = 1;
                break;
            }
            blithide(zeroLIFO[i]);
        }
    }
    if (found != INT_MAX)
    {
        sprites_in_use--;
        layer_in_use[sb->layer]--;
        sb->x = SPRITE_POS_INACTIVE;
        sb->y = SPRITE_POS_INACTIVE;
        if (sb->layer == 0)
            zeroLIFOremove(bnbr);
        else
            LIFOremove(bnbr);
        sb->layer = -1;
        sb->next_x = SPRITE_POS_INACTIVE;
        sb->next_y = SPRITE_POS_INACTIVE;
        sb->lastcollisions = 0;
        sb->edges = 0;
        if (zerolifo)
        {
            found = -found;
            for (i = found; i < zeroLIFOpointer; i++)
            {
                int idx = zeroLIFO[i];
                BlitShowBuffer(idx, spritebuff[idx]->x, spritebuff[idx]->y, 0);
            }
            for (i = 0; i < LIFOpointer; i++)
            {
                int idx = LIFO[i];
                BlitShowBuffer(idx, spritebuff[idx]->x, spritebuff[idx]->y, 0);
            }
        }
        else
        {
            for (i = found; i < LIFOpointer; i++)
            {
                int idx = LIFO[i];
                BlitShowBuffer(idx, spritebuff[idx]->x, spritebuff[idx]->y, 0);
            }
        }
    }
}

void showsafe(int bnbr, int x, int y)
{
    int found = INT_MAX;
    int zerolifo = 0;
    int i;
    for (i = LIFOpointer - 1; i >= 0; i--)
    {
        if (LIFO[i] == bnbr)
        {
            blithide(LIFO[i]);
            found = i;
            break;
        }
        blithide(LIFO[i]);
    }
    if (found == INT_MAX)
    {
        for (i = zeroLIFOpointer - 1; i >= 0; i--)
        {
            if (zeroLIFO[i] == bnbr)
            {
                blithide(zeroLIFO[i]);
                zerolifo = 1;
                found = -i;
                break;
            }
            blithide(zeroLIFO[i]);
        }
    }
    BlitShowBuffer(bnbr, x, y, 1);
    if (zerolifo)
    {
        found = -found;
        for (i = found + 1; i < zeroLIFOpointer; i++)
        {
            int idx = zeroLIFO[i];
            BlitShowBuffer(idx, spritebuff[idx]->x, spritebuff[idx]->y, 0);
        }
        for (i = 0; i < LIFOpointer; i++)
        {
            int idx = LIFO[i];
            BlitShowBuffer(idx, spritebuff[idx]->x, spritebuff[idx]->y, 0);
        }
    }
    else if (found != INT_MAX)
    {
        for (i = found + 1; i < LIFOpointer; i++)
        {
            int idx = LIFO[i];
            BlitShowBuffer(idx, spritebuff[idx]->x, spritebuff[idx]->y, 0);
        }
    }
}
void MIPS16 loadsprite(unsigned char *p)
{
    int fnbr, width, number, height = 0, newsprite = 1, startsprite = 1, bnbr, lc, i, toggle = 0;
    char *q, *fname;
    unsigned char buff[256], *z;
    uint32_t data;
    getcsargs(&p, 5);
    int mode = 0;
    fnbr = FindFreeFileNbr();
    if (!InitSDCard())
        return;
    fname = (char *)getFstring(argv[0]);
    if (argc >= 3 && *argv[2])
        startsprite = (int)getint(argv[2], 1, 64);
    if (argc == 5)
        mode = getint(argv[4], 0, 1);
    AppendDefaultExtension(fname, ".spr");
    if (!BasicFileOpen(fname, fnbr, FA_READ))
        error((char *)"File not found");
    MMgetline(fnbr, (char *)buff); // get the input line
    while (buff[0] == 39)
        MMgetline(fnbr, (char *)buff);
    z = buff;
    {
        getargs(&z, 5, (unsigned char *)", ");
        width = getinteger(argv[0]);
        number = getinteger(argv[2]);
        if (argc == 5)
            height = getinteger(argv[4]);
        if (height == 0)
            height = width;
        bnbr = startsprite;
        if (number + startsprite > MAXBLITBUF)
        {
            FileClose(fnbr);
            error((char *)"Maximum of % sprites", MAXBLITBUF);
        }
        while (!MMfeof(fnbr) && bnbr <= number + startsprite)
        { // while waiting for the end of file
            if (newsprite)
            {
                newsprite = 0;
                // Allocate sprite structure if needed (also allocates spritebuff[0])
                if (spritebuff[bnbr] == NULL)
                    allocSpriteBuff(bnbr);
                if (spritebuff[bnbr]->spritebuffptr == NULL)
                {
                    // Allocate both buffers in one block to save memory pages
                    int bufsize = (width * height + 1) >> 1;
                    char *combined = (char *)GetMemory(bufsize * 2);
                    spritebuff[bnbr]->spritebuffptr = combined;
                    spritebuff[bnbr]->blitstoreptr = combined + bufsize;
                }
                initSpriteBuff(bnbr, width, height);
                q = spritebuff[bnbr]->spritebuffptr;
                lc = height;
            }
            while (lc--)
            {
                MMgetline(fnbr, (char *)buff); // get the input line
                while (buff[0] == 39)
                    MMgetline(fnbr, (char *)buff);
                int bufflen = (int)strlen((char *)buff); // Cache strlen result
                if (bufflen < width)
                    memset(&buff[bufflen], 32, width - bufflen);
                for (i = 0; i < width; i++)
                {
                    int colorIdx = spriteCharToColorIndex(buff[i]);
                    if (colorIdx >= 0)
                        data = mode ? sprite_color_mode1[colorIdx] : sprite_color_mode0[colorIdx];
                    else
                        data = 0;
                    if (toggle)
                    {
                        *q++ |= (RGB121(data) << 4);
                    }
                    else
                    {
                        *q = RGB121(data);
                    }
                    toggle = !toggle;
                }
            }
            bnbr++;
            newsprite = 1;
        }
        FileClose(fnbr);
    }
}

void MIPS16 loadarray(unsigned char *p)
{
    int bnbr, w, h, size, i, toggle = 0;
    int maxH = VRes;
    int maxW = HRes;
    MMFLOAT *a3float = NULL;
    int64_t *a3int = NULL;
    char *q;
    //    uint16_t* qq;
    //    uint32_t* qqq;
    getcsargs(&p, 7);
    if (*argv[0] == '#')
        argv[0]++;
    bnbr = (int)getint(argv[0], 1, MAXBLITBUF);
    // Allocate sprite structure if needed (also allocates spritebuff[0])
    if (spritebuff[bnbr] == NULL)
        allocSpriteBuff(bnbr);
    if (spritebuff[bnbr]->spritebuffptr == NULL)
    {
        w = (int)getint(argv[2], 1, maxW);
        h = (int)getint(argv[4], 1, maxH);
        size = parsenumberarray(argv[6], &a3float, &a3int, 4, 1, NULL, true, NULL) - 1;
        if (size < w * h - 1)
            error((char *)"Array Dimensions");
        // Allocate both buffers in one block to save memory pages
        int bufsize = (w * h + 1) >> 1;
        char *combined = (char *)GetMemory(bufsize * 2);
        spritebuff[bnbr]->spritebuffptr = combined;
        spritebuff[bnbr]->blitstoreptr = combined + bufsize;
        initSpriteBuff(bnbr, w, h);
        q = spritebuff[bnbr]->spritebuffptr;
        int c;
        for (i = 0; i < w * h; i++)
        {
            if (a3float)
                c = (int)a3float[i];
            else
                c = (int)a3int[i];
            if (toggle)
            {
                *q++ |= (RGB121(c) << 4);
            }
            else
            {
                *q = RGB121(c);
            }
            toggle = !toggle;
        }
    }
    else
        error((char *)"Buffer already in use");
}

typedef enum
{
    SCROLL_BPP_1 = 1,
    SCROLL_BPP_4 = 4,
    SCROLL_BPP_8 = 8,
    SCROLL_BPP_16 = 16
} scroll_bpp_t;

static inline scroll_bpp_t scroll_get_bpp(void)
{
#ifdef PICOMITEVGA
    if (DISPLAY_TYPE == SCREENMODE1)
        return SCROLL_BPP_1;
#ifdef HDMI
    if (DISPLAY_TYPE == SCREENMODE4)
        return SCROLL_BPP_16;
    if (DISPLAY_TYPE == SCREENMODE5)
        return SCROLL_BPP_8;
#endif
    return SCROLL_BPP_4; // SCREENMODE2 and SCREENMODE3
#else
    // On non-VGA platforms sprite scroll is only valid when WriteBuf != NULL and is always RGB121 packed (4bpp).
    return SCROLL_BPP_4;
#endif
}

static inline int scroll_row_bytes(int width, scroll_bpp_t bpp)
{
    switch (bpp)
    {
    case SCROLL_BPP_1:
        return ((width + 7) >> 3);
    case SCROLL_BPP_4:
        return ((width + 1) >> 1);
    case SCROLL_BPP_8:
        return width;
    case SCROLL_BPP_16:
        return (width << 1);
    default:
        return ((width + 1) >> 1);
    }
}

static inline int scroll_rect_bytes(int width, int height, scroll_bpp_t bpp)
{
    return scroll_row_bytes(width, bpp) * height;
}

void ScrollBufferH(int pixels)
{
    if (!pixels)
        return;
    volatile uint8_t *s, *d, *l;
    int y;
    scroll_bpp_t bpp = scroll_get_bpp();
    int row_bytes = scroll_row_bytes(HRes, bpp);

    if (bpp == SCROLL_BPP_8 || bpp == SCROLL_BPP_16)
    {
        int bytes_per_pixel = (bpp == SCROLL_BPP_16 ? 2 : 1);
        int shift_bytes = (pixels > 0 ? pixels : -pixels) * bytes_per_pixel;
        if (pixels > 0)
        {
            for (y = 0; y < VRes; y++)
            {
                s = (WriteBuf + y * row_bytes);
                d = s + shift_bytes;
                memmove((void *)d, (void *)s, row_bytes - shift_bytes);
            }
        }
        else
        {
            pixels = -pixels;
            shift_bytes = pixels * bytes_per_pixel;
            for (y = 0; y < VRes; y++)
            {
                s = (WriteBuf + y * row_bytes);
                d = s;
                s += shift_bytes;
                memmove((void *)d, (void *)s, row_bytes - shift_bytes);
            }
        }
    }
    else
    {
        int pack_mode = (bpp == SCROLL_BPP_1 ? 1 : 0);
        int align_pixels = (bpp == SCROLL_BPP_1 ? 8 : 2);
        int abs_pixels = (pixels > 0 ? pixels : -pixels);

        if ((abs_pixels % align_pixels) == 0)
        {
            int shift_bytes = abs_pixels / align_pixels;
            if (pixels > 0)
            {
                for (y = 0; y < VRes; y++)
                {
                    s = (WriteBuf + y * row_bytes);
                    d = s + shift_bytes;
                    memmove((void *)d, (void *)s, row_bytes - shift_bytes);
                }
            }
            else
            {
                pixels = -pixels;
                shift_bytes = pixels / align_pixels;
                for (y = 0; y < VRes; y++)
                {
                    s = (WriteBuf + y * row_bytes);
                    d = s;
                    s += shift_bytes;
                    memmove((void *)d, (void *)s, row_bytes - shift_bytes);
                }
            }
            return;
        }

        volatile uint8_t *ss, *dd;
        ss = GetTempMainMemory(HRes);
        dd = GetTempMainMemory(HRes);
        if (pixels > 0)
        {
            for (y = 0; y < VRes; y++)
            {
                l = (WriteBuf + y * row_bytes);
                s = ss;
                d = dd + pixels;
                expandpixel(l, s, HRes, pack_mode);
                memcpy((void *)d, (void *)s, (HRes - pixels));
                contractpixel(dd, l, HRes, pack_mode);
            }
        }
        else
        {
            pixels = -pixels;
            for (y = 0; y < VRes; y++)
            {
                l = (WriteBuf + y * row_bytes);
                s = ss;
                d = dd;
                expandpixel(l, s, HRes, pack_mode);
                s += pixels;
                memcpy((void *)d, (void *)s, (HRes - pixels));
                contractpixel(d, l, HRes, pack_mode);
            }
        }
    }
}

void ScrollBufferV(int lines, int blank)
{
    uint8_t *s, *d;
    int y, yy;
    scroll_bpp_t bpp = scroll_get_bpp();
    int row_bytes = scroll_row_bytes(HRes, bpp);

    if (lines > 0)
    {
        for (y = 0; y < VRes - lines; y++)
        {
            yy = y + lines;
            d = (uint8_t *)(WriteBuf + y * row_bytes);
            s = (uint8_t *)(WriteBuf + yy * row_bytes);
            memcpy(d, s, row_bytes);
        }
        if (blank)
        {
            DrawRectangle(0, VRes - lines, HRes - 1, VRes - 1, gui_bcolour); // erase the line to be scrolled off
        }
    }
    else if (lines < 0)
    {
        lines = -lines;
        for (y = VRes - 1; y >= lines; y--)
        {
            yy = y - lines;
            d = (uint8_t *)(WriteBuf + y * row_bytes);
            s = (uint8_t *)(WriteBuf + yy * row_bytes);
            memcpy(d, s, row_bytes);
        }
        if (blank)
            DrawRectangle(0, 0, HRes - 1, lines - 1, gui_bcolour); // erase the line to be scrolled off
    }
}
/*  @endcond */
void packline(uint32_t *data, int width)
{
    uint8_t *s = (uint8_t *)data;
    uint8_t *d = s;
    for (int i = 0; i < width; i++)
    {
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        s++;
    }
}
/* Not static: cmd_blit (Blit.c) also loads BMPs through
   loadBMPlinecallback and stashes its state here first (declared in
   DrawInternal.h). */
s_ReadBMP *readstate;
// External line callback function
bool loadBMPlinecallback(int *imagewidth, int *imageheight, uint32_t *linedata, int *linenumber)
{
    if (*linenumber < readstate->img_y_offset || *linenumber >= readstate->img_y_offset + readstate->height)
        return true;
    packline(linedata, *imagewidth);
    unsigned char *d = readstate->output_buffer;
    d += (*linenumber - readstate->img_y_offset) * readstate->width * 3;
    unsigned char *s = (uint8_t *)linedata;
    s += readstate->img_x_offset * 3;
    memcpy(d, s, readstate->width * 3);
    return true;
}
void cmd_sprite(void)
{
    int x1, y1, w, h, bnbr;
    unsigned char *p;
    int maxW;
    int maxH;
    int newb = 0;
#ifndef PICOMITEVGA
    if (WriteBuf == NULL)
        error("Not available on physical display");
#endif
    CheckDisplay();
    maxW = HRes;
    maxH = VRes;
    if (DISPLAY_TYPE == SCREENMODE4 || DISPLAY_TYPE == SCREENMODE5)
        error("Not available for this display mode");
    if ((p = checkstring(cmdline, (unsigned char *)"SHOW SAFE")))
    {
        int layer, mode = 1;
        getcsargs(&p, 11);
        if (!(argc == 7 || argc == 9 || argc == 11))
            SyntaxError();
        if (hideall)
            error((char *)"Sprites are hidden");
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] == NULL)
            error((char *)"Buffer not in use");
        if (spritebuff[bnbr]->h == TRIANGLE_BUFFER_MARKER)
            StandardError(37);
        if (spritebuff[bnbr]->spritebuffptr != NULL)
        {
            x1 = (int)getint(argv[2], -spritebuff[bnbr]->w + 1, maxW - 1);
            y1 = (int)getint(argv[4], -spritebuff[bnbr]->h + 1, maxH - 1);
            layer = (int)getint(argv[6], 0, MAXLAYER);
            if (argc >= 9 && *argv[8])
                spritebuff[bnbr]->rotation = (char)getint(argv[8], 0, 7);
            else
                spritebuff[bnbr]->rotation = 0;
            if (spritebuff[bnbr]->rotation > 3)
            {
                mode |= 8;
                spritebuff[bnbr]->rotation &= 3;
            }
            if (argc == 11 && *argv[10])
            {
                newb = (int)getint(argv[10], 0, 1);
            }
            //            q = spritebuff[bnbr]->spritebuffptr;
            w = spritebuff[bnbr]->w;
            h = spritebuff[bnbr]->h;
            if (spritebuff[bnbr]->active)
            {
                if (newb)
                {
                    hidesafe(bnbr);
                    spritebuff[bnbr]->layer = layer;
                    layer_in_use[spritebuff[bnbr]->layer]++;
                    if (spritebuff[bnbr]->layer == 0)
                        zeroLIFOadd(bnbr);
                    else
                        LIFOadd(bnbr);
                    sprites_in_use++;
                    BlitShowBuffer(bnbr, x1, y1, mode);
                }
                else
                {
                    showsafe(bnbr, x1, y1);
                }
            }
            else
            {
                spritebuff[bnbr]->layer = layer;
                layer_in_use[spritebuff[bnbr]->layer]++;
                if (spritebuff[bnbr]->layer == 0)
                    zeroLIFOadd(bnbr);
                else
                    LIFOadd(bnbr);
                sprites_in_use++;
                BlitShowBuffer(bnbr, x1, y1, mode);
            }
            ProcessCollisions(bnbr);
            if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
                error((char *)"sprite internal error");
        }
        else
            error((char *)"Buffer not in use");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SHOW")))
    {
        int layer, mode = 1;
        getcsargs(&p, 9);
        if (!(argc == 7 || argc == 9))
            SyntaxError();
        if (hideall)
            error((char *)"Sprites are hidden");
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] == NULL)
            error((char *)"Buffer not in use");
        if (spritebuff[bnbr]->h == TRIANGLE_BUFFER_MARKER)
            StandardError(37);
        if (spritebuff[bnbr]->spritebuffptr != NULL)
        {
            x1 = (int)getint(argv[2], -spritebuff[bnbr]->w + 1, maxW - 1);
            y1 = (int)getint(argv[4], -spritebuff[bnbr]->h + 1, maxH - 1);
            layer = (int)getint(argv[6], 0, MAXLAYER);
            if (argc == 9)
                spritebuff[bnbr]->rotation = (int)getint(argv[8], 0, 7);
            else
                spritebuff[bnbr]->rotation = 0;
            if (spritebuff[bnbr]->rotation > 3)
            {
                mode |= 8;
                spritebuff[bnbr]->rotation &= 3;
            }
            w = spritebuff[bnbr]->w;
            h = spritebuff[bnbr]->h;
            if (spritebuff[bnbr]->active)
            {
                layer_in_use[spritebuff[bnbr]->layer]--;
                if (spritebuff[bnbr]->layer == 0)
                    zeroLIFOremove(bnbr);
                else
                    LIFOremove(bnbr);
                sprites_in_use--;
            }
            spritebuff[bnbr]->layer = layer;
            layer_in_use[spritebuff[bnbr]->layer]++;
            if (spritebuff[bnbr]->layer == 0)
                zeroLIFOadd(bnbr);
            else
                LIFOadd(bnbr);
            sprites_in_use++;
            //            int cursorhidden = 0;
            BlitShowBuffer(bnbr, x1, y1, mode);
            ProcessCollisions(bnbr);
            if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
                error((char *)"sprite internal error");
        }
        else
            error((char *)"Buffer not in use");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"HIDE ALL")))
    {
        if (hideall)
            error((char *)"Sprites are hidden");
        int i;
        //        int cursorhidden = 0;
        for (i = LIFOpointer - 1; i >= 0; i--)
        {
            blithide(LIFO[i]);
        }
        for (i = zeroLIFOpointer - 1; i >= 0; i--)
        {
            blithide(zeroLIFO[i]);
        }
        hideall = 1;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"RESTORE")))
    {
        if (!hideall)
            error((char *)"Sprites are not hidden");
        int i;
        //        int cursorhidden = 0;
        for (i = 0; i < zeroLIFOpointer; i++)
        {
            BlitShowBuffer(zeroLIFO[i], spritebuff[zeroLIFO[i]]->x, spritebuff[zeroLIFO[i]]->y, 0);
        }
        for (i = 0; i < LIFOpointer; i++)
        {
            if (spritebuff[LIFO[i]]->next_x != SPRITE_POS_INACTIVE)
            {
                spritebuff[LIFO[i]]->x = spritebuff[LIFO[i]]->next_x;
                spritebuff[LIFO[i]]->next_x = SPRITE_POS_INACTIVE;
            }
            if (spritebuff[LIFO[i]]->next_y != SPRITE_POS_INACTIVE)
            {
                spritebuff[LIFO[i]]->y = spritebuff[LIFO[i]]->next_y;
                spritebuff[LIFO[i]]->next_y = SPRITE_POS_INACTIVE;
            }
            BlitShowBuffer(LIFO[i], spritebuff[LIFO[i]]->x, spritebuff[LIFO[i]]->y, 0);
        }
        hideall = 0;
        ProcessCollisions(0);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"HIDE SAFE")))
    {
        getcsargs(&p, 1);
        if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
            error((char *)"sprite internal error");
        if (argc != 1)
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (hideall)
            error((char *)"Sprites are hidden");
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->spritebuffptr != NULL)
        {
            if (spritebuff[bnbr]->active)
            {
                //                int cursorhidden = 0;
                hidesafe(bnbr);
                if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
                    error((char *)"sprite internal error");
            }
            else
                error((char *)"Not Showing");
        }
        else
            error((char *)"Buffer not in use");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"HIDE")))
    {
        getcsargs(&p, 1);
        if (argc != 1)
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->spritebuffptr != NULL)
        {
            if (spritebuff[bnbr]->active)
            {
                sprites_in_use--;
                //                int cursorhidden = 0;
                blithide(bnbr);
                layer_in_use[spritebuff[bnbr]->layer]--;
                spritebuff[bnbr]->x = SPRITE_POS_INACTIVE;
                spritebuff[bnbr]->y = SPRITE_POS_INACTIVE;
                if (spritebuff[bnbr]->layer == 0)
                    zeroLIFOremove(bnbr);
                else
                    LIFOremove(bnbr);
                spritebuff[bnbr]->layer = -1;
                spritebuff[bnbr]->next_x = SPRITE_POS_INACTIVE;
                spritebuff[bnbr]->next_y = SPRITE_POS_INACTIVE;
                spritebuff[bnbr]->lastcollisions = 0;
                spritebuff[bnbr]->edges = 0;
            }
            else
                error((char *)"Not Showing");
        }
        else
            error((char *)"Buffer not in use");
        if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
            error((char *)"sprite internal error");
        //
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SWAP")))
    {
        int rbnbr = 0, mode = 2;
        int64_t master;
        signed char mymaster;
        getcsargs(&p, 5);
        if (argc < 3)
            SyntaxError();
        if (hideall)
            error((char *)"Sprites are hidden");
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (*argv[2] == '#')
            argv[2]++;
        rbnbr = (int)getint(argv[2], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] == NULL || spritebuff[bnbr]->spritebuffptr == NULL || spritebuff[bnbr]->active == false)
            error((char *)"Original buffer not displayed");
        //        if (spritebuff[bnbr]->master == -1)error((char *)"Can't swap a copy");
        if (spritebuff[rbnbr] == NULL)
            error((char *)"New buffer not defined");
        if (spritebuff[rbnbr]->active)
            error((char *)"New buffer already displayed");
        //        if (spritebuff[rbnbr]->master == -1)error((char *)"Can't swap a copy");
        if (!(spritebuff[rbnbr]->w == spritebuff[bnbr]->w && spritebuff[rbnbr]->h == spritebuff[bnbr]->h))
            error((char *)"Size mismatch");
        // copy the relevant data
        master = spritebuff[rbnbr]->master;
        mymaster = spritebuff[rbnbr]->mymaster;
        spritebuff[rbnbr]->master = spritebuff[bnbr]->master;
        spritebuff[rbnbr]->mymaster = spritebuff[bnbr]->mymaster;
        spritebuff[rbnbr]->blitstoreptr = spritebuff[bnbr]->blitstoreptr;
        spritebuff[rbnbr]->x = spritebuff[bnbr]->x;
        spritebuff[rbnbr]->y = spritebuff[bnbr]->y;
        spritebuff[rbnbr]->layer = spritebuff[bnbr]->layer;
        spritebuff[rbnbr]->lastcollisions = spritebuff[bnbr]->lastcollisions;
        spritebuff[rbnbr]->boundsleft = spritebuff[bnbr]->boundsleft;
        spritebuff[rbnbr]->boundsright = spritebuff[bnbr]->boundsright;
        spritebuff[rbnbr]->boundstop = spritebuff[bnbr]->boundstop;
        spritebuff[rbnbr]->boundsbottom = spritebuff[bnbr]->boundsbottom;
        if (spritebuff[rbnbr]->layer == 0)
            zeroLIFOswap(bnbr, rbnbr);
        else
            LIFOswap(bnbr, rbnbr);
        // "Hide" the old sprite
        spritebuff[bnbr]->master = master;
        spritebuff[bnbr]->mymaster = mymaster;
        spritebuff[bnbr]->x = SPRITE_POS_INACTIVE;
        spritebuff[bnbr]->y = SPRITE_POS_INACTIVE;
        spritebuff[bnbr]->layer = -1;
        spritebuff[bnbr]->next_x = SPRITE_POS_INACTIVE;
        spritebuff[bnbr]->next_y = SPRITE_POS_INACTIVE;
        spritebuff[bnbr]->active = 0;
        spritebuff[bnbr]->lastcollisions = 0;
        if (argc == 5)
            spritebuff[rbnbr]->rotation = (int)getint(argv[4], 0, 7);
        else
            spritebuff[rbnbr]->rotation = 0;
        if (spritebuff[rbnbr]->rotation > 3)
        {
            mode |= 8;
            spritebuff[rbnbr]->rotation &= 3;
        }
        BlitShowBuffer(rbnbr, spritebuff[rbnbr]->x, spritebuff[rbnbr]->y, mode);
        if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
            error((char *)"sprite internal error");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"READ")))
    {
        getcsargs(&p, 11);
        if (!(argc == 9))
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        x1 = (int)getinteger(argv[2]);
        y1 = (int)getinteger(argv[4]);
        w = (int)getinteger(argv[6]);
        h = (int)getinteger(argv[8]);
        if (w < 1 || h < 1)
            return;
        // Allocate sprite structure if needed (also allocates spritebuff[0])
        if (spritebuff[bnbr] == NULL)
            allocSpriteBuff(bnbr);
        if (spritebuff[bnbr]->spritebuffptr == NULL)
        {
            // Allocate both sprite buffer and blit store in one block to save memory pages
            int bufsize = (w * h + 1) >> 1;
            char *combined = (char *)GetMemory(bufsize * 2);
            spritebuff[bnbr]->spritebuffptr = combined;
            spritebuff[bnbr]->blitstoreptr = combined + bufsize;
            initSpriteBuff(bnbr, w, h);
        }
        else
        {
            if (spritebuff[bnbr]->mymaster != -1)
                error((char *)"Can't read into a copy", bnbr);
            if (spritebuff[bnbr]->master > 0)
                error((char *)"Copies exist", bnbr);
            if (!(spritebuff[bnbr]->w == w && spritebuff[bnbr]->h == h))
                error((char *)"Existing buffer is incorrect size");
        }
        ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, (unsigned char *)spritebuff[bnbr]->spritebuffptr);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"COPY")))
    {
        int cpy, nbr, c1, n1;
        getcsargs(&p, 5);
        if (argc != 5)
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->spritebuffptr != NULL)
        {
            if (*argv[2] == '#')
                argv[2]++;
            c1 = cpy = (int)getint(argv[2], 1, MAXBLITBUF);
            n1 = nbr = (int)getint(argv[4], 1, MAXBLITBUF - 1);

            while (n1)
            {
                if (spritebuff[c1] != NULL && spritebuff[c1]->spritebuffptr != NULL)
                    error((char *)"Buffer already in use %", c1);
                if (spritebuff[bnbr]->master == -1)
                    error((char *)"Can't copy a copy");
                ;
                n1--;
                c1++;
            }
            while (nbr)
            {
                // Allocate sprite structure for copy if needed
                if (spritebuff[cpy] == NULL)
                    allocSpriteBuff(cpy);
                spritebuff[cpy]->spritebuffptr = spritebuff[bnbr]->spritebuffptr;
                spritebuff[cpy]->w = spritebuff[bnbr]->w;
                spritebuff[cpy]->h = spritebuff[bnbr]->h;
                spritebuff[cpy]->blitstoreptr = (char *)GetMemory((spritebuff[cpy]->w * spritebuff[cpy]->h + 1) >> 1);
                spritebuff[cpy]->x = SPRITE_POS_INACTIVE;
                spritebuff[cpy]->y = SPRITE_POS_INACTIVE;
                spritebuff[cpy]->next_x = SPRITE_POS_INACTIVE;
                spritebuff[cpy]->next_y = SPRITE_POS_INACTIVE;
                spritebuff[cpy]->layer = -1;
                spritebuff[cpy]->mymaster = bnbr;
                spritebuff[cpy]->master = -1;
                spritebuff[cpy]->edges = 0;
                spritebuff[bnbr]->master |= ((int64_t)1 << (int64_t)cpy);
                spritebuff[bnbr]->lastcollisions = 0;
                spritebuff[cpy]->active = false;
                spritebuff[cpy]->boundsleft = spritebuff[bnbr]->boundsleft;
                spritebuff[cpy]->boundsright = spritebuff[bnbr]->boundsright;
                spritebuff[cpy]->boundstop = spritebuff[bnbr]->boundstop;
                spritebuff[cpy]->boundsbottom = spritebuff[bnbr]->boundsbottom;
                nbr--;
                cpy++;
            }
        }
        else
            error((char *)"Buffer not in use");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"LOADARRAY")))
    {
        loadarray(p);
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"LOAD")))
    {
        loadsprite(p);
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"STATIC CLEAR")))
    {
        closeallstobjects();
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"STATIC")))
    {
        getcsargs(&p, 9);
        if (*argv[0] == '#')
            argv[0]++;
        int stnum = (int)getint(argv[0], 1, MAXSTOBJECTS);
        if (argc == 3 && checkstring(argv[2], (unsigned char *)"OFF"))
        {
            // SPRITE STATIC n, OFF - remove static object
            stobjects[stnum].active = 0;
            stobjects[stnum].x = 0;
            stobjects[stnum].y = 0;
            stobjects[stnum].w = 0;
            stobjects[stnum].h = 0;
        }
        else if (argc == 9)
        {
            // SPRITE STATIC n, x, y, w, h - define static object
            stobjects[stnum].x = (short)getint(argv[2], -32768, 32767);
            stobjects[stnum].y = (short)getint(argv[4], -32768, 32767);
            stobjects[stnum].w = (short)getint(argv[6], 1, 32767);
            stobjects[stnum].h = (short)getint(argv[8], 1, 32767);
            stobjects[stnum].active = 1;
        }
        else
            SyntaxError();
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"STINTERRUPT")))
    {
        getcsargs(&p, 1);
        STCollisionInterrupt = (char *)GetIntAddress(argv[0]); // get the interrupt location
        InterruptUsed = true;
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"NOSTINTERRUPT")))
    {
        STCollisionInterrupt = NULL;
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"INTERRUPT")))
    {
        getcsargs(&p, 1);
        COLLISIONInterrupt = (char *)GetIntAddress(argv[0]); // get the interrupt location
        InterruptUsed = true;
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"NOINTERRUPT")))
    {
        COLLISIONInterrupt = NULL; // get the interrupt location
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE ALL")))
    {
        closeallsprites();
        closeallstobjects();
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
    {
        getcsargs(&p, 1);
        if (hideall)
            error((char *)"Sprites are hidden");
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF);
        if (spritebuff[bnbr] == NULL)
            error((char *)"Buffer not in use");
        if (spritebuff[bnbr]->master > 0)
            error((char *)"Copies still open");
        if (spritebuff[bnbr]->spritebuffptr != NULL)
        {
            if (spritebuff[bnbr]->active)
            {
                blithide(bnbr);
                if (spritebuff[bnbr]->layer == 0)
                    zeroLIFOremove(bnbr);
                else
                    LIFOremove(bnbr);
                layer_in_use[spritebuff[bnbr]->layer]--;
                sprites_in_use--;
            }
            if (spritebuff[bnbr]->mymaster == -1)
            {
                // Master sprite: spritebuffptr and blitstoreptr are in one block
                FreeMemorySafe((void **)&spritebuff[bnbr]->spritebuffptr);
                // boundsleft, boundsright, boundstop, boundsbottom are all in one block
                FreeMemorySafe((void **)&spritebuff[bnbr]->boundsleft);
                spritebuff[bnbr]->boundsright = NULL;
                spritebuff[bnbr]->boundstop = NULL;
                spritebuff[bnbr]->boundsbottom = NULL;
            }
            else
            {
                // Copy sprite: only blitstoreptr is allocated separately
                spritebuff[spritebuff[bnbr]->mymaster]->master &= ~(1 << bnbr);
                FreeMemorySafe((void **)&spritebuff[bnbr]->blitstoreptr);
            }
            spritebuff[bnbr]->spritebuffptr = NULL;
            spritebuff[bnbr]->blitstoreptr = NULL;
            spritebuff[bnbr]->master = -1;
            spritebuff[bnbr]->mymaster = -1;
            spritebuff[bnbr]->x = SPRITE_POS_INACTIVE;
            spritebuff[bnbr]->y = SPRITE_POS_INACTIVE;
            spritebuff[bnbr]->w = 0;
            spritebuff[bnbr]->h = 0;
            spritebuff[bnbr]->next_x = SPRITE_POS_INACTIVE;
            spritebuff[bnbr]->next_y = SPRITE_POS_INACTIVE;
            spritebuff[bnbr]->layer = -1;
            spritebuff[bnbr]->active = false;
            spritebuff[bnbr]->edges = 0;
            spritebuff[bnbr]->boundsleft = NULL;
            spritebuff[bnbr]->boundsright = NULL;
            spritebuff[bnbr]->boundstop = NULL;
            spritebuff[bnbr]->boundsbottom = NULL;
        }
        else
            error((char *)"Buffer not in use");
        if (sprites_in_use != LIFOpointer + zeroLIFOpointer || sprites_in_use != sumlayer())
            error((char *)"sprite internal error");
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"NEXT")))
    {
        getcsargs(&p, 5);
        if (!(argc == 5))
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] == NULL)
            error((char *)"Buffer not in use");
        spritebuff[bnbr]->next_x = (short)getint(argv[2], -spritebuff[bnbr]->w + 1, maxW - 1);
        spritebuff[bnbr]->next_y = (short)getint(argv[4], -spritebuff[bnbr]->h + 1, maxH - 1);
        //
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")))
    {
        int mode = 4;
        getcsargs(&p, 7);
        if (!(argc == 5 || argc == 7))
            SyntaxError();
        if (*argv[0] == '#')
            argv[0]++;
        bnbr = (int)getint(argv[0], 1, MAXBLITBUF); // get the number
        if (spritebuff[bnbr] == NULL)
            error((char *)"Buffer not in use");
        if (spritebuff[bnbr]->h == TRIANGLE_BUFFER_MARKER)
            StandardError(37);
        if (spritebuff[bnbr]->spritebuffptr != NULL)
        {
            x1 = (int)getint(argv[2], -spritebuff[bnbr]->w + 1, maxW);
            y1 = (int)getint(argv[4], -spritebuff[bnbr]->h + 1, maxH);
            if (argc == 7)
                spritebuff[bnbr]->rotation = (char)getint(argv[6], 0, 7);
            else
                spritebuff[bnbr]->rotation = 4;
            if ((spritebuff[bnbr]->rotation & 4) == 0)
                mode |= 8;
            spritebuff[bnbr]->rotation &= 3;
            w = spritebuff[bnbr]->w;
            h = spritebuff[bnbr]->h;
            //            int cursorhidden = 0;
            BlitShowBuffer(bnbr, x1, y1, mode);
        }
        else
            error((char *)"Buffer not in use");
    }
#ifdef rp2350
    else if ((p = checkstring(cmdline, (unsigned char *)"LOADPNG")))
    {
        int toggle = 0, transparent = 0, cutoff = 30, remap_colour = -1;
        int w, h;
        upng_t *upng;
        // get the command line arguments
        getcsargs(&p, 11); // this MUST be the first executable line in the function
        if (*argv[0] == '#')
            argv[0]++;                         // check if the first arg is prefixed with a #
        bnbr = getint(argv[0], 1, MAXBLITBUF); // get the buffer number
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->spritebuffptr)
            error("Buffer % in use", bnbr);
        if (argc == 0)
            StandardError(2);
        if (!InitSDCard())
            return;
        unsigned char *q = getFstring(argv[2]); // get the file name
        if (argc >= 5 && *argv[4])
        {
            int targ = getint(argv[4], -15, 15);
            if (targ < 0)
            {
                remap_colour = -targ; // RGB121 index to substitute for opaque black
                transparent = 0;
            }
            else
            {
                transparent = targ;
            }
        }
        transparent = RGB121map[transparent];
        if (argc == 7)
            cutoff = getint(argv[6], 1, 254);
        AppendDefaultExtension((char *)q, ".png");
        upng = upng_new_from_file((char *)q);
        routinechecks();
        upng_header(upng);
        w = upng_get_width(upng);
        h = upng_get_height(upng);
        // Allocate sprite structure if needed
        if (spritebuff[bnbr] == NULL)
            allocSpriteBuff(bnbr);
        // Allocate both buffers in one block to save memory pages
        {
            int bufsize = (w * h + 4) >> 1;
            char *combined = GetMemory(bufsize * 2);
            spritebuff[bnbr]->spritebuffptr = combined;
            spritebuff[bnbr]->blitstoreptr = combined + bufsize;
        }
        initSpriteBuff(bnbr, w, h);
        unsigned char *t = (unsigned char *)spritebuff[bnbr]->spritebuffptr;
        if (w > HRes || h > VRes)
        {
            upng_free(upng);
            error("Image too large");
        }
        routinechecks();
        upng_decode(upng);
        if (!(upng_get_format(upng) == UPNG_RGBA8))
        {
            upng_free(upng);
            error("Invalid format, must be RGBA8888 or indexed PNG");
        }
        unsigned char *rr;
        routinechecks();
        rr = (unsigned char *)upng_get_buffer(upng);
        unsigned char *pp = rr;
        char d[3];
        int i = w * h;
        while (i--)
        {
            d[0] = rr[2];
            d[1] = rr[1];
            d[2] = rr[0];
            if (rr[3] > cutoff)
            {
                pp[0] = d[0];
                pp[1] = d[1];
                pp[2] = d[2];
            }
            else
            {
                pp[0] = (transparent & 0xFF0000) >> 16;
                pp[1] = (transparent & 0xFF00) >> 8;
                pp[2] = (transparent & 0xFF);
            }
            {
                uint8_t c4;
                if (DISPLAY_TYPE == SCREENMODE1)
                    c4 = (((uint16_t)pp[2] + (uint16_t)pp[1] + (uint16_t)pp[0]) < 0x180) ? 0 : 0xF;
                else
                    c4 = ((pp[2] & 0x80) >> 4) | ((pp[1] & 0xC0) >> 5) | ((pp[0] & 0x80) >> 7);
                if (remap_colour >= 0 && c4 == 0 && rr[3] > cutoff)
                    c4 = (uint8_t)remap_colour;
                if (toggle)
                    *t |= (c4 << 4);
                else
                    *t = c4;
            }
            if (toggle)
                t++;
            toggle = !toggle;
            pp += 3;
            rr += 4;
        }
        upng_free(upng);
        return;
    }
#endif
    else if ((p = checkstring(cmdline, (unsigned char *)"LOADBMP")))
    {
        int toggle = 0;
        //        int xOrigin, yOrigin, xlen, ylen;
        s_ReadBMP state;
        readstate = &state; // store the various variables for use in the callback
        // get the command line arguments
        getcsargs(&p, 11); // this MUST be the first executable line in the function
        if (*argv[0] == '#')
            argv[0]++;                         // check if the first arg is prefixed with a #
        bnbr = getint(argv[0], 1, MAXBLITBUF); // get the buffer number
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->spritebuffptr)
            error("Buffer % in use", bnbr);
        if (argc == 0)
            StandardError(2);
        if (!InitSDCard())
            return;
        unsigned char *pp = getFstring(argv[2]); // get the file name
        state.img_x_offset = 0;
        state.img_y_offset = 0;
        if (argc >= 5 && *argv[4])
            state.img_x_offset = getinteger(argv[4]); // get the x origin (optional) argument
        if (argc >= 7 && *argv[6])
            state.img_y_offset = getinteger(argv[6]); // get the y origin (optional) argument
        if (state.img_x_offset < 0 || state.img_y_offset < 0)
            StandardError(34);
        state.width = state.height = -1;
        if (argc >= 9 && *argv[8])
            state.width = getinteger(argv[8]); // get the x length (optional) argument
        if (argc == 11)
            state.height = getinteger(argv[10]); // get the y length (optional) argument
        // open the file
        AppendDefaultExtension((char *)pp, ".bmp");
        BMPfnbr = FindFreeFileNbr();
        if (!BasicFileOpen((char *)pp, BMPfnbr, FA_READ))
            return;
        decodeBMPheader(&state.image_width, &state.image_height); //        BDEC_bReadHeader(&BmpDec, fnbr);
        if (state.width == -1)
            state.width = state.image_width - state.img_x_offset;
        if (state.height == -1)
            state.height = state.image_height - state.img_y_offset;
        if (state.width + state.img_x_offset > state.image_width || state.height + state.img_y_offset > state.image_height)
            StandardError(34);
        state.output_buffer = GetTempMainMemory(state.width * state.height * 3);
        // Allocate sprite structure if needed
        if (spritebuff[bnbr] == NULL)
            allocSpriteBuff(bnbr);
        // Allocate both buffers in one block to save memory pages
        {
            int bufsize = (state.width * state.height + 4) >> 1;
            char *combined = GetMemory(bufsize * 2);
            spritebuff[bnbr]->spritebuffptr = combined;
            spritebuff[bnbr]->blitstoreptr = combined + bufsize;
        }
        memset(state.output_buffer, 0xFF, state.width * state.height * 3);
        linecallback = loadBMPlinecallback;
        decodeBMP(0);
        FileClose(BMPfnbr);
        initSpriteBuff(bnbr, state.width, state.height);
        char *t = spritebuff[bnbr]->spritebuffptr;
        int i = state.width * state.height;
        unsigned char *q = state.output_buffer;
        while (i--)
        {
            if (DISPLAY_TYPE == SCREENMODE1)
            {
                if (toggle)
                {
                    *t |= (char)(((uint16_t)q[2] + (uint16_t)q[1] + (uint16_t)q[0]) < 0x180 ? 0 : 0xF0);
                }
                else
                {
                    *t = (char)(((uint16_t)q[2] + (uint16_t)q[1] + (uint16_t)q[0]) < 0x180 ? 0 : 0xF);
                }
            }
            else
            {
                if (toggle)
                {
                    *t |= ((q[2] & 0x80)) | ((q[1] & 0xC0) >> 1) | ((q[0] & 0x80) >> 3);
                }
                else
                {
                    *t = ((q[2] & 0x80) >> 4) | ((q[1] & 0xC0) >> 5) | ((q[0] & 0x80) >> 7);
                }
            }
            if (toggle)
                t++;
            toggle = !toggle;
            q += 3;
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"MOVE")))
    {
        if (hideall)
            error((char *)"Sprites are hidden");
        int i;
        //        int cursorhidden = 0;
        for (i = LIFOpointer - 1; i >= 0; i--)
            blithide(LIFO[i]);
        for (i = zeroLIFOpointer - 1; i >= 0; i--)
            blithide(zeroLIFO[i]);
        //
        for (i = 0; i < zeroLIFOpointer; i++)
        {
            if (spritebuff[zeroLIFO[i]]->next_x != SPRITE_POS_INACTIVE)
            {
                spritebuff[zeroLIFO[i]]->x = spritebuff[zeroLIFO[i]]->next_x;
                spritebuff[zeroLIFO[i]]->next_x = SPRITE_POS_INACTIVE;
            }
            if (spritebuff[zeroLIFO[i]]->next_y != SPRITE_POS_INACTIVE)
            {
                spritebuff[zeroLIFO[i]]->y = spritebuff[zeroLIFO[i]]->next_y;
                spritebuff[zeroLIFO[i]]->next_y = SPRITE_POS_INACTIVE;
            }
            BlitShowBuffer(zeroLIFO[i], spritebuff[zeroLIFO[i]]->x, spritebuff[zeroLIFO[i]]->y, 0);
        }
        for (i = 0; i < LIFOpointer; i++)
        {
            if (spritebuff[LIFO[i]]->next_x != SPRITE_POS_INACTIVE)
            {
                spritebuff[LIFO[i]]->x = spritebuff[LIFO[i]]->next_x;
                spritebuff[LIFO[i]]->next_x = SPRITE_POS_INACTIVE;
            }
            if (spritebuff[LIFO[i]]->next_y != SPRITE_POS_INACTIVE)
            {
                spritebuff[LIFO[i]]->y = spritebuff[LIFO[i]]->next_y;
                spritebuff[LIFO[i]]->next_y = SPRITE_POS_INACTIVE;
            }
            BlitShowBuffer(LIFO[i], spritebuff[LIFO[i]]->x, spritebuff[LIFO[i]]->y, 0);
        }
        ProcessCollisions(0);
    }

    else if ((p = checkstring(cmdline, (unsigned char *)"SCROLL")))
    {
        int i, n, m = 0, blank = -2, x, y;
        char *current = NULL;
        getcsargs(&p, 5);
        if (hideall)
            error((char *)"Sprites are hidden");
        x = (int)getint(argv[0], -maxW / 2 - 1, maxW);
        y = (int)getint(argv[2], -maxH / 2 - 1, maxH);
        if (argc == 5)
            blank = (int)getColour((char *)argv[4], 1);
        if (!(x == 0 && y == 0))
        {
            scroll_bpp_t bpp = scroll_get_bpp();
            int ay = (y > 0 ? y : -y);
            int ax = (x > 0 ? x : -x);
            m = scroll_rect_bytes(maxW, ay, bpp);
            n = scroll_rect_bytes(ax, maxH, bpp);
            if (n > m)
                m = n;
            if (blank == -2)
                current = (char *)GetMemory(m);
            for (i = LIFOpointer - 1; i >= 0; i--)
                blithide(LIFO[i]);
            for (i = zeroLIFOpointer - 1; i >= 0; i--)
            {
                int xs = spritebuff[zeroLIFO[i]]->x + (spritebuff[zeroLIFO[i]]->w >> 1);
                int ys = spritebuff[zeroLIFO[i]]->y + (spritebuff[zeroLIFO[i]]->h >> 1);
                blithide(zeroLIFO[i]);
                xs += x;
                if (xs >= maxW)
                    xs -= maxW;
                if (xs < 0)
                    xs += maxW;
                spritebuff[zeroLIFO[i]]->x = xs - (spritebuff[zeroLIFO[i]]->w >> 1);
                ys -= y;
                if (ys >= maxH)
                    ys -= maxH;
                if (ys < 0)
                    ys += maxH;
                spritebuff[zeroLIFO[i]]->y = ys - (spritebuff[zeroLIFO[i]]->h >> 1);
            }
            // Scroll static objects with the background
            for (i = 1; i <= MAXSTOBJECTS; i++)
            {
                if (stobjects[i].active)
                {
                    int bx = stobjects[i].x + (stobjects[i].w >> 1);
                    int by = stobjects[i].y + (stobjects[i].h >> 1);
                    bx += x;
                    if (bx >= maxW)
                        bx -= maxW;
                    if (bx < 0)
                        bx += maxW;
                    stobjects[i].x = bx - (stobjects[i].w >> 1);
                    by -= y;
                    if (by >= maxH)
                        by -= maxH;
                    if (by < 0)
                        by += maxH;
                    stobjects[i].y = by - (stobjects[i].h >> 1);
                }
            }
            if (x > 0)
            {
                if (blank == -2)
                    ReadBufferFast(maxW - x, 0, maxW - 1, maxH - 1, (unsigned char *)current);
                ScrollBufferH(x);
                if (blank == -2)
                    DrawBufferFast(0, 0, x - 1, maxH - 1, -1, (unsigned char *)current);
                else if (blank != -1)
                    DrawRectangle(0, 0, x - 1, maxH - 1, blank);
            }
            else if (x < 0)
            {
                x = -x;
                if (blank == -2)
                    ReadBufferFast(0, 0, x - 1, maxH - 1, (unsigned char *)current);
                ScrollBufferH(-x);
                if (blank == -2)
                    DrawBufferFast(maxW - x, 0, maxW - 1, maxH - 1, -1, (unsigned char *)current);
                else if (blank != -1)
                    DrawRectangle(maxW - x, 0, maxW - 1, maxH - 1, blank);
            }
            if (y > 0)
            {
                if (blank == -2)
                    ReadBufferFast(0, 0, maxW - 1, y - 1, (unsigned char *)current);
                ScrollBufferV(y, 0);
                if (blank == -2)
                    DrawBufferFast(0, maxH - y, maxW - 1, maxH - 1, -1, (unsigned char *)current);
                else if (blank != -1)
                    DrawRectangle(0, maxH - y, maxW - 1, maxH - 1, blank);
            }
            else if (y < 0)
            {
                y = -y;
                if (blank == -2)
                    ReadBufferFast(0, maxH - y, maxW - 1, maxH - 1, (unsigned char *)current);
                ScrollBufferV(-y, 0);
                if (blank == -2)
                    DrawBufferFast(0, 0, maxW - 1, y - 1, -1, (unsigned char *)current);
                else if (blank != -1)
                    DrawRectangle(0, 0, maxW - 1, y - 1, blank);
            }
            for (i = 0; i < zeroLIFOpointer; i++)
            {
                BlitShowBuffer(zeroLIFO[i], spritebuff[zeroLIFO[i]]->x, spritebuff[zeroLIFO[i]]->y, 0);
            }
            for (i = 0; i < LIFOpointer; i++)
            {
                if (spritebuff[LIFO[i]]->next_x != SPRITE_POS_INACTIVE)
                {
                    spritebuff[LIFO[i]]->x = spritebuff[LIFO[i]]->next_x;
                    spritebuff[LIFO[i]]->next_x = SPRITE_POS_INACTIVE;
                }
                if (spritebuff[LIFO[i]]->next_y != SPRITE_POS_INACTIVE)
                {
                    spritebuff[LIFO[i]]->y = spritebuff[LIFO[i]]->next_y;
                    spritebuff[LIFO[i]]->next_y = SPRITE_POS_INACTIVE;
                }

                BlitShowBuffer(LIFO[i], spritebuff[LIFO[i]]->x, spritebuff[LIFO[i]]->y, 0);
            }
            ProcessCollisions(0);
            if (current)
                FreeMemory((unsigned char *)current);
        }
    }

    else if ((p = checkstring(cmdline, (unsigned char *)"SET TRANSPARENT")))
    {
        sprite_transparent = getint((unsigned char *)p, 0, 15);
    }
    else
        SyntaxError();
}
void fun_sprite(void)
{
    int bnbr = 0, w = -1, h = -1, t = 0, x = SPRITE_POS_INACTIVE, y = SPRITE_POS_INACTIVE, l = 0, n, c = 0;
    getcsargs(&ep, 5);
    // Static object queries
    if (checkstring(argv[0], (unsigned char *)"ST"))
    {
        if (argc < 3)
            SyntaxError();
        if (checkstring(argv[2], (unsigned char *)"COLLISION"))
        {
            // SPRITE(ST, COLLISION) - which sprite hit a static object
            iret = sprite_hit_st;
            targ = T_INT;
            return;
        }
        else if (checkstring(argv[2], (unsigned char *)"OBJECT"))
        {
            // SPRITE(ST, OBJECT) - which static object was hit
            iret = st_which_collided;
            targ = T_INT;
            return;
        }
        else
        {
            // SPRITE(ST, n, property) - get static object properties
            if (argc < 5)
                SyntaxError();
            if (*argv[2] == '#')
                argv[2]++;
            int stnum = (int)getint(argv[2], 1, MAXSTOBJECTS);
            if (checkstring(argv[4], (unsigned char *)"X"))
                iret = stobjects[stnum].active ? stobjects[stnum].x : -1;
            else if (checkstring(argv[4], (unsigned char *)"Y"))
                iret = stobjects[stnum].active ? stobjects[stnum].y : -1;
            else if (checkstring(argv[4], (unsigned char *)"W"))
                iret = stobjects[stnum].active ? stobjects[stnum].w : -1;
            else if (checkstring(argv[4], (unsigned char *)"H"))
                iret = stobjects[stnum].active ? stobjects[stnum].h : -1;
            else if (checkstring(argv[4], (unsigned char *)"A"))
                iret = stobjects[stnum].active;
            else
                SyntaxError();
            targ = T_INT;
            return;
        }
    }
    if (checkstring(argv[0], (unsigned char *)"B"))
        t = 14;
    else if (checkstring(argv[0], (unsigned char *)"H"))
        t = 2;
    else if (checkstring(argv[0], (unsigned char *)"X"))
        t = 3;
    else if (checkstring(argv[0], (unsigned char *)"Y"))
        t = 4;
    else if (checkstring(argv[0], (unsigned char *)"L"))
        t = 5;
    else if (checkstring(argv[0], (unsigned char *)"C"))
        t = 6;
    else if (checkstring(argv[0], (unsigned char *)"V"))
        t = 7;
    else if (checkstring(argv[0], (unsigned char *)"T"))
        t = 8;
    else if (checkstring(argv[0], (unsigned char *)"E"))
        t = 9;
    else if (checkstring(argv[0], (unsigned char *)"D"))
        t = 10;
    else if (checkstring(argv[0], (unsigned char *)"A"))
        t = 11;
    else if (checkstring(argv[0], (unsigned char *)"N"))
        t = 12;
    else if (checkstring(argv[0], (unsigned char *)"S"))
        t = 13;
    else if (checkstring(argv[0], (unsigned char *)"W"))
        t = 1;
    else
        SyntaxError();
    if (t < 12)
    {
        if (argc < 3)
            SyntaxError();
        if (*argv[2] == '#')
            argv[2]++;
        bnbr = (int)getint(argv[2], 0, MAXBLITBUF);
        if (bnbr == 0)
        {
            if (spritebuff[0] == NULL)
                error((char *)"No sprites defined");
            if (argc == 5 && !(t == 7 || t == 10))
            {
                n = (int)getint(argv[4], 1, spritebuff[0]->collisions[0]);
                c = spritebuff[0]->collisions[n];
            }
            else
                c = spritebuff[0]->collisions[0];
        }
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->spritebuffptr != NULL)
        {
            w = spritebuff[bnbr]->w;
            h = spritebuff[bnbr]->h;
        }
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->active)
        {
            x = spritebuff[bnbr]->x;
            y = spritebuff[bnbr]->y;
            l = spritebuff[bnbr]->layer;
            if (argc == 5 && !(t == 7 || t == 10))
            {
                n = (int)getint(argv[4], 1, spritebuff[bnbr]->collisions[0]);
                c = spritebuff[bnbr]->collisions[n];
            }
            else
                c = spritebuff[bnbr]->collisions[0];
        }
    }
    if (t == 1)
        iret = w;
    else if (t == 2)
        iret = h;
    else if (t == 3)
    {
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->active)
            iret = x;
        else
            iret = SPRITE_POS_INACTIVE;
    }
    else if (t == 4)
    {
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->active)
            iret = y;
        else
            iret = SPRITE_POS_INACTIVE;
    }
    else if (t == 5)
    {
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->active)
            iret = l;
        else
            iret = -1;
    }
    else if (t == 8)
    {
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->active)
            iret = spritebuff[bnbr]->lastcollisions;
        else
            iret = 0;
    }
    else if (t == 9)
    {
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->active)
            iret = spritebuff[bnbr]->edges;
        else
            iret = 0;
    }
    else if (t == 6)
    {
        if (spritebuff[bnbr] != NULL && spritebuff[bnbr]->collisions[0])
            iret = c;
        else
            iret = -1;
    }
    else if (t == 11)
        iret = (spritebuff[bnbr] != NULL) ? (int64_t)((uint32_t)spritebuff[bnbr]->spritebuffptr) : 0;
    else if (t == 7)
    {
        int rbnbr = 0;
        int x1 = 0, y1 = 0, h1 = 0, w1 = 0;
        MMFLOAT vector;
        if (argc < 5)
            SyntaxError();
        if (*argv[4] == '#')
            argv[4]++;
        rbnbr = (int)getint(argv[4], 1, MAXBLITBUF);
        if (spritebuff[rbnbr] != NULL && spritebuff[rbnbr]->spritebuffptr != NULL)
        {
            w1 = spritebuff[rbnbr]->w;
            h1 = spritebuff[rbnbr]->h;
        }
        if (spritebuff[rbnbr] != NULL && spritebuff[rbnbr]->active)
        {
            x1 = spritebuff[rbnbr]->x;
            y1 = spritebuff[rbnbr]->y;
        }
        if (!(spritebuff[bnbr] != NULL && spritebuff[bnbr]->active && spritebuff[rbnbr] != NULL && spritebuff[rbnbr]->active))
            fret = -1.0;
        else
        {
            x += w / 2;
            y += h / 2;
            x1 += w1 / 2;
            y1 += h1 / 2;
            y1 -= y;
            x1 -= x;
            vector = atan2(y1, x1);
            vector += M_PI_2;
            if (vector < 0)
                vector += M_TWOPI;
            fret = vector;
        }
        targ = T_NBR;
        return;
    }
    else if (t == 10)
    {
        int rbnbr = 0;
        int x1 = 0, y1 = 0, h1 = 0, w1 = 0;
        if (argc < 5)
            SyntaxError();
        if (*argv[4] == '#')
            argv[4]++;
        rbnbr = (int)getint(argv[4], 1, MAXBLITBUF);
        if (spritebuff[rbnbr] != NULL && spritebuff[rbnbr]->spritebuffptr != NULL)
        {
            w1 = spritebuff[rbnbr]->w;
            h1 = spritebuff[rbnbr]->h;
        }
        if (spritebuff[rbnbr] != NULL && spritebuff[rbnbr]->active)
        {
            x1 = spritebuff[rbnbr]->x;
            y1 = spritebuff[rbnbr]->y;
        }
        if (!(spritebuff[bnbr] != NULL && spritebuff[bnbr]->active && spritebuff[rbnbr] != NULL && spritebuff[rbnbr]->active))
            fret = -1.0;
        else
        {
            x += w / 2;
            y += h / 2;
            x1 += w1 / 2;
            y1 += h1 / 2;
            fret = sqrt((x1 - x) * (x1 - x) + (y1 - y) * (y1 - y));
        }
        targ = T_NBR;
        return;
    }
    else if (t == 12)
    {
        if (argc == 3)
        {
            n = (int)getint(argv[2], 0, MAXLAYER);
            iret = layer_in_use[n];
        }
        else
            iret = sprites_in_use;
    }
    else if (t == 13)
        iret = sprite_which_collided;
    else if (t == 14)
    {
        if (argc < 3)
            SyntaxError();
        if (*argv[2] == '#')
            argv[2]++;
        bnbr = (int)getint(argv[2], 1, MAXBLITBUF);

        // Cache the sprite pointer to avoid repeated array indexing
        struct spritebuffer *sp = spritebuff[bnbr];

        if (argc == 3)
        {
            if (sp != NULL && sp->active)
            {
                // Lazy initialization of bounds arrays - only allocate when SPRITE(B is used
                if (sp->boundsleft == NULL)
                    getspritebounds(bnbr);

                int sprite_transparent2 = sprite_transparent << 4;
                int sh = sp->h;
                int sw = sp->w;
                int rowbytes = (sw + 1) >> 1; // bytes per row in packed 4-bit format

                // Cache backgroundcollision array pointer
                short *bgc = sp->backgroundcollision;

                iret = 0;
                bgc[0] = -1;       // left (max x)
                bgc[1] = SHRT_MAX; // right (min x)
                bgc[2] = -1;       // top (max y)
                bgc[3] = SHRT_MAX; // bottom (min y)
                bgc[4] = -1;       // left offset
                bgc[5] = -1;       // right offset
                bgc[6] = -1;       // top offset
                bgc[7] = -1;       // bottom offset

                char *bstore = sp->blitstoreptr;
                if (!bstore)
                    error((char *)"Buffers are empty");

                // Cache bounds arrays
                short *boundsleft = sp->boundsleft;
                short *boundsright = sp->boundsright;
                short *boundstop = sp->boundstop;
                short *boundsbottom = sp->boundsbottom;

                // Get rotation to transform coordinates for bounds checking
                // Bounds arrays are calculated from original sprite orientation
                // but sprite may be displayed flipped, so we need to transform
                // the background pixel coordinates to match original orientation
                int rotation = sp->rotation;

                // Scan all pixels in the background store
                for (int py = 0; py < sh; ++py)
                {
                    char *rowptr = bstore + py * rowbytes;

                    for (int px = 0; px < sw; ++px)
                    {
                        // Check if background pixel at (px, py) is non-transparent
                        int bytepos = px >> 1;
                        int is_transparent;
                        if (px & 1)
                        {
                            // Odd x - upper nibble
                            is_transparent = ((rowptr[bytepos] & 0xf0) == sprite_transparent2);
                        }
                        else
                        {
                            // Even x - lower nibble
                            is_transparent = ((rowptr[bytepos] & 0x0f) == sprite_transparent);
                        }

                        if (is_transparent)
                            continue;

                        // Found a non-transparent background pixel
                        // Update bounding box of all background collisions
                        if (px > bgc[0])
                            bgc[0] = px;
                        if (px < bgc[1])
                            bgc[1] = px;
                        if (py > bgc[2])
                            bgc[2] = py;
                        if (py < bgc[3])
                            bgc[3] = py;

                        if (iret == 0)
                            iret = 1;

                        // Transform coordinates to match original sprite orientation
                        // for bounds checking (bounds are from unrotated sprite)
                        int bx = (rotation & 1) ? (sw - 1 - px) : px; // horizontal flip
                        int by = (rotation & 2) ? (sh - 1 - py) : py; // vertical flip

                        // Check if this background pixel overlaps with sprite's non-transparent area
                        if (bx >= boundsleft[by] && bx <= boundsright[by])
                        {
                            int leftOffset = bx - boundsleft[by];
                            int rightOffset = boundsright[by] - bx;
                            if (leftOffset > bgc[4])
                            {
                                bgc[4] = leftOffset;
                                iret = 2;
                            }
                            if (rightOffset > bgc[5])
                            {
                                bgc[5] = rightOffset;
                                iret = 2;
                            }
                        }
                        if (by >= boundstop[bx] && by <= boundsbottom[bx])
                        {
                            int topOffset = by - boundstop[bx];
                            int bottomOffset = boundsbottom[bx] - by;
                            if (topOffset > bgc[6])
                            {
                                bgc[6] = topOffset;
                                iret = 2;
                            }
                            if (bottomOffset > bgc[7])
                            {
                                bgc[7] = bottomOffset;
                                iret = 2;
                            }
                        }
                    }
                }

                // Post-processing: convert to final format
                bgc[0] = (bgc[0] == -1) ? 0 : bgc[0] + 1;
                bgc[1] = (bgc[1] == SHRT_MAX) ? 0 : sw - bgc[1];
                bgc[2] = (bgc[2] == -1) ? 0 : bgc[2] + 1;
                bgc[3] = (bgc[3] == SHRT_MAX) ? 0 : sh - bgc[3];
                bgc[4] = (bgc[4] == -1) ? 0 : bgc[4] + 1;
                bgc[5] = (bgc[5] == -1) ? 0 : bgc[5] + 1;
                bgc[6] = (bgc[6] == -1) ? 0 : bgc[6] + 1;
                bgc[7] = (bgc[7] == -1) ? 0 : bgc[7] + 1;

                // Cleanup reporting
                if (bgc[0] == sw)
                    bgc[0] = 0;
                if (bgc[1] == sw)
                    bgc[1] = 0;
                if (bgc[2] == sh)
                    bgc[2] = 0;
                if (bgc[3] == sh)
                    bgc[3] = 0;

                if (bgc[4] != 0 && bgc[5] != 0)
                {
                    if (bgc[4] < bgc[5])
                        bgc[5] = 0;
                    else if (bgc[4] > bgc[5])
                        bgc[4] = 0;
                    else
                    { // both sides meaning that we have top bottom hit
                        bgc[4] = 0;
                        bgc[5] = 0;
                    }
                }
                if (bgc[6] != 0 && bgc[7] != 0)
                {
                    if (bgc[6] < bgc[7])
                        bgc[7] = 0;
                    else if (bgc[6] > bgc[7])
                        bgc[6] = 0;
                    else
                    { // top bottom meaning that we have left right hit
                        bgc[6] = 0;
                        bgc[7] = 0;
                    }
                }
            }
        }
        else if (argc == 5)
        {
            int side = (int)getint(argv[4], 0, 7);
            iret = sp->backgroundcollision[side];
        }
        else
            SyntaxError();
    }
    else
    {
    }
    targ = T_INT;
}

/*  @endcond */
