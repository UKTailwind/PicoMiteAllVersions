/***********************************************************************************************************************
PicoMite MMBasic

Turtle.c

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
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Turtle.h"
const uint32_t greenturtle[turtlewidth * turtleheight] = {
    0, 0, 0, 0, 0, 0, GREEN, GREEN, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, GREEN, GREEN, GREEN, GREEN, 0, 0, 0, 0, 0,
    0, 0, 0, 0, GREEN, MYRTLE, GREEN, GREEN, MYRTLE, GREEN, 0, 0, 0, 0,
    0, 0, 0, 0, GREEN, GREEN, GREEN, GREEN, GREEN, GREEN, 0, 0, 0, 0,
    0, 0, 0, 0, 0, GREEN, GREEN, GREEN, GREEN, 0, 0, 0, 0, 0,
    0, GREEN, GREEN, 0, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, 0, GREEN, GREEN, 0,
    GREEN, GREEN, GREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, GREEN, GREEN, GREEN,
    GREEN, GREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, GREEN, GREEN,
    GREEN, 0, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, 0, GREEN,
    0, 0, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, 0, 0,
    0, 0, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, 0, 0,
    0, 0, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, 0, 0,
    0, 0, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MYRTLE, MYRTLE, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, 0, 0,
    0, 0, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, 0, 0,
    0, GREEN, GREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, GREEN, GREEN, 0,
    0, GREEN, GREEN, 0, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, MIDGREEN, 0, GREEN, GREEN, 0,
    0, GREEN, GREEN, 0, 0, 0, GREEN, GREEN, 0, 0, 0, GREEN, GREEN, 0,
    0, 0, GREEN, 0, 0, 0, GREEN, 0, 0, 0, 0, GREEN, 0, 0,
    0, 0, GREEN, 0, 0, 0, GREEN, 0, 0, 0, 0, GREEN, 0, 0};

// External drawing functions - you'll need to provide these

// Pattern definitions (8x8 patterns) - Extended set
const uint8_t fill_patterns[][8] = {
    // Original patterns (0-7)
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // 0: Solid
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}, // 1: Checkerboard
    {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88}, // 2: Vertical lines
    {0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}, // 3: Horizontal lines
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}, // 4: Diagonal cross
    {0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88}, // 5: Diagonal stripes
    {0xC3, 0xC3, 0x3C, 0x3C, 0xC3, 0xC3, 0x3C, 0x3C}, // 6: Crosshatch
    {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01}, // 7: Diagonal fine

    // New patterns (8-23)
    {0x99, 0x66, 0x99, 0x66, 0x99, 0x66, 0x99, 0x66}, // 8: Dense checkerboard
    {0x92, 0x49, 0x24, 0x92, 0x49, 0x24, 0x92, 0x49}, // 9: Diagonal right medium
    {0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0x49, 0x92}, // 10: Diagonal left medium
    {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, // 11: Vertical lines medium
    {0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00}, // 12: Horizontal lines medium
    {0xF0, 0xF0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0F, 0x0F}, // 13: Large checkerboard
    {0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00}, // 14: Dotted vertical
    {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00}, // 15: Horizontal stripes tight
    {0x81, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81, 0xFF}, // 16: Grid
    {0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55}, // 17: Weave pattern
    {0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18}, // 18: Diamond
    {0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF}, // 19: Gradient diagonal
    {0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF}, // 20: Gradient diagonal reverse
    {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF}, // 21: Border/frame
    {0xC0, 0xC0, 0xC0, 0xC0, 0x03, 0x03, 0x03, 0x03}, // 22: Vertical split
    {0x66, 0x99, 0x99, 0x66, 0x66, 0x99, 0x99, 0x66}, // 23: Woven
    {0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00}, // 24: Sparse dots
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}, // 25: Diagonal very fine
    {0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00}, // 26: Arrow up
    {0xDB, 0xDB, 0xDB, 0x00, 0xDB, 0xDB, 0xDB, 0x00}, // 27: Dense dots
    {0xE7, 0xC3, 0x81, 0x00, 0x81, 0xC3, 0xE7, 0xFF}, // 28: Chevron
    {0x18, 0x24, 0x42, 0x81, 0x81, 0x42, 0x24, 0x18}, // 29: Diamond hollow
    {0x3C, 0x42, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C}, // 30: Circle
    {0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E}, // 31: Circle filled
};
static TurtleState turtle = {
    .x = silly_high, .y = silly_high, // Screen center
    .heading = 0,
    .pen_down = 1,
    .pen_color = 0xFFFFFF,
    .pen_width = 1,
    .fill_color = 0xFFFFFF,
    .fill_enabled = 0,
    .fill_pattern = 0,
    .visible = 1,
    .cursor_size = 10,
    .cursor_color = 0x00FF00,
    .cursor_x = 0,
    .cursor_y = 0,
    .cursor_heading = 0,
    .cursor_drawn = 0,
    .has_buffer_support = 0, // Will be set on first call
    .stack_ptr = 0,
    .cursor_buffer = NULL,
    .poly_count = 0,
    .poly_recording = 0};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
void turtle_init(void)
{
    turtle_reset(&turtle, 0);
}

static float normalize_heading(float heading)
{
    // Normalize heading to 0-360 range
    while (heading < 0)
        heading += 360.0;
    while (heading >= 360.0)
        heading -= 360.0;
    return heading;
}
/*****************************************************************************************
 * Turtle cursor erase/draw support (uses new heading convention: 0° = up, 90° = right)
 *****************************************************************************************/
