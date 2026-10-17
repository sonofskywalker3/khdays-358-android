// The switch between execution contexts on 64-bit ARM (context_posix.c): the registers AAPCS64
// keeps across a call -- x19-x28, the frame pointer x29, the link register x30, d8-d15 and FPCR --
// on the context's own stack, its stack pointer kept in the context.

    .text

// void khdays_context_jump(void **save_sp, void *load_sp)
    .global khdays_context_jump
    .type khdays_context_jump, %function
    .p2align 2
khdays_context_jump:
    sub sp, sp, #176
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8, d9, [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    mrs x9, fpcr
    str x9, [sp, #160]
    mov x9, sp
    str x9, [x0]
    mov sp, x1
    ldr x9, [sp, #160]
    msr fpcr, x9
    ldp d14, d15, [sp, #144]
    ldp d12, d13, [sp, #128]
    ldp d10, d11, [sp, #112]
    ldp d8, d9, [sp, #96]
    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp, #0]
    add sp, sp, #176
    ret
    .size khdays_context_jump, . - khdays_context_jump

// A new context's first return lands here, its KhdaysContext in x19 (context_posix.c's frame).
    .global khdays_context_trampoline
    .type khdays_context_trampoline, %function
    .p2align 2
khdays_context_trampoline:
    mov x0, x19
    bl khdays_context_started
    brk #0                        // khdays_context_started does not return
    .size khdays_context_trampoline, . - khdays_context_trampoline

    .section .note.GNU-stack, "", %progbits
