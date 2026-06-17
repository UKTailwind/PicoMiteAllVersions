/***********************************************************************************************************************
PicoMite MMBasic

Draw3D.c

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
 * @file Draw3D.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the DRAW3D MMBasic command and function (3D engine:
 *        quaternion transforms, depth sorting, hidden-line removal).
 *        Split out of Draw.c; the filled faces are rendered through the
 *        polygon fill engine in DrawFill.c.
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include <math.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Memory.h"
#include "DrawInternal.h"

// Hidden-line helpers are only compiled for rp2350 builds (and HDMIWEB, which
// carries the full 3D feature like HDMIBTH).
#if defined(rp2350) && (!defined(PICOMITEWEB) || defined(PICOMITEHDMIWEB))

static bool is_hiddenline_target(void)
{
    if (WriteBuf == NULL)
        return false;

    if (WriteBuf == FrameBuf || WriteBuf == LayerBuf
#ifdef PICOMITEVGA
        || WriteBuf == DisplayBuf || WriteBuf == SecondFrame
#ifdef rp2350
        || WriteBuf == SecondLayer
#endif
#endif
    )
        return true;

    return false;
}

static void hiddenline_raster_triangle(short x0, short y0, FLOAT3D iz0,
                                       short x1, short y1, FLOAT3D iz1,
                                       short x2, short y2, FLOAT3D iz2,
                                       int minx, int miny, int maxx, int maxy,
                                       FLOAT3D *zbuf, int bw)
{
    int tri_minx = x0 < x1 ? x0 : x1;
    tri_minx = tri_minx < x2 ? tri_minx : x2;
    int tri_maxx = x0 > x1 ? x0 : x1;
    tri_maxx = tri_maxx > x2 ? tri_maxx : x2;
    int tri_miny = y0 < y1 ? y0 : y1;
    tri_miny = tri_miny < y2 ? tri_miny : y2;
    int tri_maxy = y0 > y1 ? y0 : y1;
    tri_maxy = tri_maxy > y2 ? tri_maxy : y2;

    int bbminx = tri_minx > minx ? tri_minx : minx;
    int bbmaxx = tri_maxx < maxx ? tri_maxx : maxx;
    int bbminy = tri_miny > miny ? tri_miny : miny;
    int bbmaxy = tri_maxy < maxy ? tri_maxy : maxy;

    int den_i = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (den_i == 0)
        return;

    FLOAT3D inv_den = 1.0 / (FLOAT3D)den_i;
    FLOAT3D c0 = iz0 * inv_den;
    FLOAT3D c1 = iz1 * inv_den;
    FLOAT3D c2 = iz2 * inv_den;

    // Edge-function coefficients: e(x,y) = A*x + B*y + C
    int A0 = y1 - y2, B0 = x2 - x1, C0 = x1 * y2 - x2 * y1;
    int A1 = y2 - y0, B1 = x0 - x2, C1 = x2 * y0 - x0 * y2;
    int A2 = y0 - y1, B2 = x1 - x0, C2 = x0 * y1 - x1 * y0;

    int sign = den_i > 0 ? 1 : -1;

    for (int y = bbminy; y <= bbmaxy; y++)
    {
        int e0 = A0 * bbminx + B0 * y + C0;
        int e1 = A1 * bbminx + B1 * y + C1;
        int e2 = A2 * bbminx + B2 * y + C2;
        for (int x = bbminx; x <= bbmaxx; x++)
        {
            if (e0 * sign >= 0 && e1 * sign >= 0 && e2 * sign >= 0)
            {
                FLOAT3D iz = ((FLOAT3D)e0 * c0) + ((FLOAT3D)e1 * c1) + ((FLOAT3D)e2 * c2);
                int idx = (y - miny) * bw + (x - minx);
                if (iz > zbuf[idx])
                    zbuf[idx] = iz;
            }
            e0 += A0;
            e1 += A1;
            e2 += A2;
        }
    }
}

static void hiddenline_draw_edge(short x0, short y0, FLOAT3D iz0,
                                 short x1, short y1, FLOAT3D iz1,
                                 int c,
                                 int minx, int miny, int maxx, int maxy,
                                 FLOAT3D *zbuf, int bw)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int steps = dx > dy ? dx : dy;
    if (steps == 0)
    {
        if (x0 >= minx && x0 <= maxx && y0 >= miny && y0 <= maxy)
        {
            int idx = (y0 - miny) * bw + (x0 - minx);
            if (iz0 >= zbuf[idx] - 0.0005)
                DrawPixel(x0, y0, c);
        }
        return;
    }

    FLOAT3D izstep = (iz1 - iz0) / (FLOAT3D)steps;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0;
    int y = y0;
    FLOAT3D izf = iz0;

    for (int i = 0; i <= steps; i++)
    {
        if (x >= minx && x <= maxx && y >= miny && y <= maxy)
        {
            int idx = (y - miny) * bw + (x - minx);
            if (izf >= zbuf[idx] - 0.0005)
                DrawPixel(x, y, c);
        }
        int e2 = err << 1;
        if (e2 > -dy)
        {
            err -= dy;
            x += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y += sy;
        }
        izf += izstep;
    }
}

static FLOAT3D *hiddenline_zbuf_cache = NULL;
static int hiddenline_zbuf_capacity = 0;

static FLOAT3D *hiddenline_get_zbuf(int count)
{
    if (count <= 0)
        return NULL;
    if (count > hiddenline_zbuf_capacity)
    {
        if (hiddenline_zbuf_cache != NULL)
            FreeMemory((unsigned char *)hiddenline_zbuf_cache);
        hiddenline_zbuf_cache = (FLOAT3D *)GetMemory(count * sizeof(FLOAT3D));
        hiddenline_zbuf_capacity = count;
    }
    return hiddenline_zbuf_cache;
}

static void hiddenline_release_zbuf(void)
{
    if (hiddenline_zbuf_cache != NULL)
    {
        FreeMemory((unsigned char *)hiddenline_zbuf_cache);
        hiddenline_zbuf_cache = NULL;
        hiddenline_zbuf_capacity = 0;
    }
}

#endif

/* 3D graphics globals — compiled for all non-web builds plus HDMIWEB, which
   carries the full 3D feature like its sibling HDMIBTH. */
#if !defined(PICOMITEWEB) || defined(PICOMITEHDMIWEB)
struct D3D *struct3d[MAX3D + 1] = {NULL};
s_camera camera[MAXCAM + 1];
#endif

/* 3D graphics implementation (DrawPolygon … cmd_3D/fun_3D). Compiled for all
   non-web builds plus HDMIWEB, matching HDMIBTH. */
#if !defined(PICOMITEWEB) || defined(PICOMITEHDMIWEB)

