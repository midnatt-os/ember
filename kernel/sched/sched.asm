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

    mov rax, rdi
    mov [rdi + Thread.rsp], rsp
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

    ret

global user_thread_trampoline
user_thread_trampoline:
    add rsp, 16         ; Skip kstack.exit and user_init_stack.user_trampoline

    pop rcx             ; user_rip -> RCX (required by sysret)
    pop rdi             ; user_rsp -> RDI (temp)

    mov rsp, rdi        ; Switch to User Stack
    mov r11, 0x202      ; RFLAGS: Interrupts enabled (0x200) | Reserved (0x2)

    xor rax, rax
    xor rbx, rbx
    xor rdx, rdx
    xor rsi, rsi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    swapgs
    o64 sysret
