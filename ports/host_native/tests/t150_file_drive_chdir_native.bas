DRIVE "B:"
MKDIR "vm_dir"
CHDIR "vm_dir"
OPEN "hello.txt" FOR OUTPUT AS #1
PRINT #1, "abc"
CLOSE #1
OPEN "hello.txt" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
CHDIR ".."
KILL "vm_dir/hello.txt"
RMDIR "vm_dir"
PRINT a$