void DrawPolygon(int n, short *xcoord, short *ycoord, int face)
{
    int i, facecount = struct3d[n]->facecount[face];
    int c = struct3d[n]->line[face];
    int f = struct3d[n]->fill[face];
    // first deal with outline only
    if (struct3d[n]->fill[face] == 0xFFFFFFFF)
    {
        for (i = 0; i < facecount; i++)
        {
            if (i < facecount - 1)
            {
                DrawLine(xcoord[i], ycoord[i], xcoord[i + 1], ycoord[i + 1], 1, c);
            }
            else
            {
                DrawLine(xcoord[i], ycoord[i], xcoord[0], ycoord[0], 1, c);
            }
        }
    }
    else
    {
        if (facecount == 3)
        {
            DrawTriangle(xcoord[0], ycoord[0], xcoord[1], ycoord[1], xcoord[2], ycoord[2], c, f);
        }
        else if (facecount == 4)
        {
            DrawTriangle(xcoord[0], ycoord[0], xcoord[1], ycoord[1], xcoord[2], ycoord[2], f, f);
            DrawTriangle(xcoord[0], ycoord[0], xcoord[2], ycoord[2], xcoord[3], ycoord[3], f, f);
            if (f != c)
            {
                DrawLine(xcoord[0], ycoord[0], xcoord[1], ycoord[1], 1, c);
                DrawLine(xcoord[1], ycoord[1], xcoord[2], ycoord[2], 1, c);
                DrawLine(xcoord[2], ycoord[2], xcoord[3], ycoord[3], 1, c);
                DrawLine(xcoord[0], ycoord[0], xcoord[3], ycoord[3], 1, c);
            }
        }
        else
        {
            int ymax = -1000000, ymin = 1000000;
            fill_set_pen_color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            fill_set_fill_color((f >> 16) & 0xFF, (f >> 8) & 0xFF, f & 0xFF);
            fill_begin_fill();
            main_fill_poly_vertex_count = 0;
            for (i = 0; i < facecount; i++)
            {
                main_fill_polyX[main_fill_poly_vertex_count] = (TFLOAT)xcoord[i];
                main_fill_polyY[main_fill_poly_vertex_count] = (TFLOAT)ycoord[i];
                if (main_fill_polyY[main_fill_poly_vertex_count] > ymax)
                    ymax = main_fill_polyY[main_fill_poly_vertex_count];
                if (main_fill_polyY[main_fill_poly_vertex_count] < ymin)
                    ymin = main_fill_polyY[main_fill_poly_vertex_count];
                main_fill_polyX[main_fill_poly_vertex_count] = (TFLOAT)xcoord[i];
                main_fill_poly_vertex_count++;
            }
            if (main_fill_polyY[main_fill_poly_vertex_count] != main_fill_polyY[0] || main_fill_polyX[main_fill_poly_vertex_count] != main_fill_polyX[0])
            {
                main_fill_polyX[main_fill_poly_vertex_count] = main_fill_polyX[0];
                main_fill_polyY[main_fill_poly_vertex_count] = main_fill_polyY[0];
                main_fill_poly_vertex_count++;
            }
            fill_end_fill(main_fill_poly_vertex_count - 1, ymin, ymax);
        }
    }
}

void MIPS16 Free3DMemory(int i)
{
    FreeMemorySafe((void *)&struct3d[i]->q_vertices);  // array of original vertices
    FreeMemorySafe((void *)&struct3d[i]->r_vertices);  // array of rotated vertices
    FreeMemorySafe((void *)&struct3d[i]->q_centroids); // array of original vertices
    FreeMemorySafe((void *)&struct3d[i]->r_centroids); // array of rotated vertices
    FreeMemorySafe((void *)&struct3d[i]->facecount);   // number of vertices for each face
    FreeMemorySafe((void *)&struct3d[i]->facestart);   // index into the face_x_vert table of the start of a given face
    FreeMemorySafe((void *)&struct3d[i]->fill);        // fill colours
    FreeMemorySafe((void *)&struct3d[i]->line);        // line colours
    FreeMemorySafe((void *)&struct3d[i]->colours);
    FreeMemorySafe((void *)&struct3d[i]->face_x_vert); // list of vertices for each face
    FreeMemorySafe((void *)&struct3d[i]->dots);
    FreeMemorySafe((void *)&struct3d[i]->depth);
    FreeMemorySafe((void *)&struct3d[i]->depthindex);
    FreeMemorySafe((void *)&struct3d[i]->normals);
    FreeMemorySafe((void *)&struct3d[i]->flags);
    FreeMemorySafe((void *)&struct3d[i]);
}
void MIPS16 closeall3d(void)
{
    int i;
    for (i = 0; i < MAX3D; i++)
    {
        if (struct3d[i] != NULL)
        {
            Free3DMemory(i);
        }
    }
    for (i = 1; i < 4; i++)
    {
        camera[i].viewplane = -32767;
    }
#ifdef rp2350
    hiddenline_release_zbuf();
#endif
}
void T_Mult(FLOAT3D *q1, FLOAT3D *q2, FLOAT3D *n)
{
    FLOAT3D a1 = q1[0], a2 = q2[0], b1 = q1[1], b2 = q2[1], c1 = q1[2], c2 = q2[2], d1 = q1[3], d2 = q2[3];
    n[0] = a1 * a2 - b1 * b2 - c1 * c2 - d1 * d2;
    n[1] = a1 * b2 + b1 * a2 + c1 * d2 - d1 * c2;
    n[2] = a1 * c2 - b1 * d2 + c1 * a2 + d1 * b2;
    n[3] = a1 * d2 + b1 * c2 - c1 * b2 + d1 * a2;
    n[4] = q1[4] * q2[4];
}

void T_Invert(FLOAT3D *q, FLOAT3D *n)
{
    n[0] = q[0];
    n[1] = -q[1];
    n[2] = -q[2];
    n[3] = -q[3];
    n[4] = q[4];
}

