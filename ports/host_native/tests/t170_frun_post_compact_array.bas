' Regression: post-compact heap contiguity for FRUN big-array alloc.
'
' FRUN compiles the source (allocates compile-only + runtime tables),
' bc_compiler_compact frees compile-only + shrinks runtime, then user
' code allocates a contiguous block (DIM big array). If compile-only
' tables sit at low addresses in the alive cluster, freeing them
' leaves holes that shrunk-runtime allocations land in scattered;
' largest contig after compact ends up about half the heap. With
' compile-only tables at high addresses (just below cs/vm), the post-
' compact shrunk cluster is contiguous at the top and the rest of the
' heap is one big free region.
'
' Sized so that today's fragmented largest-contig fails to satisfy it
' on a 128 KB BC_SIM_RP2040 host heap; post-fix the same heap has
' enough contiguous space.
DIM x(8000)
x(0) = 1
x(8000) = 99
PRINT "ok"
PRINT x(0); x(8000)