void erase_turtle_cursor(TurtleState *t)
{
    if (!t->cursor_drawn)
        return;
    if (t->has_buffer_support)
    {
        DrawBuffer(t->cursor_buffer_x1, t->cursor_buffer_y1,
                   t->cursor_buffer_x2, t->cursor_buffer_y2,
                   t->cursor_buffer);
        FreeMemorySafe((void **)&t->cursor_buffer);
    }
    else
    {
        float rad = t->cursor_heading * M_PI / 180.0;
        int x1 = t->cursor_x + (int)(t->cursor_size * sin(rad));
        int y1 = t->cursor_y - (int)(t->cursor_size * cos(rad));

        float angle_left = (t->cursor_heading + 150) * M_PI / 180.0;
        int x2 = t->cursor_x + (int)((t->cursor_size * 0.6) * sin(angle_left));
        int y2 = t->cursor_y - (int)((t->cursor_size * 0.6) * cos(angle_left));

        float angle_right = (t->cursor_heading - 150) * M_PI / 180.0;
        int x3 = t->cursor_x + (int)((t->cursor_size * 0.6) * sin(angle_right));
        int y3 = t->cursor_y - (int)((t->cursor_size * 0.6) * cos(angle_right));

        DrawLine(x1, y1, x2, y2, 1, 0);
        DrawLine(x2, y2, x3, y3, 1, 0);
        DrawLine(x3, y3, x1, y1, 1, 0);
    }
    t->cursor_drawn = 0;
}

void draw_turtle_cursor(TurtleState *t)
{
    static int cursorsize = 0, cursorcolor = 0;
    if (!t->visible)
        return;

    int new_cx = (int)t->x, new_cy = (int)t->y;
    float new_heading = t->heading;
    int needs_redraw = !t->cursor_drawn ||
                       (new_cx != t->cursor_x) ||
                       (new_cy != t->cursor_y) ||
                       (new_heading != t->cursor_heading) ||
                       (t->cursor_size != cursorsize) ||
                       (t->cursor_color != cursorcolor);
    cursorsize = t->cursor_size;
    cursorcolor = t->cursor_color;

    if (!needs_redraw)
        return;

    if (t->cursor_drawn)
        erase_turtle_cursor(t);

    float rad = new_heading * M_PI / 180.0;
    int x_front = new_cx + (int)(t->cursor_size * sin(rad));
    int y_front = new_cy - (int)(t->cursor_size * cos(rad));

    float angle_left = (new_heading + 150) * M_PI / 180.0;
    int x_left = new_cx + (int)((t->cursor_size * 0.6) * sin(angle_left));
    int y_left = new_cy - (int)((t->cursor_size * 0.6) * cos(angle_left));

    float angle_right = (new_heading - 150) * M_PI / 180.0;
    int x_right = new_cx + (int)((t->cursor_size * 0.6) * sin(angle_right));
    int y_right = new_cy - (int)((t->cursor_size * 0.6) * cos(angle_right));

    // STEP 3: Calculate bounding box from vertices
    int min_x = x_front;
    int max_x = x_front;
    int min_y = y_front;
    int max_y = y_front;

    if (x_left < min_x)
        min_x = x_left;
    if (x_left > max_x)
        max_x = x_left;
    if (x_right < min_x)
        min_x = x_right;
    if (x_right > max_x)
        max_x = x_right;

    if (y_left < min_y)
        min_y = y_left;
    if (y_left > max_y)
        max_y = y_left;
    if (y_right < min_y)
        min_y = y_right;
    if (y_right > max_y)
        max_y = y_right;

    // Add margin for line width
    min_x -= 2;
    min_y -= 2;
    max_x += 2;
    max_y += 2;

    // Clamp to screen bounds
    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x >= HRes)
        max_x = HRes - 1;
    if (max_y >= VRes)
        max_y = VRes - 1;

    // STEP 4: Save background at new position
    if (t->has_buffer_support)
    {
        int width = max_x - min_x + 1;
        int height = max_y - min_y + 1;
        if (!t->cursor_buffer)
            t->cursor_buffer = GetMemory(width * height * 3);
        ReadBuffer(min_x, min_y, max_x, max_y, t->cursor_buffer);
        t->cursor_buffer_x1 = min_x;
        t->cursor_buffer_y1 = min_y;
        t->cursor_buffer_x2 = max_x;
        t->cursor_buffer_y2 = max_y;
    }
    DrawLine(x_front, y_front, x_left, y_left, 1, t->cursor_color);
    DrawLine(x_left, y_left, x_right, y_right, 1, t->cursor_color);
    DrawLine(x_right, y_right, x_front, y_front, 1, t->cursor_color);

    t->cursor_x = new_cx;
    t->cursor_y = new_cy;
    t->cursor_heading = new_heading;
    t->cursor_drawn = 1;
}
// Movement functions
/*****************************************************************************************
 * Movement functions
 *****************************************************************************************/
