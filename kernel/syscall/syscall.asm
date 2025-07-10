%define CPU_SCHED_OFF 8
%define SCHED_CURRENT_OFF 0

struc Thread
    .rsp resq 1
    .syscall_rsp resq 1
    .k_stack_base resq 1
    ; ...
endstruc

extern syscall_exit
extern syscall_debug
extern syscall_set_tcb
extern syscall_anon_alloc
extern syscall_anon_free
extern syscall_open
extern syscall_close
extern syscall_read
extern syscall_write
extern syscall_seek
extern syscall_fetch_framebuffer
extern syscall_fork
extern syscall_execve
extern syscall_mmap
extern syscall_mprotect
extern syscall_mkdir
extern syscall_dup2
extern syscall_gettime
extern syscall_nsleep
extern syscall_getpid
extern syscall_getcwd
extern syscall_isatty
extern syscall_getppid
extern syscall_ioctl
extern syscall_fcntl
extern syscall_stat
extern syscall_dup

section .rodata
syscall_table:
    dq syscall_exit ; 0
    dq syscall_debug ; 1
    dq syscall_set_tcb ; 2
    dq syscall_anon_alloc ; 3
    dq syscall_anon_free ; 4
    dq syscall_open ; 5
    dq syscall_close ; 6
    dq syscall_read ; 7
    dq syscall_write ; 8
    dq syscall_seek ; 9
    dq syscall_fetch_framebuffer ; 10
    dq syscall_fork ; 11
    dq syscall_execve ; 12
    dq syscall_mmap ; 13
    dq syscall_mprotect ; 14
    dq syscall_mkdir ; 15
    dq syscall_dup2 ; 16
    dq syscall_gettime ; 17
    dq syscall_nsleep ; 18
    dq syscall_getpid ; 19
    dq syscall_getcwd ; 20
    dq syscall_isatty ; 21
    dq syscall_getppid ; 22
    dq syscall_ioctl ; 23
    dq syscall_fcntl ; 24
    dq syscall_stat ; 25
    dq syscall_dup ; 26


.length: dq ($ - syscall_table) / 8

section .text
global syscall_entry
syscall_entry:
    swapgs

    mov r15, qword [gs:CPU_SCHED_OFF]
    mov r15, qword [r15 + SCHED_CURRENT_OFF]

    mov qword [r15 + Thread.syscall_rsp], rsp
    mov rsp, qword [r15 + Thread.k_stack_base]

    push rcx ; Now safe to mov arg 4 (r10) into rcx per sysv abi.
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cmp rax, qword [syscall_table.length]
    jge .invalid_syscall

    mov rax, [syscall_table + rax * 8]
    cmp rax, 0
    je .invalid_syscall

    mov rcx, r10 ; Move the fourth arg (r10) into rcx where it's "supposed" to be.

    sti
    call rax
    cli

    mov rbx, rdx ; Cannot use rdx for return value

    .invalid_syscall:

    xor r12, r12
    mov r12, ds
    mov r12, es

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx

    mov rsp, qword [r15 + Thread.syscall_rsp]
    xor r15, r15

    swapgs
    o64 sysret

global fork_ret
fork_ret:
    cli

    mov rbx, rdx

    xor r12, r12
    mov r12, ds
    mov r12, es

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx

    pop r15
    mov rsp, r15
    xor r15, r15

    xor rax, rax

    swapgs
    o64 sysret
