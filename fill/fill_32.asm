; Fill 170F:0000 to 170F:FFFF with ffh
; This is used to determine "safe" elements to modify (i.e. that ZZT sets
; pointers to zero for). A different segment may be needed for ZZT 2.0.

	org 100h
	use16

main:
	pusha
	push 0170fh
	pop ds
	xor cx, cx
	dec cx				; is now FFFFh
	xor di, di
looping:
	mov byte ptr di, 0ffh
	inc di
	loop looping

	int 20h