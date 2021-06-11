
; This is for ZZT 3.2 only. ZZT 3.0 data addresses are offset by four bytes (if you want
; to port this to 3.0).

; Known bugs: the charset object's character changes to char 0 for some reason...
; It seems to clean up properly after itself otherwise.
; TODO: Clean up db 13s, then release.
; TODO: Find out why Windows XP VirtualBox crashes (CS:IP 3831:02b6)

	org 0h
	use16

entry_pt:
	inc ax		; @ denotes label to ZZT
	jmp main
	db " Code"
	db 13

	; The extra spaces after #end make things easier to read in Hacker's View.

	db "'Beware: Here there be x86 code! Changing", 13
	db "'it will probably cause a crash.", 13
	db '#end', 20h, 20h, 20h, 13

main:
	; Push the base pointer because Pascal needs it.
	push bp

	; ------------------------- Clean up what we broke ----------------------

	push ds
	pop es

	xor ax, ax

	; We need to restore these values:
	; DS:1951 - DS:1955:	00 2C 00 17		(intermediate location for pointer)
	; DS:51B5 - DS:51B9:	00 00 00 00		(secondary replicators)
	; DS:8A19 - DS:8A1D:	00 4C 00 4C		(pointer for element 83)

	; Secondary replicators
	mov di, 051b5h
	mov word ptr di, ax
	mov word ptr di+2, ax

	; Intermediate location for ptr
	mov di, 01951h
	mov ah, 2ch
	mov word ptr di, ax		; LSB, so this resolves to 00 2C
	mov ah, 17
	mov word ptr di+2, ax		; Resolves to 00 17

	; Entry pointer at Element 83
	mov di, 08a19h
	mov ah, 4ch
	mov word ptr di, ax
	mov word ptr di+2, ax

	; -------------------------- End cleanup ---------------------------


	; ------------------------- Fingerprinting -------------------------

; Run a pretty simple hash on the BIOS signature at F000:E060 and compare
; against a known value to detect DOSBox.

; 16-bit variant of djb2 from http://www.cse.yorku.ca/~oz/hash.html, with a
; different initial prime.

	xor cx, cx
	mov bx, 0BA0Dh		; Initial prime
	mov cl, 10h			; number of bytes to hash

	push ds				; Push the real data segment for later recovery
	mov si, 0F001h		; Load segment F001 (literal push not allowed in Zeta)
	push si
	pop ds
	mov si, 0E050h		; equivalent to F000:E060

hash:
	mov ax, bx
	; Rotate ax left by five. It has to be done this way because Zeta doesn't
	; support the whole 80186 opcode set.
	rol ax, 1
	rol ax, 1
	rol ax, 1
	rol ax, 1
	rol ax, 1

	add bx, ax
	xor ax, ax
	lodsb				; Load ds:si to ax, then increment si
	add bx, ax
	loop hash

	pop ds				; Recover ds.

	; The hash value is now in bx, and we want to set gems to it.
	; However, we can't test or change negative values in ZZT, so AND
	; away the high bit.
	and bh, 7fh

	; Now set the values.

	mov si, 4822h	; Location of ammo counter in ZZT 3.2
	xor ax, 13		; KevEdit separator (effectively a NOP)

	; Just for the fun of it, set ammo to slightly above 32767
	mov word ptr si, 800Ah	; Set ammo
	mov word ptr si+2, bx	; Set gems to our hash

	cmp bx, 24491			; Are we using Zeta?
	je short denouement_bounce	; Skip the demo if so because it doesn't implement
								; the required interrupts properly. We need a short
								; jmp since Zeta doesn't understand near jumps.

	; First phase is done.
	jmp short first_phase_bounce	; Must be a short jump or we'll have NULs in the code

	; Some data for the routine that sets palette colors. This gives the index
	; into VGA colors for each text-mode color. It has to be located here because
	; some later calculations depend on the distance from start to this label being
	; an 8-bit integer.
color_indices:
	; The first one here is actually 0, but KevEdit doesn't like that, so I've
	; added a high bit.
	db 80h, 1, 2, 3, 4, 5, 014h, 7, 038h, 039h, 03Ah, 03Bh, 03Ch, 03Dh, 03Eh, 03Fh

	db 13			; KevEdit break

	; Demo routines

	; ### Setting a display test mode ###
	; AX must either be set to 1202h for 400 scanlines or 1201h for 350 scanlines.
	; See http://www.ctyme.com/intr/rb-0167.htm.
	; CL=01h sets the character size to 8x14, CL=12h to 8x8.