void turtle_forward(TurtleState *t, float distance)
{
    if (fabs(distance) < 0.001)
        return;
    float old_x = t->x, old_y = t->y;
    float rad = t->heading * M_PI / 180.0;

    t->x += distance * sin(rad);
    t->y -= distance * cos(rad);

    if (t->pen_down)
        DrawLine((int)old_x, (int)old_y, (int)t->x, (int)t->y, -(t->pen_width), t->pen_color);

    if (t->poly_recording && t->poly_count < MAX_POLYGON_POINTS - 1)
    {
        t->poly_points_x[t->poly_count] = (int)t->x;
        t->poly_points_y[t->poly_count] = (int)t->y;
        t->poly_count++;
    }
    if (t->visible)
        draw_turtle_cursor(t);
}

void turtle_goto(TurtleState *t, float new_x, float new_y)
{
    float old_x = t->x;
    float old_y = t->y;

    t->x = new_x;
    t->y = new_y;

    if (t->pen_down)
    {
        DrawLine((int)old_x, (int)old_y, (int)new_x, (int)new_y,
                 -(t->pen_width), t->pen_color);
    }

    // Record point for polygon fill
    if (t->poly_recording && t->poly_count < MAX_POLYGON_POINTS - 1)
    {
        t->poly_points_x[t->poly_count] = (int)new_x;
        t->poly_points_y[t->poly_count] = (int)new_y;
        t->poly_count++;
    }

    if (t->visible)
    {
        draw_turtle_cursor(t);
    }
}

void turtle_arc(TurtleState *t, float radius, float angle)
{
    // Draw arc with given radius and angle
    // Positive angle = left turn, negative = right turn

    int segments = (int)(fabs(angle) / 5.0) + 1; // ~5 degrees per segment
    if (segments < 4)
        segments = 4;

    float angle_step = angle / segments;
    float distance = 2.0 * radius * sin((angle_step * M_PI / 180.0) / 2.0);

    for (int i = 0; i < segments; i++)
    {
        turtle_forward(t, distance);
        t->heading += angle_step;
        t->heading = normalize_heading(t->heading);
    }
}

void turtle_bezier(TurtleState *t, float cp1_dist, float cp1_angle,
                   float cp2_dist, float cp2_angle, float end_dist, float end_angle)
{
    float p0_x = t->x, p0_y = t->y;

    // First control point: offset by cp1_angle from current heading
    float rad1 = (t->heading + cp1_angle) * M_PI / 180.0;
    float p1_x = p0_x + cp1_dist * sin(rad1);
    float p1_y = p0_y - cp1_dist * cos(rad1);

    // Second control point: offset by cp2_angle from current heading
    float rad2 = (t->heading + cp2_angle) * M_PI / 180.0;
    float p2_x = p0_x + cp2_dist * sin(rad2);
    float p2_y = p0_y - cp2_dist * cos(rad2);

    // End point: offset by end_angle from current heading
    float rad3 = (t->heading + end_angle) * M_PI / 180.0;
    float p3_x = p0_x + end_dist * sin(rad3);
    float p3_y = p0_y - end_dist * cos(rad3);

    int segments = 20;
    float prev_x = p0_x, prev_y = p0_y;
    for (int i = 1; i <= segments; i++)
    {
        float t_param = (float)i / segments;
        float u = 1.0 - t_param;
        float x = u * u * u * p0_x + 3 * u * u * t_param * p1_x +
                  3 * u * t_param * t_param * p2_x + t_param * t_param * t_param * p3_x;
        float y = u * u * u * p0_y + 3 * u * u * t_param * p1_y +
                  3 * u * t_param * t_param * p2_y + t_param * t_param * t_param * p3_y;
        if (t->pen_down)
            DrawLine((int)prev_x, (int)prev_y, (int)x, (int)y, -(t->pen_width), t->pen_color);
        prev_x = x;
        prev_y = y;
    }
    t->x = p3_x;
    t->y = p3_y;
    // Update heading to end_angle direction
    t->heading = fmod(t->heading + end_angle + 360.0, 360.0);
    if (t->visible)
        draw_turtle_cursor(t);
}
void turtle_wedge(TurtleState *t, float radius, float start_angle, float end_angle)
{
    // Draw filled wedge (pie slice)
    int cx = (int)t->x;
    int cy = (int)t->y;

    // Convert to absolute angles
    float abs_start = t->heading + start_angle;
    float abs_end = t->heading + end_angle;

    // Create polygon for the wedge
    int poly_x[128], poly_y[128];
    int poly_count = 0;

    // Center point
    poly_x[poly_count] = cx;
    poly_y[poly_count] = cy;
    poly_count++;

    // Arc points
    int segments = (int)(fabs(end_angle - start_angle) / 5.0) + 1;
    for (int i = 0; i <= segments; i++)
    {
        float angle = abs_start + (abs_end - abs_start) * i / segments;
        float rad = angle * M_PI / 180.0;
        poly_x[poly_count] = cx + (int)(radius * cos(rad));
        poly_y[poly_count] = cy - (int)(radius * sin(rad));
        poly_count++;
    }

    // Fill the wedge
    if (t->fill_enabled)
    {
        fill_polygon_scanline(poly_x, poly_y, poly_count, t->fill_color);
    }

    // Draw outline
    if (t->pen_down)
    {
        // Arc
        for (int i = 1; i < poly_count - 1; i++)
        {
            DrawLine(poly_x[i], poly_y[i], poly_x[i + 1], poly_y[i + 1],
                     -(t->pen_width), t->pen_color);
        }
        // Radii
        DrawLine(cx, cy, poly_x[1], poly_y[1], t->pen_width, t->pen_color);
        DrawLine(cx, cy, poly_x[poly_count - 1], poly_y[poly_count - 1],
                 -(t->pen_width), t->pen_color);
    }
}

