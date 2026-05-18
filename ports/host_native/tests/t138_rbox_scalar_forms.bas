CLS RGB(BLACK)

' Default radius/colour/fill
RBOX 5, 5, 12, 10

' Explicit radius only
RBOX 25, 5, 12, 10, 4

' Explicit colour
RBOX 45, 5, 12, 10, 5, RGB(RED)

' Fill only
RBOX 65, 5, 12, 10, , , RGB(BLUE)

' Radius + colour + fill
RBOX 85, 5, 12, 10, 3, RGB(GREEN), RGB(YELLOW)

' Negative width/height follow BOX semantics
RBOX 105, 15, -12, -10, 4, RGB(CYAN), RGB(MAGENTA)
