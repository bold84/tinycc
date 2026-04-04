    .text
    .p2align 2
    .global arm64_call_with_dregs

arm64_call_with_dregs:
    stp x29, x30, [sp, -32]!
    stp x19, x20, [sp, 16]
    mov x29, sp
    mov x19, x1

    ldr d8, [x0, 0]
    ldr d9, [x0, 8]
    ldr d10, [x0, 16]
    ldr d11, [x0, 24]
    ldr d12, [x0, 32]
    ldr d13, [x0, 40]
    ldr d14, [x0, 48]
    ldr d15, [x0, 56]

    mov x0, x3
    blr x2

    str d8, [x19, 0]
    str d9, [x19, 8]
    str d10, [x19, 16]
    str d11, [x19, 24]
    str d12, [x19, 32]
    str d13, [x19, 40]
    str d14, [x19, 48]
    str d15, [x19, 56]

    ldp x19, x20, [sp, 16]
    ldp x29, x30, [sp], 32
    ret
