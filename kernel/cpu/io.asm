global port_write8
global port_write16
global port_write32
global port_read8
global port_read16
global port_read32

port_write8:
	mov dx, di
	mov al, sil
	out dx, al
	ret

port_write16:
	mov dx, di
	mov ax, si
	out dx, ax
	ret

port_write32:
	mov dx, di
	mov eax, esi
	out dx, eax
	ret

port_read8:
	mov dx, di
	in al, dx
	ret

port_read16:
	mov dx, di
	in ax, dx
	ret
port_read32:

	mov dx, di
	in eax, dx
	ret