void depthsort(FLOAT3D *farray, int n, int *index)
{
    int i, j = n, s = 1;
    int t;
    FLOAT3D f;
    while (s)
    {
        s = 0;
        for (i = 1; i < j; i++)
        {
            if (farray[i] > farray[i - 1])
            {
                f = farray[i];
                farray[i] = farray[i - 1];
                farray[i - 1] = f;
                s = 1;
                if (index != NULL)
                {
                    t = index[i - 1];
                    index[i - 1] = index[i];
                    index[i] = t;
                }
            }
        }
        j--;
    }
}
void q_rotate(s_quaternion *in, s_quaternion rotate, s_quaternion *out)
{
    //	PFlt(in->x);PFltComma(in->y);PFltComma(in->z);PFltComma(in->m);PRet();
    s_quaternion temp, qtemp;
    T_Mult((FLOAT3D *)&rotate, (FLOAT3D *)in, (FLOAT3D *)&temp);
    T_Invert((FLOAT3D *)&rotate, (FLOAT3D *)&qtemp);
    T_Mult((FLOAT3D *)&temp, (FLOAT3D *)&qtemp, (FLOAT3D *)out);
    //	PFlt(out->x);PFltComma(out->y);PFltComma(out->z);PFltComma(out->m);PRet();
}
void normalise(s_vector *v)
{
    FLOAT3D n = sqrt3d((v->x) * (v->x) + (v->y) * (v->y) + (v->z) * (v->z));
    v->x /= n;
    v->y /= n;
    v->z /= n;
}
void display3d(int n, FLOAT3D x, FLOAT3D y, FLOAT3D z, int clear, int nonormals, int depthmode)
{
    s_vector ray, lighting = {0};
    s_vector p1, p2, p3, U, V;
    FLOAT3D x1, y1, z1, tmp;
    FLOAT3D at, bt, ct, t, /*A=0, B=0, */ C = 1, D = -camera[struct3d[n]->camera].viewplane;
    int maxH = VRes;
    int maxW = HRes;
    int vp, v, f, sortindex, csave = 0, fsave = 0;
#ifdef rp2350
    FLOAT3D invzcoord[MAX_3D_POLYGON_VERTICES];
    int hiddenline = (depthmode == 2);
    FLOAT3D *hlr_zbuf = NULL;
    int hlr_minx = maxW, hlr_maxx = -1, hlr_miny = maxH, hlr_maxy = -1;
    int hlr_bw = 0, hlr_bh = 0;
    short *hlr_projx = NULL, *hlr_projy = NULL;
    FLOAT3D *hlr_projiz = NULL;
    uint8_t *hlr_visible = NULL;
#endif
    if (struct3d[n]->vmax > 4)
    { // needed for polygon fill
        main_fill_polyX = (TFLOAT *)GetMemory(struct3d[n]->tot_face_x_vert * sizeof(TFLOAT));
        main_fill_polyY = (TFLOAT *)GetMemory(struct3d[n]->tot_face_x_vert * sizeof(TFLOAT));
    }
    if (struct3d[n]->xmin != 32767 && clear)
        DrawRectangle(struct3d[n]->xmin, struct3d[n]->ymin, struct3d[n]->xmax, struct3d[n]->ymax, 0);
    struct3d[n]->xmin = 32767;
    struct3d[n]->ymin = 32767;
    struct3d[n]->xmax = -32767;
    struct3d[n]->ymax = -32767;
    short xcoord[MAX_3D_POLYGON_VERTICES], ycoord[MAX_3D_POLYGON_VERTICES];
    struct3d[n]->distance = 0.0;
    for (f = 0; f < struct3d[n]->nf; f++)
    {
        // calculate the surface normals for each face
        vp = struct3d[n]->facestart[f];
        p1.x = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].m + x;
        p1.y = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].m + y;
        p1.z = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].m + z;
        p2.x = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].m + x;
        p2.y = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].m + y;
        p2.z = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].m + z;
        p3.x = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].m + x;
        p3.y = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].m + y;
        p3.z = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].m + z;
        U.x = p2.x - p1.x;
        U.y = p2.y - p1.y;
        U.z = p2.z - p1.z;
        V.x = p3.x - p1.x;
        V.y = p3.y - p1.y;
        V.z = p3.z - p1.z;
        struct3d[n]->normals[f].x = U.y * V.z - U.z * V.y;
        struct3d[n]->normals[f].y = U.z * V.x - U.x * V.z;
        struct3d[n]->normals[f].z = U.x * V.y - U.y * V.x;
        normalise(&struct3d[n]->normals[f]);
        ray.x = p1.x - camera[struct3d[n]->camera].x;
        ray.y = p1.y - camera[struct3d[n]->camera].y;
        ray.z = p1.z - camera[struct3d[n]->camera].z;
        normalise(&ray);
        lighting.x = p1.x - struct3d[n]->light.x;
        lighting.y = p1.y - struct3d[n]->light.y;
        lighting.z = p1.z - struct3d[n]->light.z;
        normalise(&lighting);
        struct3d[n]->dots[f] = ray.x * struct3d[n]->normals[f].x + ray.y * struct3d[n]->normals[f].y + ray.z * struct3d[n]->normals[f].z;
        if (depthmode == 0)
        {
            tmp = struct3d[n]->r_centroids[f].m;
            struct3d[n]->depth[f] =
                (struct3d[n]->r_centroids[f].z * tmp + z - camera[struct3d[n]->camera].z) *
                    (struct3d[n]->r_centroids[f].z * tmp + z - camera[struct3d[n]->camera].z) +
                (struct3d[n]->r_centroids[f].y * tmp + y - camera[struct3d[n]->camera].y) *
                    (struct3d[n]->r_centroids[f].y * tmp + y - camera[struct3d[n]->camera].y) +
                (struct3d[n]->r_centroids[f].x * tmp + x - camera[struct3d[n]->camera].x) *
                    (struct3d[n]->r_centroids[f].x * tmp + x - camera[struct3d[n]->camera].x);
            struct3d[n]->depthindex[f] = f;
            struct3d[n]->distance += sqrt3d(struct3d[n]->depth[f]);
        }
        else
        {
            FLOAT3D max_depth = -32767.0;
            for (v = 0; v < struct3d[n]->facecount[f]; v++)
            {
                tmp = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m;
                FLOAT3D vertex_depth =
                    (struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].z * tmp + z - camera[struct3d[n]->camera].z) *
                        (struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].z * tmp + z - camera[struct3d[n]->camera].z) +
                    (struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].y * tmp + y - camera[struct3d[n]->camera].y) *
                        (struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].y * tmp + y - camera[struct3d[n]->camera].y) +
                    (struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].x * tmp + x - camera[struct3d[n]->camera].x) *
                        (struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].x * tmp + x - camera[struct3d[n]->camera].x);
                if (vertex_depth > max_depth)
                {
                    max_depth = vertex_depth;
                }
            }
            struct3d[n]->depth[f] = max_depth;
            struct3d[n]->depthindex[f] = f;
            struct3d[n]->distance += sqrt3d(struct3d[n]->depth[f]);
        }
    }
    struct3d[n]->distance /= f;
    // sort the distances from the faces to the camera
    depthsort(struct3d[n]->depth, struct3d[n]->nf, struct3d[n]->depthindex);

