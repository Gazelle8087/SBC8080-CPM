;	IPL for emuz80-57q
;
;	Copyright (C) 2024 by Gazelle
;
;Permission is hereby granted, free of charge, to any person
;obtaining a copy of this software and associated documentation
;files (the "Software"), to deal in the Software without
;restriction, including without limitation the rights to use,
;copy, modify, merge, publish, distribute, sublicense, and/or sell
;copies of the Software, and to permit persons to whom the
;Software is furnished to do so, subject to the following
;conditions:
;
;The above copyright notice and this permission notice shall be
;included in all copies or substantial portions of the Software.
;
;THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;OTHER DEALINGS IN THE SOFTWARE.

	page	0
	cpu	Z180
	include	"ipl_bios_inc.asm"

	ORG	0		;mem base of boot
MSIZE	EQU	64		;mem size in kbytes

BIAS	EQU	(MSIZE-20)*1024	;offset from 20k system
CCP	EQU	3400H+BIAS	;base of the ccp
BIOS	EQU	CCP+1600H	;base of the bios

OMCR_V	EQU	0E0h
CLOCK_0	EQU	0
CLOCK_1	EQU	0
CLOCK_2	EQU	1

	NOP
	NOP
	NOP
	DI
	JP	COLD

	ORG	80h
IPLMSG:	DB	0dh,0ah,"IPL:",0

COLD:
	DI
	LD	SP,IPLMSG
	LD	HL,IPLMSG
	CALL	STROUT

; check 8080/8085/MYCPU80
	LD	A,07fh
	INC	A
	JP	PE,check_z80

	LD	BC,0FFFFH
	PUSH	BC
	POP	AF
	PUSH	AF
	POP	DE

	LD	A,E
	AND	A,00100010B	;if bit51 is 00
	JP	Z,ID_MYCPU	;then MYCPU80

	LD	A,E
	AND	A,00101010B	;if bit 531 is 001
	CP	A,00000010B	;then intel i8080
	JP	Z,ID_8080AF

	LD	A,E
	AND	A,00001010B	;if bit 31 is 11
	CP	A,00001010B	;then NEC uPD8080A(not AF)
	JP	Z,ID_UPD8080A

	LD	A,E
	AND	A,00101010B	
	CP	A,00100010B
	JP	NZ,LD_CPM
	LD	BC,0
	PUSH	BC
	POP	AF
	PUSH	AF
	POP	DE
	LD	A,E
	AND	A,00101010B	;if bit 51 is writable
	JP	Z,ID_8085	;then 8085
	JP	LD_CPM

; check Z80 or Z80180
check_z80:
	LD	BC,00FFH
	DB	0EDH,4CH	; MLT BC (HD64180)
	LD	A,B
	OR	C
	JP	NZ,ID_Z80

; HD64180R/Z or Z8S180

	LD	A,IOBASE_180
	OUT0	(3Fh),A

	LD	A,00H			; memory 0 wait IO 1 wait
	OUT0	(IOBASE_180+32H),A	; DMA/WAIT Control Register (DCNTL)
	LD	A,00H			; no refresh
	OUT0	(IOBASE_180+36H),A	; Refresh Control Register (RCR)

;check HD64180R or HD64180Z

	LD	A,7FH			; Try to change OMCR bit 7 to 0
	OUT0	(IOBASE_180+3EH),A
	IN0	A,(IOBASE_180+3EH)
	AND	80H
	JR	NZ, ID_180R		; HD64180R

;check HD64180Z(Z80180) or Z8S180

	LD	A,OMCR_V
	OUT0	(IOBASE_180+3EH),A	; Restore OMCR

	XOR	A
	OUT0	(IOBASE_180+12H),A
	IN0	A,(IOBASE_180+12H)
	AND	40H
	JR	NZ,ID_180Z

; Z8S180