set_display_mode:
	mov bl, 30h		; Set number of scanlines
	int 10h

	xor ax, ax
	mov al, 083h	; Set display mode 3, and don't overwrite video mem (high bit set)
	int 10h

	mov ah, 11h		; Load 8x8 character table (1112h) or 8x14 (1101h)
	mov al, cl
	xor bl, bl		; Block 0
	int 10h			; http://www.ctyme.com/intr/rb-0147.htm

	mov ah, 1		; Disable cursor
	mov ch, 26h
	int 10h

	ret

	db 13

	; #### Loading a font. ####

	; Inputs: ES:BP: start of character data
	;                (blocks of 40 bytes with 8 bytes per character, then newline)

loadfont:
	; First XOR with the constant used to get around KevEdit's inability to deal
	; with NUL bytes.
	mov cx, 2099	; 256 bytes at 8 per = 2048, plus 51 newlines.
pre_xorloop:
	inc bp
	; The magic constant is chosen so that no data bytes have value 0.
	; The -1 avoids another NUL byte, but requires the increment to be
	; first.
	xor byte [es:bp-1], 35
	loop pre_xorloop

	sub bp, 2099	; All done.

	xor ax, ax		; http://www.ctyme.com/intr/rb-0136.htm
	mov ah, 11h		; ax=1100h: load user-defined font

	xor dx, dx		; Start redefining characters at char 0
	mov bh, 8		; height of a character is 8
	xor bl, bl		; and we're loading to font block 0

loadfont_loop:
	xor cx, cx
	mov cl, 5		; Number of characters to redefine: 40/8 = 5
	int 10h			; Do it.

	add dx, 5		; Add 5 to the character offset for next redefinition
	add bp, 41  	; Skip the 40 bytes we used for definition, plus newline
	mov cl, 13		; Break line for KevEdit
	cmp dl, 255 	; Check if we're done defining characters
					; TODO: If we're defining fewer characters, might not actually
					; be necessary!

	jb loadfont_loop ; Not done, so loop.

	; Manually redefine character 0xFF because the above loop only
	; redefines to 5*51=255 exclusive.
	xor cx, cx
	mov cl, 1
	int 10h

	; Clean up our mess by XORing the charset back the way it was.
	mov cx, 2099
	sub bp, cx		; Now the bp register count is actually 8 too low because
					; we didn't increment bp to account for the last character.
					; But have no fear...
post_xorloop:
	; We account for that right here.
	xor byte [es:bp+8], 35
	inc bp
	loop post_xorloop

	ret

; Some trampoline action...

first_phase_bounce:
	jmp short demo

denouement_bounce:
	jmp short denouement_bounce_two

	db 13

	; ### Setting palette colors. ###

	; Inputs: DS:SI: start of color data
	;         ES:DI: start of color_indices

set_colors:

	; We need to set the colors one at a time because the text mode colors
	; are not contiguous in the VGA palette. Blame IBM. :-)

	; The color table is in Blue-Green-Red order.

	xor bl, bl				; Disable blink
	mov ax, 1003h
	int 10h					; http://www.ctyme.com/intr/rb-0117.htm

	xor bp, bp				; for the lack of another register

set_colors_loop:
	mov bl, [es:di+bp]		; Color index
	and bl, 03Fh			; Remove the high bits used to conceal NUL from KevEdit
	mov cx, [ds:si]			; Blue and green
	inc si
	inc si
	mov dh, [ds:si]			; Red
	inc si
	mov ax, 1010h			; Set a color of the palette
	int 10h					; http://www.ctyme.com/intr/rb-0121.htm
	inc bp
	cmp bp, 8				; Eight colors until a newline
	jne dont_skip_newline
	inc si					; Skip over the newline
dont_skip_newline:
	cmp bp, 16				; And at 16 colors, we're done
	jne set_colors_loop
	ret

	db 13

backup_restore_picture:
	; Copy 4000 bytes from B800:SI to B800:DI.
	; This is used to stash the ZZT screen somewhere else before overwriting the
	; screen memory, and then later to recover it.

	push ds					; We're going to overwrite DS (DS:SI), so store it for later

	xor bx, bx
	mov bh, 0B8h
	push bx					; Push B800
	push bx
	pop es
	pop ds					; Now DS and ES are both B800

	mov cx, 2000			; copy 80 * 25 * 2 / 2 = 2000 words
	rep movsw

	pop ds					; Restore ds
	ret

; More trampoline action, then...
denouement_bounce_two:
	jmp short denouement

	; ------------- Back to main! -------------