#ifdef rp2350
    if (hiddenline)
    {
        hlr_projx = (short *)GetTempMainMemory(struct3d[n]->tot_face_x_vert * sizeof(short));
        hlr_projy = (short *)GetTempMainMemory(struct3d[n]->tot_face_x_vert * sizeof(short));
        hlr_projiz = (FLOAT3D *)GetTempMainMemory(struct3d[n]->tot_face_x_vert * sizeof(FLOAT3D));
        hlr_visible = (uint8_t *)GetTempMainMemory(struct3d[n]->nf);
        memset(hlr_visible, 0, struct3d[n]->nf);

        // First pass: project visible faces once and cache for depth fill and edge draw.
        for (f = 0; f < struct3d[n]->nf; f++)
        {
            sortindex = struct3d[n]->depthindex[f];
            FLOAT3D facedot = struct3d[n]->dots[sortindex];
            if (struct3d[n]->flags[sortindex] & 4)
                facedot = -facedot;
            if ((struct3d[n]->flags[sortindex] & 1) || !(nonormals || facedot < 0))
                continue;
            hlr_visible[sortindex] = 1;

            vp = struct3d[n]->facestart[sortindex];
            for (v = 0; v < struct3d[n]->facecount[sortindex]; v++)
            {
                int pi = vp + v;
                x1 = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m + x;
                y1 = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m + y;
                z1 = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m + z;
                at = x1 - camera[struct3d[n]->camera].x;
                bt = y1 - camera[struct3d[n]->camera].y;
                ct = z1 - camera[struct3d[n]->camera].z;
                if (ct > -0.0005 && ct < 0.0005)
                    ct = (ct < 0.0 ? -0.0005 : 0.0005);
                t = -(C * z1 + D) / (C * ct);
                hlr_projx[pi] = x1 + round3d(at * t) + (maxW >> 1) - camera[struct3d[n]->camera].x - camera[struct3d[n]->camera].panx;
                hlr_projy[pi] = maxH - round3d(y1 + bt * t) - 1;
                hlr_projy[pi] -= (maxH >> 1) - camera[struct3d[n]->camera].y - camera[struct3d[n]->camera].pany;
                if (ct < 0.0005)
                    ct = 0.0005;
                hlr_projiz[pi] = 1.0 / ct;

                if (hlr_projx[pi] < hlr_minx)
                    hlr_minx = hlr_projx[pi];
                if (hlr_projx[pi] > hlr_maxx)
                    hlr_maxx = hlr_projx[pi];
                if (hlr_projy[pi] < hlr_miny)
                    hlr_miny = hlr_projy[pi];
                if (hlr_projy[pi] > hlr_maxy)
                    hlr_maxy = hlr_projy[pi];

                if (clear)
                {
                    if (hlr_projx[pi] > struct3d[n]->xmax)
                        struct3d[n]->xmax = hlr_projx[pi];
                    if (hlr_projx[pi] < struct3d[n]->xmin)
                        struct3d[n]->xmin = hlr_projx[pi];
                    if (hlr_projy[pi] > struct3d[n]->ymax)
                        struct3d[n]->ymax = hlr_projy[pi];
                    if (hlr_projy[pi] < struct3d[n]->ymin)
                        struct3d[n]->ymin = hlr_projy[pi];
                }
            }
        }

        if (hlr_minx < 0)
            hlr_minx = 0;
        if (hlr_miny < 0)
            hlr_miny = 0;
        if (hlr_maxx >= maxW)
            hlr_maxx = maxW - 1;
        if (hlr_maxy >= maxH)
            hlr_maxy = maxH - 1;

        if (hlr_maxx >= hlr_minx && hlr_maxy >= hlr_miny)
        {
            hlr_bw = hlr_maxx - hlr_minx + 1;
            hlr_bh = hlr_maxy - hlr_miny + 1;
            hlr_zbuf = hiddenline_get_zbuf(hlr_bw * hlr_bh);
            memset(hlr_zbuf, 0, hlr_bw * hlr_bh * sizeof(FLOAT3D));

            // Second pass: depth prefill from all visible faces.
            for (f = 0; f < struct3d[n]->nf; f++)
            {
                sortindex = struct3d[n]->depthindex[f];
                if (!hlr_visible[sortindex])
                    continue;

                vp = struct3d[n]->facestart[sortindex];
                for (v = 1; v < struct3d[n]->facecount[sortindex] - 1; v++)
                {
                    hiddenline_raster_triangle(hlr_projx[vp], hlr_projy[vp], hlr_projiz[vp],
                                               hlr_projx[vp + v], hlr_projy[vp + v], hlr_projiz[vp + v],
                                               hlr_projx[vp + v + 1], hlr_projy[vp + v + 1], hlr_projiz[vp + v + 1],
                                               hlr_minx, hlr_miny, hlr_maxx, hlr_maxy,
                                               hlr_zbuf, hlr_bw);
                }
            }
        }
    }
#endif

#ifdef rp2350
    if (hiddenline && hlr_zbuf != NULL)
    {
        for (f = 0; f < struct3d[n]->nf; f++)
        {
            sortindex = struct3d[n]->depthindex[f];
            if (!hlr_visible[sortindex])
                continue;
            vp = struct3d[n]->facestart[sortindex];
            if (struct3d[n]->fill[sortindex] == 0xFFFFFFFF)
            {
                int vc = struct3d[n]->facecount[sortindex];
                int lc = struct3d[n]->line[sortindex];
                for (v = 0; v < vc; v++)
                {
                    int vn = (v + 1 == vc ? 0 : v + 1);
                    hiddenline_draw_edge(hlr_projx[vp + v], hlr_projy[vp + v], hlr_projiz[vp + v],
                                         hlr_projx[vp + vn], hlr_projy[vp + vn], hlr_projiz[vp + vn],
                                         lc,
                                         hlr_minx, hlr_miny, hlr_maxx, hlr_maxy,
                                         hlr_zbuf, hlr_bw);
                }
            }
            else
            {
                for (v = 0; v < struct3d[n]->facecount[sortindex]; v++)
                {
                    xcoord[v] = hlr_projx[vp + v];
                    ycoord[v] = hlr_projy[vp + v];
                }
                DrawPolygon(n, xcoord, ycoord, sortindex);
            }
        }
    }
    else
