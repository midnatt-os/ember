__attribute__((naked, section(".text.startup")))
void _start(void) {
    asm volatile("movabsq $0x00000000deadbeef, %r15");
    asm volatile("jmp _start");
}
