/* ---------------------------------------------- */
/* chkstk86.s */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

/* ---------------------------------------------- */
#if defined(__aarch64__)
/* ---------------------------------------------- */

.globl __chkstk
__chkstk:
    lsl     x15, x15, 4
    mov     x16, sp
.L_loop:
    subs    x17, x15, 4096
    b_cc    .L_done
    sub     x16, x16, 4096
    ldr     xzr, [x16]
    mov     x15, x17
    b       .L_loop
.L_done:
    sub     sp, x16, x15
    ret

/* ---------------------------------------------- */
#elif !defined(__x86_64__)
/* ---------------------------------------------- */

.globl _(__chkstk)
_(__chkstk):
    xchg    (%esp),%ebp     /* store ebp, get ret.addr */
    push    %ebp            /* push ret.addr */
    lea     4(%esp),%ebp    /* setup frame ptr */
    push    %ecx            /* save ecx */
    mov     %ebp,%ecx
P0:
    sub     $4096,%ecx
    test    %eax,(%ecx)
    sub     $4096,%eax
    cmp     $4096,%eax
    jge     P0
    sub     %eax,%ecx
    test    %eax,(%ecx)

    mov     %esp,%eax
    mov     %ecx,%esp
    mov     (%eax),%ecx     /* restore ecx */
    jmp     *4(%eax)

/* ---------------------------------------------- */
#else
/* ---------------------------------------------- */

.globl _(__chkstk)
_(__chkstk):
    xchg    (%rsp),%rbp     /* store ebp, get ret.addr */
    push    %rbp            /* push ret.addr */
    lea     8(%rsp),%rbp    /* setup frame ptr */
    push    %rcx            /* save ecx */
    mov     %rbp,%rcx
    movslq  %eax,%rax
P0:
    sub     $4096,%rcx
    test    %rax,(%rcx)
    sub     $4096,%rax
    cmp     $4096,%rax
    jge     P0
    sub     %rax,%rcx
    test    %rax,(%rcx)

    mov     %rsp,%rax
    mov     %rcx,%rsp
    mov     (%rax),%rcx     /* restore ecx */
    jmp     *8(%rax)

/* ---------------------------------------------- */
/* setjmp/longjmp support */

.globl _(tinyc_getbp)
_(tinyc_getbp):
    mov %rbp,%rax
    ret

/* ---------------------------------------------- */
#endif
/* ---------------------------------------------- */
