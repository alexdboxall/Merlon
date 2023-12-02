

global x86LoadGdt
x86LoadGdt:
	; The address of the GDTR is passed in as an argument
	mov eax, [esp + 4]
	lgdt [eax]

	; We now need to reload CS using a far jump...
	jmp 0x08:.reloadSegments

.reloadSegments:
	; And all of the other segments by loading them
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	; Kernel doesn't use gs/fs
	mov ss, ax

	ret