// Polygon fill functions
void fill_polygon_scanline(int *x, int *y, int count, int color)
{
    // Find bounding box
    int min_y = y[0], max_y = y[0];
    for (int i = 1; i < count; i++)
    {
        if (y[i] < min_y)
            min_y = y[i];
        if (y[i] > max_y)
            max_y = y[i];
    }

    // For each scanline
    for (int scan_y = min_y; scan_y <= max_y; scan_y++)
    {
        int intersections[256];
        int inter_count = 0;

        // Find intersections with polygon edges
        for (int i = 0; i < count; i++)
        {
            int next = (i + 1) % count;
            int y1 = y[i];
            int y2 = y[next];

            if ((y1 <= scan_y && y2 > scan_y) ||
                (y2 <= scan_y && y1 > scan_y))
            {

                int x1 = x[i];
                int x2 = x[next];
                int inter_x = x1 + (scan_y - y1) * (x2 - x1) / (y2 - y1);

                if (inter_count < 256)
                {
                    intersections[inter_count++] = inter_x;
                }
            }
        }

        // Sort intersections
        for (int i = 0; i < inter_count - 1; i++)
        {
            for (int j = i + 1; j < inter_count; j++)
            {
                if (intersections[i] > intersections[j])
                {
                    int temp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = temp;
                }
            }
        }

        // Fill between pairs
        for (int i = 0; i < inter_count - 1; i += 2)
        {
            DrawLine(intersections[i], scan_y,
                     intersections[i + 1], scan_y, 1, color);
        }
    }
}

void fill_polygon_pattern(int *x, int *y, int count, int color, int pattern)
{
    if (pattern < 0 || pattern >= 8)
        pattern = 0;

    // Find bounding box
    int min_y = y[0], max_y = y[0];
    for (int i = 1; i < count; i++)
    {
        if (y[i] < min_y)
            min_y = y[i];
        if (y[i] > max_y)
            max_y = y[i];
    }

    for (int scan_y = min_y; scan_y <= max_y; scan_y++)
    {
        int intersections[256];
        int inter_count = 0;

        for (int i = 0; i < count; i++)
        {
            int next = (i + 1) % count;
            int y1 = y[i], y2 = y[next];

            if ((y1 <= scan_y && y2 > scan_y) ||
                (y2 <= scan_y && y1 > scan_y))
            {
                int x1 = x[i], x2 = x[next];
                int inter_x = x1 + (scan_y - y1) * (x2 - x1) / (y2 - y1);
                if (inter_count < 256)
                {
                    intersections[inter_count++] = inter_x;
                }
            }
        }

        for (int i = 0; i < inter_count - 1; i++)
        {
            for (int j = i + 1; j < inter_count; j++)
            {
                if (intersections[i] > intersections[j])
                {
                    int temp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = temp;
                }
            }
        }

        // Draw with pattern
        for (int i = 0; i < inter_count - 1; i += 2)
        {
            uint8_t pattern_row = fill_patterns[pattern][scan_y & 7];
            for (int px = intersections[i]; px <= intersections[i + 1]; px++)
            {
                if (pattern_row & (1 << (px & 7)))
                {
                    DrawPixel(px, scan_y, color);
                }
            }
        }
    }
}

