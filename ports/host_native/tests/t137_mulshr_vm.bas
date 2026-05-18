' RUN_ARGS: --vm
OPTION EXPLICIT

IF MULSHR(3, 5, 0) <> 15 THEN ERROR "mulshr bits0"
IF MULSHR(7, 9, 1) <> 31 THEN ERROR "mulshr positive"
IF MULSHR(-7, 9, 1) <> (-63 \ 2) THEN ERROR "mulshr neg lhs"
IF MULSHR(7, -9, 1) <> (-63 \ 2) THEN ERROR "mulshr neg rhs"
IF MULSHR(-7, -9, 1) <> (63 \ 2) THEN ERROR "mulshr both neg"
IF MULSHR(1073741824, 1073741824, 30) <> 1073741824 THEN ERROR "mulshr q30 1.0"
IF MULSHR(-1073741824, 536870912, 29) <> (-1073741824) THEN ERROR "mulshr q29 neg"

PRINT "ok"
