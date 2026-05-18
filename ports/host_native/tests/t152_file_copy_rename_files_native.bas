OPEN "B:/copy_a.txt" FOR OUTPUT AS #1
PRINT #1, "copied"
CLOSE #1
COPY "B:/copy_a.txt" TO "B:/copy_b.txt"
RENAME "B:/copy_b.txt" AS "B:/copy_c.txt"
FILES "B:/copy_*"
OPEN "B:/copy_c.txt" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
KILL "B:/copy_a.txt"
KILL "B:/copy_c.txt"
PRINT a$
