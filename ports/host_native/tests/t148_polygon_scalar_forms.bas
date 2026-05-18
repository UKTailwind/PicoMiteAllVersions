OPTION DEFAULT INTEGER
CLS 0
DIM x%(4), y%(4)
x%(0) = 20: y%(0) = 10
x%(1) = 42: y%(1) = 6
x%(2) = 60: y%(2) = 22
x%(3) = 48: y%(3) = 42
x%(4) = 16: y%(4) = 34
POLYGON 5, x%(), y%(), &HFFFFFF, &H0000FF
END
