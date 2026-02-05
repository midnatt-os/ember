%define CPU_SCHED_OFF 8
%define SCHED_CURRENT_OFF 0

struc thread
    .rsp resq 1
    .syscall_rsp resq 1
    .k_stack_base resq 1
    .k_stack_size resq 1
endstruc

extern syscall_table
extern syscall_table_len

global syscall_entry
syscall_entry:
    swapgs

    ; 1. Get current thread via GS macros
    mov r15, qword [gs:CPU_SCHED_OFF]

    ; 2. Switch to kernel stack
    mov qword [r15 + thread.syscall_rsp], rsp
    mov rsp, qword [r15 + thread.k_stack_base]
    add rsp, qword [r15 + thread.k_stack_size]

    ; 3. Save return state (Destroyed by syscall/sysret)
    push rcx ; User RIP
    push r11 ; User RFLAGS

    ; 4. Build syscall_args_t on stack
    push r9  ; args->arg5
    push r8  ; args->arg4
    push r10 ; args->arg3 (r10 used instead of rcx)
    push rdx ; args->arg2
    push rsi ; args->arg1
    push rdi ; args->arg0

    ; 5. Bounds check
    cmp rax, qword [syscall_table_len]
    jae .invalid_syscall

    ; 6. Dispatch
    mov rdi, rsp ; First argument: pointer to the struct we just built
    mov rax, [syscall_table + rax * 8]

    sti
    call rax     ; C returns value in RAX, error in RDX
    cli

    ; 7. Move error code to RBX per requirement
    mov rbx, rdx
    jmp .cleanup

.invalid_syscall:
    mov rax, 0
    mov rbx, -1  ; ENOSYS / Invalid

.cleanup:
    ; 8. Cleanup stack and restore state
    add rsp, 48 ; Remove 6 args
    pop r11     ; Restore RFLAGS
    pop rcx     ; Restore RIP

    ; 9. Reload r15 to ensure we have the correct thread pointer after potential block
    mov r15, qword [gs:CPU_SCHED_OFF]

    mov rsp, qword [r15 + thread.syscall_rsp]

    xor r15, r15
    swapgs
    o64 sysret