#endif
    {
        // display the forward facing faces in the order of the furthest away first
        for (f = 0; f < struct3d[n]->nf; f++)
        {
            sortindex = struct3d[n]->depthindex[f];
            vp = struct3d[n]->facestart[sortindex];
            if (struct3d[n]->flags[sortindex] & 4)
                struct3d[n]->dots[sortindex] = -struct3d[n]->dots[sortindex];
            if (nonormals || struct3d[n]->dots[sortindex] < 0)
            {
                for (v = 0; v < struct3d[n]->facecount[sortindex]; v++)
                {
                    x1 = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m + x;
                    y1 = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m + y;
                    z1 = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + v]].m + z;
                    // We now have the coordinates in real space so project them
                    at = x1 - camera[struct3d[n]->camera].x;
                    bt = y1 - camera[struct3d[n]->camera].y;
                    ct = z1 - camera[struct3d[n]->camera].z;
                    if (ct > -0.0005 && ct < 0.0005)
                        ct = (ct < 0.0 ? -0.0005 : 0.0005);
                    t = -(/*A * x1 + B * y1*/ +C * z1 + D) / (/*A * at + B * bt + */ C * ct);
                    xcoord[v] = x1 + round3d(at * t) + (maxW >> 1) - camera[struct3d[n]->camera].x - camera[struct3d[n]->camera].panx;
                    ycoord[v] = maxH - round3d(y1 + bt * t) - 1;
                    ycoord[v] -= (maxH >> 1) - camera[struct3d[n]->camera].y - camera[struct3d[n]->camera].pany;
                    if (ct < 0.0005)
                        ct = 0.0005;
#ifdef rp2350
                    invzcoord[v] = 1.0 / ct;
#endif
                    if (clear)
                    {
                        if (xcoord[v] > struct3d[n]->xmax)
                            struct3d[n]->xmax = xcoord[v];
                        if (xcoord[v] < struct3d[n]->xmin)
                            struct3d[n]->xmin = xcoord[v];
                        if (ycoord[v] > struct3d[n]->ymax)
                            struct3d[n]->ymax = ycoord[v];
                        if (ycoord[v] < struct3d[n]->ymin)
                            struct3d[n]->ymin = ycoord[v];
                    }
                }
                if ((struct3d[n]->flags[sortindex] & 1) == 0)
                {
                    if (struct3d[n]->flags[sortindex] & 10)
                    {
                        fsave = struct3d[n]->fill[sortindex];
                        csave = struct3d[n]->line[sortindex];
                        if (struct3d[n]->flags[sortindex] & 2)
                            struct3d[n]->fill[sortindex] = 0xFF0000;
                        if (struct3d[n]->flags[sortindex] & 8)
                        {
                            FLOAT3D lightratio = fabs3d(lighting.x * struct3d[n]->normals[sortindex].x + lighting.y * struct3d[n]->normals[sortindex].y + lighting.z * struct3d[n]->normals[sortindex].z);
                            lightratio = (lightratio * struct3d[n]->ambient) + struct3d[n]->ambient;
                            int red = (struct3d[n]->fill[sortindex] & 0xFF0000) >> 16;
                            int green = (struct3d[n]->fill[sortindex] & 0xFF00) >> 8;
                            int blue = (struct3d[n]->fill[sortindex] & 0xFF);
                            red = (round3d)((FLOAT3D)red * lightratio);
                            green = (round3d)((FLOAT3D)green * lightratio);
                            blue = (round3d)((FLOAT3D)blue * lightratio);
                            struct3d[n]->fill[sortindex] = (red << 16) | (green << 8) | blue;
                            red = (struct3d[n]->line[sortindex] & 0xFF0000) >> 16;
                            green = (struct3d[n]->line[sortindex] & 0xFF00) >> 8;
                            blue = (struct3d[n]->line[sortindex] & 0xFF);
                            red = (round3d)((FLOAT3D)red * lightratio);
                            green = (round3d)((FLOAT3D)green * lightratio);
                            blue = (round3d)((FLOAT3D)blue * lightratio);
                            struct3d[n]->line[sortindex] = (red << 16) | (green << 8) | blue;
                        }
                    }
#ifdef rp2350
                    if (hiddenline && hlr_zbuf != NULL && struct3d[n]->fill[sortindex] == 0xFFFFFFFF)
                    {
                        int vc = struct3d[n]->facecount[sortindex];
                        int lc = struct3d[n]->line[sortindex];
                        for (v = 0; v < vc; v++)
                        {
                            int vn = (v + 1 == vc ? 0 : v + 1);
                            hiddenline_draw_edge(xcoord[v], ycoord[v], invzcoord[v],
                                                 xcoord[vn], ycoord[vn], invzcoord[vn],
                                                 lc,
                                                 hlr_minx, hlr_miny, hlr_maxx, hlr_maxy,
                                                 hlr_zbuf, hlr_bw);
                        }
                    }
                    else
#endif
                    {
                        DrawPolygon(n, xcoord, ycoord, sortindex);
                    }
                    if (struct3d[n]->flags[sortindex] & 10)
                    {
                        struct3d[n]->fill[sortindex] = fsave;
                        struct3d[n]->line[sortindex] = csave;
                    }
                }
            }
        }
    }
    // Save information about how it was displayed for DRAW3D function and RESTORE command
    struct3d[n]->current.x = x;
    struct3d[n]->current.y = y;
    struct3d[n]->current.z = z;
    struct3d[n]->nonormals = nonormals;
    struct3d[n]->depthmode = depthmode;
    if (struct3d[n]->vmax > 4)
    { // needed for polygon fill
        FreeMemory((unsigned char *)main_fill_polyX);
        FreeMemory((unsigned char *)main_fill_polyY);
    }
}

void MIPS16 diagnose3d(int n, FLOAT3D x, FLOAT3D y, FLOAT3D z, int sort)
{
    s_vector ray, normals;
    s_vector p1, p2, p3, U, V;
    FLOAT3D tmp;
    int vp, f, sortindex;
    for (f = 0; f < struct3d[n]->nf; f++)
    {
        // calculate the surface normals for each face
        vp = struct3d[n]->facestart[f];
        p1.x = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].m + x;
        p1.y = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].m + y;
        p1.z = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 1]].m + z;
        p2.x = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].m + x;
        p2.y = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].m + y;
        p2.z = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp + 2]].m + z;
        p3.x = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].x * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].m + x;
        p3.y = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].y * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].m + y;
        p3.z = struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].z * struct3d[n]->r_vertices[struct3d[n]->face_x_vert[vp]].m + z;
        U.x = p2.x - p1.x;
        U.y = p2.y - p1.y;
        U.z = p2.z - p1.z;
        V.x = p3.x - p1.x;
        V.y = p3.y - p1.y;
        V.z = p3.z - p1.z;
        normals.x = U.y * V.z - U.z * V.y;
        normals.y = U.z * V.x - U.x * V.z;
        normals.z = U.x * V.y - U.y * V.x;
        normalise(&normals);
        ray.x = p1.x - camera[struct3d[n]->camera].x;
        ray.y = p1.y - camera[struct3d[n]->camera].y;
        ray.z = p1.z /*  -camera[struct3d[n]->camera].z*/;
        normalise(&ray);
        struct3d[n]->dots[f] = ray.x * normals.x + ray.y * normals.y + ray.z * normals.z;
        tmp = struct3d[n]->r_centroids[f].m;
        struct3d[n]->depth[f] = sqrt3d(
            (struct3d[n]->r_centroids[f].z * tmp + z - camera[struct3d[n]->camera].z) *
                (struct3d[n]->r_centroids[f].z * tmp + z - camera[struct3d[n]->camera].z) +
            (struct3d[n]->r_centroids[f].y * tmp + y - camera[struct3d[n]->camera].y) *
                (struct3d[n]->r_centroids[f].y * tmp + y - camera[struct3d[n]->camera].y) +
            (struct3d[n]->r_centroids[f].x * tmp + x - camera[struct3d[n]->camera].x) *
                (struct3d[n]->r_centroids[f].x * tmp + x - camera[struct3d[n]->camera].x));
        struct3d[n]->depthindex[f] = f;
    }
    // sort the dot products
    depthsort(struct3d[n]->depth, struct3d[n]->nf, struct3d[n]->depthindex);
    // display the forward facing faces in the order of the furthest away first
    for (f = 0; f < struct3d[n]->nf; f++)
    {
        if (sort)
            sortindex = struct3d[n]->depthindex[f];
        else
            sortindex = f;
        vp = struct3d[n]->facestart[sortindex];
        MMPrintString("Face ");
        PInt(sortindex);
        MMPrintString(" at distance ");
        PFlt(struct3d[n]->depth[f]);
        MMPrintString(" dot product is ");
        PFlt(struct3d[n]->dots[sortindex]);
        MMPrintString(" so the face is ");
        MMPrintString(struct3d[n]->dots[sortindex] > 0 ? "Hidden" : "Showing");
        PRet();
    }
}
/*  @endcond */

