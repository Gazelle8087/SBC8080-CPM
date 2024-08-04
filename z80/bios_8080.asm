;	CBIOS for CP/M 2.2 running on emuz80-57q + SBC8080
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
;
;2024/7/28	first release
;2024/8/4	adustment for serial interrupt
;
;	Z80 CBIOS for Z80-Simulator
;
;	Copyright (C) 1988-2007 by Udo Munk
;
	CPU z80			;written in Zilog mnemonic,
				;however only use 8080 instruction
	include	"ipl_bios_inc.asm"

MSIZE	EQU	64		;cp/m version memory size in kilobytes
;
;	"bias" is address offset from 3400H for memory systems
;	than 16K (referred to as "b" throughout the text).
;
BIAS	EQU	(MSIZE-20)*1024
CCP	EQU	3400H+BIAS	;base of ccp
BDOS	EQU	CCP+806H	;base of bdos
BIOS	EQU	CCP+1600H	;base of bios
NSECTS	EQU	(BIOS-CCP)/128	;warm start sector count
CDISK	EQU	0004H		;current disk number 0=A,...,15=P
IOBYTE	EQU	0003H		;intel i/o byte
;
	ORG	BIOS		;origin of this program
;
;	jump vector for individual subroutines
;
	JP	BOOT		;cold start
WBOOTE: JP	WBOOT		;warm start
	JP	CONST		;console status
	JP	CONIN		;console character in
	JP	CONOUT		;console character out
	JP	LIST		;list character out
	JP	PUNCH		;punch character out
	JP	READER		;reader character in
	JP	HOME		;move head to home position
	JP	SELDSK		;select disk
	JP	SETTRK		;set track number
	JP	SETSEC		;set sector number
	JP	SETDMA		;set dma address
	JP	READ		;read disk
	JP	WRITE		;write disk
	JP	LISTST		;return list status
	JP	SECTRAN		;sector translate
;
;	fixed data tables for four-drive standard
;	IBM-compatible 8" SD disks
;
;	disk parameter header for disk 00
DPBASE:	DEFW	TRANS,0000H
	DEFW	0000H,0000H
	DEFW	DIRBF,DPBLK
	DEFW	CHK00,ALL00
;	disk parameter header for disk 01
	DEFW	TRANS,0000H
	DEFW	0000H,0000H
	DEFW	DIRBF,DPBLK
	DEFW	CHK01,ALL01
;	disk parameter header for disk 02
	DEFW	TRANS,0000H
	DEFW	0000H,0000H
	DEFW	DIRBF,DPBLK
	DEFW	CHK02,ALL02
;	disk parameter header for disk 03
	DEFW	TRANS,0000H
	DEFW	0000H,0000H
	DEFW	DIRBF,DPBLK
	DEFW	CHK03,ALL03
;
;	sector translate vector for the IBM 8" SD disks
;
TRANS:	DEFB	1,7,13,19	;sectors 1,2,3,4
	DEFB	25,5,11,17	;sectors 5,6,7,8
	DEFB	23,3,9,15	;sectors 9,10,11,12
	DEFB	21,2,8,14	;sectors 13,14,15,16
	DEFB	20,26,6,12	;sectors 17,18,19,20
	DEFB	18,24,4,10	;sectors 21,22,23,24
	DEFB	16,22		;sectors 25,26
;
;	disk parameter block, common to all IBM 8" SD disks
;
DPBLK:  DEFW	26		;sectors per track
	DEFB	3		;block shift factor
	DEFB	7		;block mask
	DEFB	0		;extent mask
	DEFW	242		;disk size-1
	DEFW	63		;directory max
	DEFB	192		;alloc 0
	DEFB	0		;alloc 1
	DEFW	16		;check size
	DEFW	2		;track offset
;
;	fixed data tables for 4MB harddisks
;
;	disk parameter header
HDB1:	DEFW	0000H,0000H
	DEFW	0000H,0000H
	DEFW	DIRBF,HDBLK
	DEFW	CHKHD1,ALLHD1
HDB2:	DEFW	0000H,0000H
	DEFW	0000H,0000H
	DEFW	DIRBF,HDBLK
	DEFW	CHKHD2,ALLHD2
;
;       disk parameter block for harddisk
;
HDBLK:  DEFW    128		;sectors per track
	DEFB    4		;block shift factor
	DEFB    15		;block mask
	DEFB    0		;extent mask
	DEFW    2039		;disk size-1
	DEFW    1023		;directory max
	DEFB    255		;alloc 0
	DEFB    255		;alloc 1
	DEFW    0		;check size
	DEFW    0		;track offset
;
;	messages
;
SIGNON: DB	'64K CP/M Vers. 2.2 (Z80 CBIOS V1.2 for Z80SIM, '
	DB	'Copyright 1988-2007 by Udo Munk)'
	DB	13,10,0
;

	ORG	BIOS+130h

PRTMSG:	LD	A,(HL)
	OR	A
	RET	Z
	LD	C,A
	CALL	CONOUT
	INC	HL
	JP	PRTMSG
;
;	individual subroutines to perform each function
;	simplest case is to just perform parameter initialization
;
BOOT:   DI
	LD	SP,80H		;use space below buffer for stack
	LD	HL,SIGNON	;print message
	CALL	PRTMSG
	XOR	A		;zero in the accum
	LD	(IOBYTE),A	;clear the iobyte
	LD	(CDISK),A	;select disk zero
	JP	GOCPM		;initialize and go to cp/m
