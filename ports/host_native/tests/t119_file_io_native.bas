OPEN "B:/vm_missing_syscall.tmp" FOR OUTPUT AS #1
PRINT #1, "hello"
CLOSE #1
OPEN "B:/vm_missing_syscall.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
IF a$ <> "hello" THEN ERROR "file io failed"
PRINT a$