void MIPS16 cmd_3D(void)
{
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"CREATE")))
    {
        // parameters are
        // 3D object number (1 to MAX3D
        // # of vertices = nv
        // # of faces = nf
        // vertex structure (nv)
        // face array (face number, vertex number)
        // colours array
        // edge colour index array [nf]
        // fill colour index array [nf]
        // centroid structure [nf]
        // normals structure [nf]
        MMFLOAT *vertex;
        TFLOAT tmp;
        long long int *faces, *facecount, *facecountindex, *colours, *linecolour = NULL, *fillcolour = NULL;
        getcsargs(&p, 19);
        if (argc < 17)
            StandardError(2);
        int c, colourcount = 0, vp, v, f, fc = 0, n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] != NULL)
            error("Object already exists");
        int nv = getinteger(argv[2]);
        if (nv < 3)
            error("3D object must have a minimum of 3 vertices");
        int nf = getinteger(argv[4]);
        if (nf < 1)
            error("3D object must have a minimum of 1 face");
        int cam = getint(argv[6], 1, MAXCAM);
        if (parsefloatarray(argv[8], &vertex, 5, 2, NULL, false, NULL) < nv)
            error("Vertex array too small");
        if (parseintegerarray(argv[10], &facecount, 6, 1, NULL, false, NULL) < nf)
            error("Vertex count array too small");
        facecountindex = facecount;
        for (f = 0; f < nf; f++)
            fc += (*facecountindex++);
        if (parseintegerarray(argv[12], &faces, 7, 1, NULL, false, NULL) < fc)
            error("Face/vertex array too small");
        colourcount = parseintegerarray(argv[14], &colours, 8, 1, NULL, false, NULL);
        if (argc >= 17 && *argv[16])
        {
            if (parseintegerarray(argv[16], &linecolour, 9, 1, NULL, false, NULL) < nf)
                error("Line colour  array too small");
        }

        if (argc == 19)
        {
            if (parseintegerarray(argv[18], &fillcolour, 10, 1, NULL, false, NULL) < nf)
                error("Fill colour array too small");
        }
        // The data look valid so now create the object in memory
        struct3d[n] = GetMemory(sizeof(struct D3D));
        struct3d[n]->nf = nf;
        struct3d[n]->nv = nv;
        struct3d[n]->current.x = -32767;
        struct3d[n]->current.y = -32767;
        struct3d[n]->current.z = -32767;
        struct3d[n]->xmin = 32767;
        struct3d[n]->ymin = 32767;
        struct3d[n]->xmax = -32767;
        struct3d[n]->ymax = -32767;
        struct3d[n]->camera = cam;
        struct3d[n]->q_vertices = NULL;  // array of original vertices
        struct3d[n]->r_vertices = NULL;  // array of rotated vertices
        struct3d[n]->q_centroids = NULL; // array of original vertices
        struct3d[n]->r_centroids = NULL; // array of rotated vertices
        struct3d[n]->facecount = NULL;   // number of vertices for each face
        struct3d[n]->facestart = NULL;   // index into the face_x_vert table of the start of a given face
        struct3d[n]->fill = NULL;        // fill colours
        struct3d[n]->line = NULL;        // line colours
        struct3d[n]->colours = NULL;
        struct3d[n]->face_x_vert = NULL; // list of vertices for each face
        struct3d[n]->light.x = 0;
        struct3d[n]->light.y = 0;
        struct3d[n]->light.z = 0;
        struct3d[n]->ambient = 0;
        struct3d[n]->depthmode = 0;
        struct3d[n]->nonormals = 0;
        // load up things that have one entry per vertex
        struct3d[n]->q_vertices = GetMemory(struct3d[n]->nv * sizeof(struct t_quaternion));
        struct3d[n]->r_vertices = GetMemory(struct3d[n]->nv * sizeof(struct t_quaternion));
        for (v = 0; v < struct3d[n]->nv; v++)
        {
            FLOAT3D m = 0.0;
            struct3d[n]->q_vertices[v].x = (FLOAT3D)(*vertex++);
            m += struct3d[n]->q_vertices[v].x * struct3d[n]->q_vertices[v].x;
            struct3d[n]->q_vertices[v].y = *vertex++;
            m += struct3d[n]->q_vertices[v].y * struct3d[n]->q_vertices[v].y;
            struct3d[n]->q_vertices[v].z = *vertex++;
            m += struct3d[n]->q_vertices[v].z * struct3d[n]->q_vertices[v].z;
            if (m)
            {
                m = sqrt(m);
                struct3d[n]->q_vertices[v].x = struct3d[n]->q_vertices[v].x / m;
                struct3d[n]->q_vertices[v].y = struct3d[n]->q_vertices[v].y / m;
                struct3d[n]->q_vertices[v].z = struct3d[n]->q_vertices[v].z / m;
                struct3d[n]->q_vertices[v].w = 0.0;
                struct3d[n]->q_vertices[v].m = m;
            }
            else
            {
                struct3d[n]->q_vertices[v].x = 0;
                struct3d[n]->q_vertices[v].y = 0;
                struct3d[n]->q_vertices[v].z = 0;
                struct3d[n]->q_vertices[v].w = 0.0;
                struct3d[n]->q_vertices[v].m = 1.0;
            }
            memcpy(&struct3d[n]->r_vertices[v], &struct3d[n]->q_vertices[v], sizeof(s_quaternion));
        }
        struct3d[n]->tot_face_x_vert = 0;
        // load up things that have one entry per face
        struct3d[n]->vmax = 0;
        struct3d[n]->facecount = GetMemory(struct3d[n]->nf * sizeof(uint16_t));
        struct3d[n]->facestart = GetMemory(struct3d[n]->nf * sizeof(uint16_t));
        struct3d[n]->fill = GetMemory(struct3d[n]->nf * sizeof(uint32_t));
        struct3d[n]->line = GetMemory(struct3d[n]->nf * sizeof(uint32_t));
        struct3d[n]->r_centroids = GetMemory(struct3d[n]->nf * sizeof(struct t_quaternion));
        struct3d[n]->q_centroids = GetMemory(struct3d[n]->nf * sizeof(struct t_quaternion));
        struct3d[n]->dots = GetMemory(struct3d[n]->nf * sizeof(MMFLOAT));
        struct3d[n]->depth = GetMemory(struct3d[n]->nf * sizeof(MMFLOAT));
        struct3d[n]->flags = GetMemory(struct3d[n]->nf * sizeof(uint8_t));
        struct3d[n]->depthindex = GetMemory(struct3d[n]->nf * sizeof(int));
        struct3d[n]->normals = GetMemory(struct3d[n]->nf * sizeof(struct SVD));
        for (f = 0; f < struct3d[n]->nf; f++)
        {
            struct3d[n]->facecount[f] = *facecount++;
            if (struct3d[n]->facecount[f] < 3)
            {
                Free3DMemory(n);
                error("Vertex count less than 3 for face %", f + g_OptionBase);
            }
            if (struct3d[n]->facecount[f] > struct3d[n]->vmax)
                struct3d[n]->vmax = struct3d[n]->facecount[f];
            struct3d[n]->facestart[f] = struct3d[n]->tot_face_x_vert;
            struct3d[n]->tot_face_x_vert += struct3d[n]->facecount[f];
        }
        // load up the array that holds all the face vertex information
        struct3d[n]->face_x_vert = GetMemory(struct3d[n]->tot_face_x_vert * sizeof(uint16_t)); // allocate memory for the list of vertices per face
        struct3d[n]->colours = GetMemory(colourcount * sizeof(uint32_t));
        for (c = 0; c < colourcount; c++)
        {
            struct3d[n]->colours[c] = (uint32_t)*colours++;
        }
        for (f = 0; f < struct3d[n]->tot_face_x_vert; f++)
        {
            struct3d[n]->face_x_vert[f] = *faces++;
        }
        for (f = 0; f < struct3d[n]->nf; f++)
        {
            if (linecolour != NULL)
            {
                int index = (*linecolour++) - g_OptionBase;
                if (index >= colourcount || index < 0)
                {
                    Free3DMemory(n);
                    error("Edge colour Index %", index);
                }
                struct3d[n]->line[f] = struct3d[n]->colours[index];
            }
            else
                struct3d[n]->line[f] = gui_fcolour;
            if (fillcolour != NULL)
            {
                int index = (*fillcolour++) - g_OptionBase;
                if (index >= colourcount || index < 0)
                {
                    Free3DMemory(n);
                    error("Fill colour Index %", index);
                }
                struct3d[n]->fill[f] = struct3d[n]->colours[index];
            }
            else
                struct3d[n]->fill[f] = 0xFFFFFFFF;
            FLOAT3D x = 0, y = 0, z = 0, scale;
            vp = struct3d[n]->facestart[f];
            // calculate the centroids of each face

            for (v = 0; v < struct3d[n]->facecount[f]; v++)
            {
                tmp = struct3d[n]->q_vertices[struct3d[n]->face_x_vert[vp + v]].m;
                x += struct3d[n]->q_vertices[struct3d[n]->face_x_vert[vp + v]].x * tmp;
                y += struct3d[n]->q_vertices[struct3d[n]->face_x_vert[vp + v]].y * tmp;
                z += struct3d[n]->q_vertices[struct3d[n]->face_x_vert[vp + v]].z * tmp;
            }
            x /= (FLOAT3D)struct3d[n]->facecount[f];
            y /= (FLOAT3D)struct3d[n]->facecount[f];
            z /= (FLOAT3D)struct3d[n]->facecount[f];
            struct3d[n]->q_centroids[f].x = x;
            struct3d[n]->q_centroids[f].y = y;
            struct3d[n]->q_centroids[f].z = z;
            scale = sqrt(struct3d[n]->q_centroids[f].x * struct3d[n]->q_centroids[f].x +
                         struct3d[n]->q_centroids[f].y * struct3d[n]->q_centroids[f].y +
                         struct3d[n]->q_centroids[f].z * struct3d[n]->q_centroids[f].z);
            struct3d[n]->q_centroids[f].x /= scale;
            struct3d[n]->q_centroids[f].y /= scale;
            struct3d[n]->q_centroids[f].z /= scale;
            struct3d[n]->q_centroids[f].m = scale;
            struct3d[n]->q_centroids[f].w = 0;
            memcpy(&struct3d[n]->r_centroids[f], &struct3d[n]->q_centroids[f], sizeof(s_quaternion));
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"DIAGNOSE")))
    {
        getcsargs(&p, 9);
        if (argc < 7)
            StandardError(2);
        int n = getint(argv[0], 1, MAX3D);
        int x = getint(argv[2], -32766, 32766);
        int y = getint(argv[4], -32766, 32766);
        int z = getinteger(argv[6]);
        int sort = 1;
        if (argc == 9)
            sort = getint(argv[8], 0, 1);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        if (camera[struct3d[n]->camera].viewplane == -32767)
            error("Camera position not defined");
        diagnose3d(n, x, y, z, sort);
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"LIGHT")))
    {
        getcsargs(&p, 9);
        if (argc != 9)
            StandardError(2);
        int n = getint(argv[0], 1, MAX3D);
        struct3d[n]->light.x = getint(argv[2], -32766, 32766);
        struct3d[n]->light.y = getint(argv[4], -32766, 32766);
        struct3d[n]->light.z = getint(argv[6], -32766, 32766);
        struct3d[n]->ambient = (FLOAT3D)(getint(argv[8], 0, 100)) / 100.0;
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SHOW")))
    {
        getcsargs(&p, 11);
        if (argc < 7)
            StandardError(2);
        int n = getint(argv[0], 1, MAX3D);
        int x = getint(argv[2], -32766, 32766);
        int y = getint(argv[4], -32766, 32766);
        int z = getinteger(argv[6]);
        int nonormals = 0;
        int depthmode = 0;
        if (argc >= 9 && *argv[8])
            nonormals = getint(argv[8], 0, 1);
        if (argc == 11)
            depthmode = getint(argv[10], 0, 2);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        if (camera[struct3d[n]->camera].viewplane == -32767)
            error("Camera position not defined");
#ifndef rp2350
        if (depthmode == 2)
            error("depthmode=2 requires rp2350");
#else
        if (depthmode == 2 && !is_hiddenline_target())
            error("Hidden-line mode needs framebuffer/memory display");
#endif
        display3d(n, x, y, z, 1, nonormals, depthmode);
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"SET FLAGS")))
    {
        int i, face, nbr;
        getcsargs(&p, ((MAX_ARG_COUNT - 1) * 2) - 1);
        if ((argc & 0b11) != 0b11)
            SyntaxError();
        int n = getint(argv[0], 1, MAX3D);
        int flag = getint(argv[2], 0, 255);
        // step over the equals sign and get the value for the assignment
        for (i = 4; i < argc; i += 4)
        {
            face = getinteger(argv[i]);
            nbr = getinteger(argv[i + 2]);

            if (nbr <= 0 || nbr > struct3d[n]->nf - face)
                SyntaxError();

            while (--nbr >= 0)
            {
                struct3d[n]->flags[face + nbr] = flag;
            }
        }
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"ROTATE")))
    {
        int i, n, v, f;
        s_quaternion q1;
        MMFLOAT *q = NULL;
        getcsargs(&p, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
        if ((argc & 0x01 || argc < 3) == 0)
            StandardError(2);
        if (parsefloatarray(argv[0], &q, 1, 1, NULL, true, NULL) != 5)
            StandardErrorParam(41, 1);
        q1.w = (FLOAT3D)(*q++);
        q1.x = (FLOAT3D)(*q++);
        q1.y = (FLOAT3D)(*q++);
        q1.z = (FLOAT3D)(*q++);
        q1.m = (FLOAT3D)(*q);
        for (i = 2; i < argc; i += 2)
        {
            n = getint(argv[i], 1, MAX3D);
            if (struct3d[n] == NULL)
                StandardErrorParam(7, n);
            for (v = 0; v < struct3d[n]->nv; v++)
            {
                q_rotate(&struct3d[n]->q_vertices[v], q1, &struct3d[n]->r_vertices[v]);
            }
            for (f = 0; f < struct3d[n]->nf; f++)
            {
                q_rotate(&struct3d[n]->q_centroids[f], q1, &struct3d[n]->r_centroids[f]);
            }
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"HIDE ALL")))
    {
        for (int i = 1; i <= MAX3D; i++)
        {
            if (struct3d[i] != NULL && struct3d[i]->xmin != 32767)
            {
                DrawRectangle(struct3d[i]->xmin, struct3d[i]->ymin, struct3d[i]->xmax, struct3d[i]->ymax, 0);
                struct3d[i]->xmin = 32767;
                struct3d[i]->ymin = 32767;
                struct3d[i]->xmax = -32767;
                struct3d[i]->ymax = -32767;
            }
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"RESET")))
    {
        int i, n;
        int v, f;
        getcsargs(&p, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
        if ((argc & 0x01 || argc < 3) == 0)
            StandardError(2);
        for (i = 0; i < argc; i += 2)
        {
            n = getint(argv[i], 1, MAX3D);
            for (v = 0; v < struct3d[n]->nv; v++)
            {
                memcpy(&struct3d[n]->q_vertices[v], &struct3d[n]->r_vertices[v], sizeof(s_quaternion));
            }
            for (f = 0; f < struct3d[n]->nf; f++)
            {
                memcpy(&struct3d[n]->q_centroids[f], &struct3d[n]->r_centroids[f], sizeof(s_quaternion));
            }
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"HIDE")))
    {
        int i, n;
        getcsargs(&p, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
        if ((argc & 0x01 || argc < 3) == 0)
            StandardError(2);
        for (i = 0; i < argc; i += 2)
        {
            n = getint(argv[i], 1, MAX3D);
            if (struct3d[n] == NULL)
                StandardErrorParam(7, n);
            if (struct3d[n]->xmin == 32767)
                return;
            DrawRectangle(struct3d[n]->xmin, struct3d[n]->ymin, struct3d[n]->xmax, struct3d[n]->ymax, 0);
            struct3d[n]->xmin = 32767;
            struct3d[n]->ymin = 32767;
            struct3d[n]->xmax = -32767;
            struct3d[n]->ymax = -32767;
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"RESTORE")))
    {
        int i, n;
        getcsargs(&p, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
        if ((argc & 0x01 || argc < 3) == 0)
            StandardError(2);
        for (i = 0; i < argc; i += 2)
        {
            n = getint(argv[i], 1, MAX3D);
            if (struct3d[n] == NULL)
                StandardErrorParam(7, n);
            if (struct3d[n]->xmin != 32767)
                error("Object % is not hidden", n);
            display3d(n, struct3d[n]->current.x, struct3d[n]->current.y, struct3d[n]->current.z, 1, struct3d[n]->nonormals, struct3d[n]->depthmode);
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")))
    {
        getcsargs(&p, 11);
        if (argc < 7)
            StandardError(2);
        int n = getint(argv[0], 1, MAX3D);
        int x = getint(argv[2], -32766, 32766);
        int y = getint(argv[4], -32766, 32766);
        int z = getinteger(argv[6]);
        int nonormals = 0;
        int depthmode = 0;
        if (argc >= 9 && *argv[8])
            nonormals = getint(argv[8], 0, 1);
        if (argc == 11)
            depthmode = getint(argv[10], 0, 2);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        if (camera[struct3d[n]->camera].viewplane == -32767)
            error("Camera position not defined");
#ifndef rp2350
        if (depthmode == 2)
            error("depthmode=2 requires rp2350");
#else
        if (depthmode == 2 && !is_hiddenline_target())
            error("Hidden-line mode needs framebuffer/memory display");
#endif
        display3d(n, x, y, z, 0, nonormals, depthmode);
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE ALL")))
    {
        closeall3d();
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")))
    {
        int i, n;
        getcsargs(&p, (MAX_ARG_COUNT * 2) - 1); // macro must be the first executable stmt in a block
        if ((argc & 0x01 || argc < 3) == 0)
            StandardError(2);
        for (i = 0; i < argc; i += 2)
        {
            n = getint(argv[i], 1, MAX3D);
            if (struct3d[n] == NULL)
                StandardErrorParam(7, n);
            if (struct3d[n]->xmin != 32767)
                DrawRectangle(struct3d[n]->xmin, struct3d[n]->ymin, struct3d[n]->xmax, struct3d[n]->ymax, 0);
            Free3DMemory(n);
        }
        return;
    }
    else if ((p = checkstring(cmdline, (unsigned char *)"CAMERA")))
    {
        getcsargs(&p, 11);
        if (argc < 3)
            StandardError(2);
        int n = getint(argv[0], 1, MAXCAM);
        camera[n].viewplane = getnumber(argv[2]);
        camera[n].x = (FLOAT3D)0;
        camera[n].y = (FLOAT3D)0;
        camera[n].panx = (FLOAT3D)0;
        camera[n].pany = (FLOAT3D)0;
        camera[n].z = 0.0;
        if (argc >= 5 && *argv[4])
            camera[n].x = getnumber(argv[4]);
        if (camera[n].x > 32766 || camera[n].x < -32766)
            error("Valid is -32766 to 32766");
        if (argc >= 7 && *argv[6])
            camera[n].y = getnumber(argv[6]);
        if (camera[n].y > 32766 || camera[n].x < -32766)
            error("Valid is -32766 to 32766");
        if (argc >= 9 && *argv[8])
            camera[n].panx = getint(argv[8], -32766 - camera[n].x, 32766 - camera[n].x);
        if (argc == 11)
            camera[n].pany = getint(argv[10], -32766 - camera[n].y, 32766 - camera[n].y);
        return;
    }
    else
    {
        SyntaxError();
        ;
    }
}
void MIPS16 fun_3D(void)
{
    unsigned char *p;
    if ((p = checkstring(ep, (unsigned char *)"XMIN")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->xmin;
    }
    else if ((p = checkstring(ep, (unsigned char *)"XMAX")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->xmax;
    }
    else if ((p = checkstring(ep, (unsigned char *)"YMIN")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->ymin;
    }
    else if ((p = checkstring(ep, (unsigned char *)"YMAX")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->ymax;
    }
    else if ((p = checkstring(ep, (unsigned char *)"X")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->current.x;
    }
    else if ((p = checkstring(ep, (unsigned char *)"Y")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->current.y;
    }
    else if ((p = checkstring(ep, (unsigned char *)"DISTANCE")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->distance;
    }
    else if ((p = checkstring(ep, (unsigned char *)"Z")))
    {
        getcsargs(&p, 1);
        int n = getint(argv[0], 1, MAX3D);
        if (struct3d[n] == NULL)
            StandardErrorParam(7, n);
        fret = struct3d[n]->current.z;
    }
    else
        SyntaxError();
    ;
    targ = T_NBR;
}
#endif

/*  @endcond */