void draw_filled_polygon(TurtleState *t)
{
    if (t->poly_count < 3)
        return;
    if (!(t->poly_points_x[0] == t->poly_points_x[t->poly_count]) &&
        (t->poly_points_y[0] == t->poly_points_y[t->poly_count]))
    { // close the polygon
        if (t->poly_count < MAX_POLYGON_POINTS - 1)
        {
            t->poly_points_x[t->poly_count] = t->poly_points_x[0];
            t->poly_points_y[t->poly_count] = t->poly_points_y[0];
            t->poly_count++;
        }
        else
            error("Polygon can't be closed");
    }

    if (t->fill_pattern == 0)
    {
        // Solid fill
        fill_polygon_scanline(t->poly_points_x, t->poly_points_y,
                              t->poly_count, t->fill_color);
    }
    else
    {
        // Pattern fill
        fill_polygon_pattern(t->poly_points_x, t->poly_points_y,
                             t->poly_count, t->fill_color, t->fill_pattern);
    }

    // Optionally redraw outline
    if (t->pen_down)
    {
        for (int i = 0; i < t->poly_count - 1; i++)
        {
            DrawLine(t->poly_points_x[i], t->poly_points_y[i],
                     t->poly_points_x[i + 1], t->poly_points_y[i + 1],
                     -(t->pen_width), t->pen_color);
        }
        // Close the polygon
        DrawLine(t->poly_points_x[t->poly_count - 1],
                 t->poly_points_y[t->poly_count - 1],
                 t->poly_points_x[0], t->poly_points_y[0],
                 -(t->pen_width), t->pen_color);
    }
}

