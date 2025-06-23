%define MSR_FS_BASE 0xC0000100
%define MSR_KERNEL_GS_BASE 0xC0000102

struc Thread
    .rsp resq 1
    ; ...
endstruc

global sched_context_switch
sched_context_switch:
    push rbx
    push rbp
    push r15
    push r14
    push r13
    push r12

    ; Save rsp into old thread
    mov [rdi + Thread.rsp], rsp
    ; load rsp from new thread
    mov rsp, [rsi + Thread.rsp]

    xor r12, r12
    mov ds, r12
    mov es, r12

    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    pop rbx

    mov rax, rdi
    ret



global thread_init_user
thread_init_user:
    swapgs
    iretq
