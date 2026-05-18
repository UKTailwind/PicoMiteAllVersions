DIM x%(2), y%(2), w%(2), h%(2), r%(2), c%(2), f%(2)

x%(0)=10: y%(0)=10: w%(0)=12: h%(0)=10: r%(0)=3: c%(0)=RGB(RED):   f%(0)=RGB(BLUE)
x%(1)=30: y%(1)=10: w%(1)=12: h%(1)=10: r%(1)=4: c%(1)=RGB(GREEN): f%(1)=RGB(YELLOW)
x%(2)=50: y%(2)=10: w%(2)=12: h%(2)=10: r%(2)=5: c%(2)=RGB(CYAN):  f%(2)=RGB(MAGENTA)

CLS RGB(BLACK)
RBOX x%(), y%(), w%(), h%(), r%(), c%(), f%()