float getangle(unsigned char *tp)
{
    return normalize_heading(getnumber(tp));
}
void turtle_rectangle(TurtleState *t, float width, float height)
{
    float rad = t->heading * M_PI / 180.0;
    float rad_perp = (t->heading + 90) * M_PI / 180.0;

    float dx_width = (width / 2) * sin(rad);
    float dy_width = (width / 2) * cos(rad);
    float dx_height = (height / 2) * sin(rad_perp);
    float dy_height = (height / 2) * cos(rad_perp);

    float corners_x[4], corners_y[4];
    int poly_x[4], poly_y[4];

    corners_x[0] = t->x - dx_width - dx_height;
    corners_y[0] = t->y + dy_width + dy_height;
    corners_x[1] = t->x + dx_width - dx_height;
    corners_y[1] = t->y - dy_width + dy_height;
    corners_x[2] = t->x + dx_width + dx_height;
    corners_y[2] = t->y - dy_width - dy_height;
    corners_x[3] = t->x - dx_width + dx_height;
    corners_y[3] = t->y + dy_width - dy_height;

    for (int i = 0; i < 4; i++)
    {
        poly_x[i] = (int)(corners_x[i] + 0.5f);
        poly_y[i] = (int)(corners_y[i] + 0.5f);
    }

    if (t->fill_enabled)
    {
        if (t->fill_pattern == 0)
            fill_polygon_scanline(poly_x, poly_y, 4, t->fill_color);
        else
            fill_polygon_pattern(poly_x, poly_y, 4, t->fill_color, t->fill_pattern);
    }

    if (t->pen_down)
    {
        for (int i = 0; i < 4; i++)
        {
            int next = (i + 1) % 4;
            DrawLine(poly_x[i], poly_y[i], poly_x[next], poly_y[next], t->pen_width, t->pen_color);
        }
    }
}
void DrawCircleFilled_Pattern(int cx, int cy, int radius, int color, int pattern)
{
    if (radius <= 0)
        return;
    if (pattern < 0 || pattern >= NUM_PATTERNS)
        pattern = 0;

    // For solid fill or pattern 0, use existing function
    if (pattern == 0)
    {
        { // Check if function exists
            DrawCircle(cx, cy, radius, 0, color, color, 1.0);
        }
        return;
    }

    // Pattern fill - simple scanline method
    int rsquared = radius * radius;
    for (int dy = -radius; dy <= radius; dy++)
    {
        int y = cy + dy;
        int dx_max = (int)sqrt(rsquared - dy * dy);

        int x1 = cx - dx_max;
        int x2 = cx + dx_max;

        // Draw patterned line
        uint8_t pattern_row = fill_patterns[pattern][y & 7];
        for (int px = x1; px <= x2; px++)
        {
            if (pattern_row & (1 << (px & 7)))
            {
                DrawPixel(px, y, color);
            }
        }
    }
}
void DrawRectangleFilled_Pattern(int x, int y, int width, int height, int color, int pattern)
{
    if (pattern < 0 || pattern >= 8)
        pattern = 0;

    if (pattern == 0)
    {
        // Solid fill - use existing function if available
        DrawRectangle(x, y, x + width - 1, y + height - 1, color);
    }
    else
    {
        // Pattern fill
        for (int py = y; py < y + height; py++)
        {
            uint8_t pattern_row = fill_patterns[pattern][py & 7];
            for (int px = x; px < x + width; px++)
            {
                if (pattern_row & (1 << (px & 7)))
                {
                    DrawPixel(px, py, color);
                }
            }
        }
    }
}
void turtle_reset(TurtleState *t, bool showturtle)
{
    if (t->visible && t->cursor_drawn)
    {
        t->visible = 0;
        erase_turtle_cursor(t);
    }
    ClearScreen(0);
    t->x = HRes / 2;
    t->y = VRes / 2;
    t->heading = 0;
    t->cursor_heading = 0;
    t->pen_down = 1;
    t->pen_color = 0xFFFFFF;
    t->pen_width = 1;
    t->fill_color = 0xFFFFFF;
    t->fill_enabled = 0;
    t->fill_pattern = 0;
    FreeMemorySafe((void **)&t->cursor_buffer);
    t->visible = showturtle;
    t->cursor_size = 10;
    t->cursor_color = 0x00FF00;
    t->cursor_x = 0;
    t->cursor_y = 0;
    t->cursor_drawn = 0;
    t->has_buffer_support = ((void *)ReadBuffer != (void *)DisplayNotSet);
    t->stack_ptr = 0;
    t->poly_count = 0;
    t->poly_recording = 0;
    if (t->visible)
        draw_turtle_cursor(t);
}
void cmd_turtle(void)
{
    unsigned char *tp;
    if ((tp = checkstring(cmdline, (unsigned char *)"FORWARD")) || (tp = checkstring(cmdline, (unsigned char *)"FD")))
    {
        turtle_forward(&turtle, getnumber(tp));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"BACK")) || (tp = checkstring(cmdline, (unsigned char *)"BACKWARD")) || (tp = checkstring(cmdline, (unsigned char *)"BK")))
    {
        turtle_forward(&turtle, -getnumber(tp));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"RIGHT")) || (tp = checkstring(cmdline, (unsigned char *)"TURN RIGHT")) || (tp = checkstring(cmdline, (unsigned char *)"RT")))
    {
        getcsargs(&tp, 1);
        if (argc == 1)
            turtle.heading += getnumber(tp);
        else
            turtle.heading += 90;
        turtle.heading = normalize_heading(turtle.heading);
        draw_turtle_cursor(&turtle);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"LEFT")) || (tp = checkstring(cmdline, (unsigned char *)"TURN LEFT")) || (tp = checkstring(cmdline, (unsigned char *)"LT")))
    {
        getcsargs(&tp, 1);
        if (argc == 1)
            turtle.heading -= getnumber(tp);
        else
            turtle.heading -= 90;
        turtle.heading = normalize_heading(turtle.heading);
        draw_turtle_cursor(&turtle);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"PEN UP")) || (tp = checkstring(cmdline, (unsigned char *)"PENUP")) || (tp = checkstring(cmdline, (unsigned char *)"PU")))
    {
        turtle.pen_down = 0;
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"PEN DOWN")) || (tp = checkstring(cmdline, (unsigned char *)"PENDOWNU")) || (tp = checkstring(cmdline, (unsigned char *)"PD")))
    {
        turtle.pen_down = 1;
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"PEN COLOUR")) || (tp = checkstring(cmdline, (unsigned char *)"PENCOLOR")) || (tp = checkstring(cmdline, (unsigned char *)"PC")))
    {
        turtle.pen_color = getint(tp, 0, 0xFFFFFF);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"PEN WIDTH")) || (tp = checkstring(cmdline, (unsigned char *)"PENWIDTH")) || (tp = checkstring(cmdline, (unsigned char *)"PW")))
    {
        turtle.pen_width = getint(tp, 1, 50);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"SET XY")) || (tp = checkstring(cmdline, (unsigned char *)"SETXY")) || (tp = checkstring(cmdline, (unsigned char *)"MOVE")))
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            SyntaxError();
        turtle_goto(&turtle, getnumber(argv[0]), getnumber(argv[2]));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"SET X")) || (tp = checkstring(cmdline, (unsigned char *)"SETX")))
    {
        turtle_goto(&turtle, getnumber(tp), turtle.y);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"SET Y")) || (tp = checkstring(cmdline, (unsigned char *)"SETY")))
    {
        turtle_goto(&turtle, turtle.x, getnumber(tp));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"SET HEADING")) || (tp = checkstring(cmdline, (unsigned char *)"HEADING")) || (tp = checkstring(cmdline, (unsigned char *)"SETHEADING")) || (tp = checkstring(cmdline, (unsigned char *)"SETH")))
    {
        turtle.heading = normalize_heading(getnumber(tp));
        draw_turtle_cursor(&turtle);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"HOME")))
    {
        turtle_goto(&turtle, HRes / 2, VRes / 2);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"RESET")))
    {
        getcsargs(&tp, 1);
        if (argc == 1)
            turtle_reset(&turtle, getint(tp, 0, 1));
        else
            turtle_reset(&turtle, 0);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"ARC")))
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            SyntaxError();
        turtle_arc(&turtle, getnumber(argv[0]), getnumber(argv[2]));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"ARCL")) || (tp = checkstring(cmdline, (unsigned char *)"ARCLEFT")) || (tp = checkstring(cmdline, (unsigned char *)"ARC LEFT")))
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            SyntaxError();
        turtle_arc(&turtle, getnumber(argv[0]), getnumber(argv[2]));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"ARCR")) || (tp = checkstring(cmdline, (unsigned char *)"ARCRIGHT")) || (tp = checkstring(cmdline, (unsigned char *)"ARC RIGHT")))
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            SyntaxError();
        turtle_arc(&turtle, getnumber(argv[0]), -getnumber(argv[2]));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"BEZIER")))
    {
        getcsargs(&tp, 11);
        if (argc != 11)
            SyntaxError();
        turtle_bezier(&turtle, getnumber(argv[0]), getnumber(argv[2]), getnumber(argv[4]), getnumber(argv[6]), getnumber(argv[8]), getnumber(argv[10]));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"CIRCLE")))
    {
        DrawCircle((int)turtle.x, (int)turtle.y, getint(tp, 2, HRes / 2), 1, turtle.pen_color, -1, 1.0);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"DOT")))
    {
        getcsargs(&tp, 1);
        int size = 5;
        if (argc)
            size = getint(argv[0], 1, HRes / 2);
        DrawCircle((int)turtle.x, (int)turtle.y, size, 0, turtle.pen_color, turtle.pen_color, 1.0);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"FCIRCLE")))
    {
        if (turtle.fill_enabled)
        {
            DrawCircleFilled_Pattern((int)turtle.x, (int)turtle.y, getint(tp, 2, HRes / 2),
                                     turtle.fill_color, turtle.fill_pattern);
        }
        if (turtle.pen_down)
        {
            DrawCircle((int)turtle.x, (int)turtle.y, getint(tp, 2, HRes / 2), 1, turtle.pen_color, -1, 1.0);
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"FRECTANGLE")) || (tp = checkstring(cmdline, (unsigned char *)"FRECT")))
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            SyntaxError();
        int w = getint(argv[0], 1, HRes);
        int h = getint(argv[2], 1, VRes);
        int x = (int)turtle.x - w / 2;
        int y = (int)turtle.y - h / 2;
        int x1 = x + w - 1;
        int y1 = y + h - 1;

        if (turtle.fill_enabled)
        {
            DrawRectangleFilled_Pattern(x, y, w, h,
                                        turtle.fill_color, turtle.fill_pattern);
        }
        if (turtle.pen_down)
        {
            DrawLine(x, y, x1, y, 1, turtle.pen_color);
            DrawLine(x, y1, x1, y1, 1, turtle.pen_color);
            DrawLine(x, y, x, y1, 1, turtle.pen_color);
            DrawLine(x1, y, x1, y1, 1, turtle.pen_color);
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"ARECTANGLE")) || (tp = checkstring(cmdline, (unsigned char *)"ARECT")))
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            SyntaxError();
        turtle_rectangle(&turtle, getint(argv[0], 1, HRes), getint(argv[2], 1, VRes));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"WEDGE")))
    {
        getcsargs(&tp, 5);
        if (argc != 5)
            SyntaxError();
        turtle_wedge(&turtle, getnumber(argv[0]), getnumber(argv[2]), getnumber(argv[4]));
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"FILL COLOUR")) || (tp = checkstring(cmdline, (unsigned char *)"FILLCOLOR")) || (tp = checkstring(cmdline, (unsigned char *)"FC")))
    {
        turtle.fill_color = getint(tp, 0, 0xFFFFFF);
        turtle.fill_enabled = 1;
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"FILL PATTERN")) || (tp = checkstring(cmdline, (unsigned char *)"FILLPATTERN")) || (tp = checkstring(cmdline, (unsigned char *)"FP")))
    {
        turtle.fill_pattern = getint(tp, 0, NUM_PATTERNS);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"NOFILL")) || (tp = checkstring(cmdline, (unsigned char *)"NO FILL")))
    {
        turtle.fill_enabled = 0;
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"BEGIN FILL")) || (tp = checkstring(cmdline, (unsigned char *)"BEGIN_FILL")) || (tp = checkstring(cmdline, (unsigned char *)"BF")))
    {
        turtle.poly_count = 0;
        turtle.poly_recording = 1;
        turtle.poly_points_x[turtle.poly_count] = (int)turtle.x;
        turtle.poly_points_y[turtle.poly_count] = (int)turtle.y;
        turtle.poly_count++;
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"END FILL")) || (tp = checkstring(cmdline, (unsigned char *)"END_FILL")) || (tp = checkstring(cmdline, (unsigned char *)"EF")))
    {
        turtle.poly_recording = 0;
        if (turtle.poly_count > 2)
        {
            draw_filled_polygon(&turtle);
        }
        turtle.poly_count = 0;
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"PUSH")))
    {
        if (turtle.stack_ptr < 16)
        {
            turtle.stack_x[turtle.stack_ptr] = turtle.x;
            turtle.stack_y[turtle.stack_ptr] = turtle.y;
            turtle.stack_heading[turtle.stack_ptr] = turtle.heading;
            turtle.stack_ptr++;
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"POP")))
    {
        if (turtle.stack_ptr > 0)
        {
            turtle.stack_ptr--;
            turtle_goto(&turtle, turtle.stack_x[turtle.stack_ptr],
                        turtle.stack_y[turtle.stack_ptr]);
            turtle.heading = turtle.stack_heading[turtle.stack_ptr];
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"SHOW TURTLE")) || (tp = checkstring(cmdline, (unsigned char *)"SHOWTURTLE")) || (tp = checkstring(cmdline, (unsigned char *)"ST")))
    {
        turtle.visible = 1;
        draw_turtle_cursor(&turtle);
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"HIDE TURTLE")) || (tp = checkstring(cmdline, (unsigned char *)"HIDETURTLE")) || (tp = checkstring(cmdline, (unsigned char *)"HT")))
    {
        turtle.visible = 0;
        if (turtle.cursor_drawn)
        {
            erase_turtle_cursor(&turtle);
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"CURSOR SIZE")) || (tp = checkstring(cmdline, (unsigned char *)"CURSORSIZE")) || (tp = checkstring(cmdline, (unsigned char *)"CS")))
    {
        turtle.cursor_size = getint(tp, 5, MAX_CURSOR_SIZE);
        if (turtle.visible)
        {
            draw_turtle_cursor(&turtle);
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"CURSOR COLOUR")) || (tp = checkstring(cmdline, (unsigned char *)"CURSORCOLOR")) || (tp = checkstring(cmdline, (unsigned char *)"CC")))
    {
        turtle.cursor_color = getint(tp, 0, 0xFFFFFF);
        if (turtle.visible)
        {
            draw_turtle_cursor(&turtle);
        }
    }
    else if ((tp = checkstring(cmdline, (unsigned char *)"STAMP")))
    {
        int or = 0; // default orientation is up
        int xp = turtle.x;
        int yp = turtle.y;
        if (turtle.heading > 45 && turtle.heading <= 135)
            or = 90;
        if (turtle.heading > 135 && turtle.heading <= 225)
            or = 180;
        if (turtle.heading > 225 && turtle.heading <= 315)
            or = 270;

        int tpos = 0;
        int draw_width, draw_height;
        int x_offset, y_offset;

        // Determine drawn dimensions and calculate centering offsets
        if (or == 0 || or == 180)
        {
            draw_width = turtlewidth;
            draw_height = turtleheight;
        }
        else // or == 90 || or == 270
        {
            draw_width = turtleheight;
            draw_height = turtlewidth;
        }

        x_offset = xp - draw_width / 2;
        y_offset = yp - draw_height / 2;

        // Set starting position in turtle array
        if (or == 180)
            tpos = turtlewidth * turtleheight - 1;
        else if (or == 270)
            tpos = turtlewidth - 1;
        else if (or == 90)
            tpos = turtlewidth * (turtleheight - 1);

        if (or == 0 || or == 180)
        {
            for (int y = y_offset; y < y_offset + draw_height; y++)
            {
                for (int x = x_offset; x < x_offset + draw_width; x++)
                {
                    if (x >= 0 && x < HRes && y >= 0 && y < VRes && greenturtle[tpos] != 0)
                        DrawPixel(x, y, greenturtle[tpos]);
                    if (or == 0)
                        tpos++;
                    else
                        tpos--;
                }
            }
        }
        else // or == 90 || or == 270
        {
            for (int y = y_offset; y < y_offset + draw_height; y++)
            {
                if (or == 90)
                    tpos = turtlewidth * (turtleheight - 1) + (y - y_offset);
                else // or == 270
                    tpos = (turtlewidth - 1) - (y - y_offset);

                for (int x = x_offset; x < x_offset + draw_width; x++)
                {
                    if (x >= 0 && x < HRes && y >= 0 && y < VRes && greenturtle[tpos] != 0)
                        DrawPixel(x, y, greenturtle[tpos]);

                    if (or == 90)
                        tpos -= turtlewidth; // Move up one row in original
                    else                     // or == 270
                        tpos += turtlewidth; // Move down one row in original
                }
            }
        }
    }
    else
        SyntaxError();
}