;
;	simplest case is to read the disk until all sectors loaded
;
WBOOT:  DI
	LD	SP,80H		;use space below buffer for stack
	IN	A,(GET_BDOSCCP)
GOCPM:
	LD	A,0C3H		;c3 is a jmp instruction
	LD	(0),A		;for jmp to wboot
	LD	HL,WBOOTE	;wboot entry point
	LD	(1),HL		;set address field for jmp at 0
;
	LD	(5),A		;for jmp to bdos
	LD	HL,BDOS		;bdos entry point
	LD	(6),HL		;address field of jump at 5 to bdos
;
	LD	BC,80H		;default dma address is 80h
	CALL	SETDMA
;
	LD	A,(CDISK)	;get current disk number
	LD	C,A		;send to the ccp
	JP	CCP		;go to cp/m for further processing

CONST:	IN	A,(UART_C8251)	;console status(ready:FF busy:00)
	AND	2
	JP	Z,CONSTB
	LD	A,0FFh
	RET
CONSTB:	XOR	A
	RET

CONIN:	IN	A,(UART_C8251)	;get character from console
	AND	2
	JP	Z,CONIN
	IN	A,(UART_D8251)
	RET

CONOUT:	IN	A,(UART_C8251)
	AND	1
	JP	Z,CONOUT
	LD	A,C
	OUT	(UART_D8251),A
	RET

LISTST: LD	A,0ffh	;list status (ready:FF not ready:00) always ready (dummy)
READER: 		;reader (do nothing)
LIST:			;list (do nothing)
PUNCH:			;punch (do nothing)
	RET
;
;	i/o drivers for the disk follow
;
;	move to the track 00 position of current drive
;	translate this call into a settrk call with parameter 00
;
HOME:	XOR	A		;select track 0
	LD	(TRK_NO),A
	RET			;we will move to 00 on first read/write
;
;	select disk given by register C
;
SELDSK: LD	HL,0000H	;error return code
	LD	A,C
	CP	4		;FD drive 0-3?
	JP	C,SELFD		;go
	CP	8		;harddisk 1?
	JP	Z,SELHD1	;go
	CP	9		;harddisk 2?
	JP	Z,SELHD2	;go
	RET			;no, error
;	disk number is in the proper range
;	compute proper disk parameter header address
SELFD:
	LD	(DRV_NO),A	;selekt disk drive
	LD	L,A		;L=disk number 0,1,2,3
	ADD	HL,HL		;*2
	ADD	HL,HL		;*4
	ADD	HL,HL		;*8
	ADD	HL,HL		;*16 (size of each header)
	LD	DE,DPBASE
	ADD	HL,DE		;HL=.dpbase(diskno*16)
	RET
SELHD1:	LD	HL,HDB1		;dph harddisk 1
	JP	SELHD
SELHD2:	LD	HL,HDB2		;dph harddisk 2
SELHD:
	LD	(DRV_NO),A	;select harddisk drive
	RET
;
;	set track given by register c
;
SETTRK: LD	A,C
	LD	(TRK_NO),A
	RET
;
;	set sector given by register c
;
SETSEC: LD	A,C
	LD	(SEC_NO),A
	RET
;
;	translate the sector given by BC using the
;	translate table given by DE
;
SECTRAN:
	LD	A,D		;do we have a translation table?
	OR	E
	JP	NZ,SECT1	;yes, translate
	LD	L,C		;no, return untranslated
	LD	H,B		;in HL
	INC	L		;sector no. start with 1
	RET	NZ
	INC	H
	RET
SECT1:	EX	DE,HL		;HL=.trans
	ADD	HL,BC		;HL=.trans(sector)
	LD	L,(HL)		;L = trans(sector)
	LD	H,0		;HL= trans(sector)
	RET			;with value in HL
;
;	set dma address given by registers b and c
;
SETDMA: LD	A,C		;low order address
	LD	(DMA_L),A
	LD	A,B		;high order address
	LD	(DMA_H),A
	RET
;
;	perform read operation
;
READ:	IN	A,(DISK_READ)
	IN	A,(FDCST)	;status of i/o operation -> A
	OR	A
	RET
;
;	perform a write operation
;
WRITE:	IN	A,(DISK_WRITE)
	IN	A,(FDCST)	;status of i/o operation -> A
	RET

;	the remainder of the CBIOS is reserved uninitialized
;	data area, and does not need to be a part of the
;	system memory image (the space must be available,
;	however, between "begdat" and "enddat").
;
;	scratch ram area for BDOS use
;
BEGDAT	EQU	$		;beginning of data area
DIRBF:	DB	128	dup(0)	;scratch directory area
ALL00:	DB	31	dup(0)	;allocation vector 0
ALL01:	DB	31	dup(0)	;allocation vector 1
ALL02:	DB	31	dup(0)	;allocation vector 2
ALL03:	DB	31	dup(0)	;allocation vector 3
ALLHD1:	DB	255	dup(0)	;allocation vector harddisk 1
ALLHD2:	DB	255	dup(0)	;allocation vector harddisk 2
CHK00:	DB	16	dup(0)	;check vector 0
CHK01:	DB	16	dup(0)	;check vector 1
CHK02:	DB	16	dup(0)	;check vector 2
CHK03:	DB	16	dup(0)	;check vector 3
CHKHD1:	DB	0	dup(0)	;check vector harddisk 1
CHKHD2:	DB	0	dup(0)	;check vector harddisk 2

	END			;of BIOS
