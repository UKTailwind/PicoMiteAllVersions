#ifndef TURTLE_H
#define TURTLE_H
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
/*
Movement Commands
FORWARD distance        (FD)  - Move forward by distance pixels
BACK distance          (BK)  - Move backward by distance pixels
LEFT angle             (LT)  - Turn left by angle degrees
RIGHT angle            (RT)  - Turn right by angle degrees

Position Commands
SET XY x, y                    - Move to absolute position (x,y)
SET X x                       - Set X coordinate, keep Y
SET Y y                       - Set Y coordinate, keep X
SET HEADING angle       (SETH) - Set absolute heading (0=right, 90=up)
HOME                         - Return to center (160,120) heading 0

Pen Control Commands
PEN UP                  (PU)  - Lift pen (stop drawing)
PEN DOWN                (PD)  - Lower pen (start drawing)
PEN COLOUR color         (PC)  - Set pen color
PEN WIDTH width         (PW)  - Set pen line width

Arc and Curve Commands
ARC radius angle             - Draw arc with given radius and angle
ARCLEFT radius,angle  (ARCL) - Draw arc turning left
ARCRIGHT radius,angle (ARCR) - Draw arc turning right
BEZIER cp1 , cp1angle, cp2, cp2angle, end, endangle          - Draw Bezier curve with control points

Basic Shape Commands
CIRCLE radius               - Draw circle at current position
DOT size                   - Draw filled dot (default size=5)
FCIRCLE radius             - Draw filled circle
FRECTANGLE width,height (FRECT) - Draw filled rectangle
WEDGE radius start end     - Draw filled wedge/pie slice

Fill Commands
FILL COLOUR color        (FC)  - Set fill color and enable filling
FILL PATTERN pattern    (FP)  - Set fill pattern (0-7)
NO FILL                      - Disable filling
FILL                        - Flood fill at current position
BEGIN FILL             (BF)  - Start recording polygon for fill
END FILL               (EF)  - End recording and fill polygon

Cursor Commands
SHOW TURTLE            (ST)  - Show turtle cursor
HIDE TURTLE            (HT)  - Hide turtle cursor
CURSOR SIZE size       (CS)  - Set cursor size
CURSOR COLOUR color     (CC)  - Set cursor color
STAMP                        - Draw a turtle at the current x,y position

State Management Commands
RESET show                  - clear screen and reset everything, show the turtle if show = 1
PUSH                        - Save current position and heading to stack
POP                         - Restore position and heading from stack

Fill Patterns
0: Solid fill
1: Checkerboard
2: Vertical lines
3: Horizontal lines
4: Diagonal cross
5: Diagonal stripes
6: Crosshatch
7: Fine diagonal
8: Dense checkerboard
9: Diagonal right medium
10: Diagonal left medium
11: Vertical lines medium
12: Horizontal lines medium
13: Large checkerboard
14: Dotted vertical
15: Horizontal stripes tight
16: Grid
17: Weave pattern
18: Diamond
19: Gradient diagonal
20: Gradient diagonal reverse
21: Border/frame
22: Vertical split
23: Woven
24: Sparse dots
25: Diagonal very fine
26: Arrow up
27: Dense dots
28: Chevron
29: Diamond hollow
30: Circle
31: Circle filled
*/
// Calculate worst-case buffer size for cursor
// Worst case is diagonal orientation, so we need sqrt(2) * cursor_size in each direction
// Maximum cursor size is configurable, let's use 20 pixels as a reasonable max
#define MAX_CURSOR_SIZE 50
#define turtlewidth 14
#define turtleheight 19
#define NUM_PATTERNS 32
// Polygon settings - reasonable limit
#define MAX_POLYGON_POINTS 128
typedef struct
{
    // Position and orientation
    float x, y;
    float heading;

    // Pen state
    int pen_down;
    int pen_color;
    int pen_width;

    // Fill state
    int fill_color;
    int fill_enabled;
    int fill_pattern;

    // Cursor state
    int visible;
    int cursor_size;
    int cursor_color;
    int cursor_x;
    int cursor_y;
    float cursor_heading;
    int cursor_drawn;

    // Cursor buffer save/restore
    unsigned char *cursor_buffer;
    int cursor_buffer_x1, cursor_buffer_y1;
    int cursor_buffer_x2, cursor_buffer_y2;
    int has_buffer_support; // Flag: are ReadBuffer/DrawBuffer available?

    // Stack for push/pop
    float stack_x[16];
    float stack_y[16];
    float stack_heading[16];
    int stack_ptr;

    // Polygon fill support
    int poly_points_x[MAX_POLYGON_POINTS];
    int poly_points_y[MAX_POLYGON_POINTS];
    int poly_count;
    int poly_recording;

} TurtleState;
// Core movement functions
void turtle_forward(TurtleState *t, float distance);
void turtle_goto(TurtleState *t, float new_x, float new_y);
void turtle_arc(TurtleState *t, float radius, float angle);
void turtle_bezier(TurtleState *t, float cp1_dist, float cp1_angle, float cp2_dist, float cp2_angle, float end_dist, float end_angle);
void turtle_wedge(TurtleState *t, float radius, float start_angle, float end_angle);
void turtle_reset(TurtleState *t, bool showturtle);
// Cursor functions
void draw_turtle_cursor(TurtleState *t);
void erase_turtle_cursor(TurtleState *t);

// Fill functions
void draw_filled_polygon(TurtleState *t);
void fill_polygon_scanline(int *x, int *y, int count, int color);
void fill_polygon_pattern(int *x, int *y, int count, int color, int pattern);
void turtle_rectangle(TurtleState *t, float width, float height);
// Pattern-capable drawing functions (internal)
void DrawCircleFilled_Pattern(int cx, int cy, int radius, int color, int pattern);
void DrawRectangleFilled_Pattern(int x, int y, int width, int height, int color, int pattern);
#endif // TURTLE_H