;; CPU clock = 2.0 x external clock
	IF	CLOCK_2
	LD	A,010H			; memory 0 wait IO 2 wait
	OUT0	(IOBASE_180+32H),A	; DMA/WAIT Control Register (DCNTL)
	LD	A,80H			; Clock Divide XTAL/1 Bit 7=1
	OUT0	(IOBASE_180+1FH),A	; CPU Control Register (CCR)
	LD	A,0FFH			; X2 Clock Multiplier Mode : Enable Bit 7=1
	OUT0	(IOBASE_180+1EH),A	; Clock Multiplier Register (CMR)
	LD	HL,Z8S_2
	CALL	STROUT
	JP	LD_CPM
Z8S_2:	DB	"Z8S180 running memory 0wait I/O 2wait 2x clock",0dh,0ah,0ah,0
	ENDIF

;; CPU clock = 1.0 x external clock
	IF	CLOCK_1
	LD	A,80H			; Clock Divide XTAL/1 Bit 7=1
	OUT0	(IOBASE_180+1FH),A	; CPU Control Register (CCR)
	LD	A,07FH			; X2 Clock Multiplier Mode : Enable Bit 7=1
	OUT0	(IOBASE_180+1EH),A	; Clock Multiplier Register (CMR)
	OUT0	(IOBASE_180+1EH),A	; Clock Multiplier Register (CMR)
	LD	HL,Z8S_1
	CALL	STROUT
	JP	LD_CPM
Z8S_1:	DB	"Z8S180 running memory 0wait I/O 1wait 1x clock",0dh,0ah,0ah,0
	ENDIF

;; CPU clock = 0.5 x external clock
	IF	CLOCK_0
	LD	A,00H			; Clock Divide XTAL/1 Bit 7=1
	OUT0	(IOBASE_180+1FH),A	; CPU Control Register (CCR)
	LD	A,07FH			; X2 Clock Multiplier Mode : Enable Bit 7=1
	OUT0	(IOBASE_180+1EH),A	; Clock Multiplier Register (CMR)
	OUT0	(IOBASE_180+1EH),A	; Clock Multiplier Register (CMR)
	LD	HL,Z8S_5
	CALL	STROUT
	JP	LD_CPM
Z8S_5:	DB	"Z8S180 running memory 0wait I/O 1wait 0.5x clock",0dh,0ah,0ah,0
	ENDIF

	;; HD64180Z and HD64180R
ID_180R:
ID_180Z:
	LD	HL,HD64180
	CALL	STROUT
	JP	LD_CPM
HD64180:DB	"HD64180 running memory 0wait I/O 1wait 0.5x clock",0dh,0ah,0ah,0

ID_8080AF:
	LD	HL,I8080AF
	CALL	STROUT
	JP	LD_CPM
I8080AF:DB	"i8080 running (flag bit2 is Parity after arithmatic, bit531 is 001)",0dh,0ah,0ah,0

ID_UPD8080A:
	LD	HL,I8080A
	CALL	STROUT
	JP	LD_CPM
I8080A:	DB	"uPD8080A running (flag bit2 is Parity after arithmatic, bit31 is 11)",0dh,0ah,0ah,0

ID_8085:
	LD	HL,I8085
	CALL	STROUT
	JP	LD_CPM
I8085:	DB	"i8085 running (flag bit2 is Parity after arithmatic, bit5,1 writable)",0dh,0ah,0ah,0


ID_MYCPU:
	LD	HL,MYCPU
	CALL	STROUT
	JP	LD_CPM
MYCPU:	DB	"MYCPU80 running (flag bit2 is Parity after arithmatic, bit1,5 always 0)",0dh,0ah,0ah,0

ID_Z80:
	LD	HL,Z80
	CALL	STROUT
	JP	LD_CPM
Z80:	DB	"Z80 running (flag bit2 is Overflow after arithmatic, no MLT function)",0dh,0ah,0ah,0

LD_CPM:
	IN	A,(GET_BIOS)	;LOAD BIOS from pic ROM
	IN	A,(GET_BDOSCCP)	;LOAD BDOS & CCP from pic ROM
	JP	BIOS

STROUT:
	LD	A,(HL)
	OR	A
	RET	Z
	LD	C,A
	CALL	CONOUT
	INC	HL
	JP	STROUT

CONOUT:
	IN	A,(UART_C8251)
	AND	1
	JP	Z,CONOUT
	LD	A,C
	OUT	(UART_D8251),A
	RET

	END
