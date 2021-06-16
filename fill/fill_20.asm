; Fill 1601:0000 to 1601:FFFF with ffh
; This is used to determine "safe" elements to modify (i.e. that ZZT sets
; pointers to zero for).

	org 100h
	use16

main:
	pusha
	push 01601h
	pop ds
	xor cx, cx
	dec cx				; is now FFFFh
	xor di, di
looping:
	mov byte ptr di, 0ffh
	inc di
	loop looping

	int 20h
