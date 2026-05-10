/*
 * ports/pc386/setjmp.S — i686 setjmp/longjmp.
 *
 * AT&T syntax. Saves/restores callee-saved registers (ebx, esi, edi,
 * ebp), the stack pointer, and the return address. Uses 6 of the 8
 * jmp_buf slots; remainder reserved.
 *
 * jmp_buf layout (longs, in order):
 *   [0] ebx
 *   [1] esi
 *   [2] edi
 *   [3] ebp
 *   [4] esp (post-pop, i.e. caller's stack)
 *   [5] eip (return address)
 *   [6..7] reserved
 *
 * cdecl: argument 1 (jmp_buf*) at 4(%esp); argument 2 (int val for
 * longjmp) at 8(%esp); return-addr saved by call is at 0(%esp).
 */

    .text

    .globl  setjmp
    .type   setjmp, @function
setjmp:
    movl    4(%esp), %eax        /* eax = &env */
    movl    %ebx,  0(%eax)
    movl    %esi,  4(%eax)
    movl    %edi,  8(%eax)
    movl    %ebp, 12(%eax)
    leal    4(%esp), %ecx        /* ecx = caller's esp (post-pop) */
    movl    %ecx, 16(%eax)
    movl    (%esp), %ecx         /* ecx = return address */
    movl    %ecx, 20(%eax)
    xorl    %eax, %eax           /* return 0 */
    ret
    .size   setjmp, . - setjmp

    .globl  longjmp
    .type   longjmp, @function
longjmp:
    movl    4(%esp), %edx        /* edx = &env */
    movl    8(%esp), %eax        /* eax = val */
    testl   %eax, %eax           /* per spec: longjmp(env, 0) returns 1 */
    jnz     1f
    movl    $1, %eax
1:
    movl     0(%edx), %ebx
    movl     4(%edx), %esi
    movl     8(%edx), %edi
    movl    12(%edx), %ebp
    movl    16(%edx), %esp
    movl    20(%edx), %ecx       /* return address from setjmp call */
    jmp     *%ecx
    .size   longjmp, . - longjmp

    .section .note.GNU-stack,"",@progbits