demo:

	; ---------------- Dump picture, pt. 1 ---------------
	; Dump what's already in screen memory to a spare location (B800:2002)

	xor di, 13
	mov di, 2002h			; destination ES:DI = B800:2002
	xor si, si				; source	  DS:SI = B800:0000
	call backup_restore_picture

	; ////// Dump picture, pt. 1 done /////

	; Set 80x50 mode
	mov cl, 12h
	mov ax, 1202h
	call set_display_mode

	; Get the demo going.
	; The data pointers are in the following locations (0-indexed):
	; N/A			Object #0, the player
	; DS:3205h		Object #1, this object
	; DS:3226h		Object #2, charset
	; DS:3247h		Object #3, palette
	; DS:3268h		Object #4, image
	; DS:3289h		Object #5

	; Get the pointer for object #3 (palette)
	mov si, 03247h - 13
	add si, 13				; Serves as a newline for KevEdit

	; Set ES:DI to the address of the color indices. This one is a bit harder than one
	; would expect since we can't assume anything about the base address of this code
	; and so will have to read it off from memory.

	mov di, 03205h
	les di, [di]			; Set ES:DI to the start of this code

	; Set DS:SI to the start of the palette data
	push ds					; Need the original ds later
	lds si, [si]			; Set FS:SI according to ptr (not Zeta safe)
	add si, 17				; Add the number of bytes of the ZZT preamble

	; Add the relative offset to the color indices. The roundabout nature is because a
	; literal addition would include a NUL byte.
	xor ax, ax
	mov al, color_indices - entry_pt
	add di, ax

	call set_colors
	pop ds

	; Change the charset.

	mov di, 03226h			; The charset data is in object #2
	les bp, [di]			; Load pointer
	add bp, 17				; Add 17 to the offset to skip ZZT preamble.
	call loadfont

	; ---------------- Dump picture, pt. 2 ---------------
	;

	xor bx, bx
	mov bh, 0b8h
	push bx					; Push B800
	pop es
	push ds					; We're going to overwrite DS (DS:SI), so store it for later

	xor di, 13				; KevEdit nop
	mov di, 03268h			; The picture is in object #4
	lds si, [di]			; Load pointer
	add si, 17				; Add 17 to the offset to skip ZZT preamble.

	xor di, di				; ES:DI is now B800:0000
copy_to_display:
	xor cx, cx
	mov cl, 20				; Load 20 words (40 bytes) at a time
	rep movsw				; word ptr es:[di], word ptr [si]
	inc si					; Skip over the newline
	inc bl
	cmp bl, 200				; 8000 / 40 = 200
	jbe copy_to_display		; and keep going until done.

	xor ax, ax				; Force the lone zero byte that we couldn't represent
	mov [es:5414], ah		; directly in the image data without breaking KevEdit.

	pop ds					; Recover ds

	; ---- Done dumping picture ----

	; Wait for a keypress
	xor ax, ax
	int 16h

	mov ah, 13				; KevEdit separator

	; Set 80x25 EGA mode
	mov cl, 01h
	mov ax, 1201h
	call set_display_mode

	; ---------------- Dump picture, pt. 3 ---------------
	; Restore B800:2002 to screen

	xor si, 13
	mov si, 2002h			; source	  DS:SI = B800:2002
	xor di, di				; destination ES:DI = B800:0000
	call backup_restore_picture

denouement:
	; Restore base pointer.
	pop bp

	; Call passage code.
	; The object that contains the destination board
	; as P3 is at (11,4), so push those coordinates.
	xor ax, ax
	mov al, 11
	push ax
	mov al, 4
	push ax

	; The rest doesn't matter:
	push ax					; sourceStatId

	; The delta* variables get set to zero, so point to something that's
	; already zero.
	xor ax, ax
	push ds					; deltaX (segment)
	push ax					; deltaX (offset)
	push ds					; deltaY
	push ax

	; Transport to the destination board. Note that we're pulling the rug out from
	; under our feet since object code is located in dynamic memory and going to
	; another board frees these. So instead of CALLing directly, we must simply
	; push an appropriate return address onto the stack and then jump to the
	; passage function.

	; The appropriate return address points to a location inside the tile area,
	; and that tile has been given a color so that it decodes to a RETF function.

	; ---

							; We want to draw an empty, and ax = 0...
	mov [bp-1], al			; Param [bp-1] specifies the char to be drawn.

	; Now prepare to get out of here.
	mov di, 2e1eh			; Location of RETF opcode in tile area of destination board
	push ds
	push di

	jmp dword ptr 534bh		; Location of passage function pointer in ZZT 3.2