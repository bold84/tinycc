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
    .long   0x910003f0
    .long   0xd37cedf1
    .long   0xb4000131
    .long   0xf1400620
    .long   0x540000a9
    .long   0xd1400610
    .long   0xf940021f
    .long   0xd1400631
    .long   0x17fffffb
    .long   0xcb110210
    .long   0xf940021f
    .long   0xd65f03c0

.globl _(tinyc_getbp)
_(tinyc_getbp):
    mov     x0, x29
    ret

.globl _(mingw_getsp)
_(mingw_getsp):
    .long   0x910003e0
    .long   0xd65f03c0

.globl _(__mingw_setjmp)
_(__mingw_setjmp):
    .long   0xf900001f
    .long   0xa9015013
    .long   0xa9025815
    .long   0xa9036017
    .long   0xa9046819
    .long   0xa905701b
    .long   0xa906781d
    .long   0x910003e2
    .long   0xf9003802
    .long   0xd53b4402
    .long   0xb9007802
    .long   0xd53b4422
    .long   0xb9007c02
    .long   0x6d082408
    .long   0x6d092c0a
    .long   0x6d0a340c
    .long   0x6d0b3c0e
    .long   0xd2800000
    .long   0xd65f03c0

.globl _(__mingw_longjmp)
_(__mingw_longjmp):
    .long   0xa9415013
    .long   0xa9425815
    .long   0xa9436017
    .long   0xa9446819
    .long   0xa945701b
    .long   0xa946781d
    .long   0xf9403802
    .long   0x9100005f
    .long   0xb9407802
    .long   0xd51b4402
    .long   0xb9407c02
    .long   0xd51b4422
    .long   0x6d482408
    .long   0x6d492c0a
    .long   0x6d4a340c
    .long   0x6d4b3c0e
    .long   0xaa0103e0
    .long   0xb5000040
    .long   0xd2800020
    .long   0xd65f03c0

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
