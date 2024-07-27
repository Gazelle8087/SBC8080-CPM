/*
 * Copyright (c) 2023 @hanyazou
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/*
 * UART, disk I/O and monitor firmware for SuperMEZ80
 *
 * Based on main.c by Tetsuya Suzuki and emuz80_z80ram.c by Satoshi Okue
 * Modified by @hanyazou https://twitter.com/hanyazou
 */
/*!
 * PIC18F47Q43/PIC18F47Q83/PIC18F47Q84 ROM image uploader and UART emulation firmware
 * This single source file contains all code
 *
 * Target: EMUZ80 with Z80+RAM
 * Compiler: MPLAB XC8 v2.40
 *
 * Modified by Satoshi Okue https://twitter.com/S_Okue
 * Version 0.1 2022/11/15
 */

/*
	PIC18F47Q43 ROM RAM and UART emulation firmware
	This single source file contains all code

	Target: EMUZ80 - The computer with only Z80 and PIC18F47Q43
	Compiler: MPLAB XC8 v2.36
	Written by Tetsuya Suzuki
*/
#define INCLUDE_PIC_PRAGMA
#define BOARD_DEPENDENT_SOURCE

#include "../supermez80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/SDCard.h"
#include "../../drivers/picregister.h"
#include "../../drivers/SPI.h"
#include "../../drivers/utils.h"

static FATFS fs;
uint8_t io_addr;

const unsigned char rom[] = {	// Initial program loader at 0x0000
#include "../../z80/ipl_8080.inc"
};

const unsigned char bdosccp[] = {	// CCP and BDOS of CP/M 2.2 at 0E400h
#include "../../z80/CPM22_CCP_BDOS.inc"
};

const unsigned char bios[] = {		// BIOS of CP/M 2.2 at 0FA00h
#include "../../z80/bios_8080.inc"
};

#define CPU_HOLD		E0
#define CPU_RESET		E1
#define CPU_INT			E2
#define CPU_IORD		A0
#define CPU_IOWR		A1
#define CPU_READY		A4
#define CPU_HOLDA		C6
#define CPU_CLK			A3

#define ADDR_BUS_L		B
#define ADDR_BUS_H		D
#define DATA_BUS		F

#define SPI_PREFIX		SPI_SD
#define SPI_HW_INST		SPI1

#define SPI_SD_SS		C4
#define SPI_SD_PICO		C1
#define SPI_SD_CLK		C0
#define SPI_SD_POCI		C2

#define SRAM_OE			A5
#define SRAM_WE			C5
#define SRAM_CS2		C7

/*
void emuz80_57q_bus_master(uint8_t enable)
{
    if (enable) {
		while(!R(CPU_HOLDA)) ;
		// Set address bus as output
		TRIS(ADDR_BUS_L) = 0x00;	// A7-A0
		TRIS(ADDR_BUS_H) = 0x00;	// A15-A8

		// Set /MEMRQ, /RD and /WR as output
		LAT(SRAM_OE) = 1;			// deactivate /RD
		LAT(SRAM_WE) = 1;			// deactivate /WR
		TRIS(SRAM_OE) = 0;			// output
		TRIS(SRAM_WE) = 0;			// output
	} else {
		// Set address bus as input
		TRIS(ADDR_BUS_L) = 0xff;	// A7-A0
		TRIS(ADDR_BUS_H) = 0xff;	// A15-A8
		TRIS(DATA_BUS) = 0xff;		// D7-D0 pin

		// Set /MEMRQ, /RD and /WR as input
		TRIS(SRAM_OE) = 1;			// input
		TRIS(SRAM_WE) = 1;			// input
	}
}
*/
void emuz80_57q_enter_bus_master(void)
{
	while(!R(CPU_HOLDA)) ;
	// Set address bus as output
	TRIS(ADDR_BUS_L) = 0x00;	// A7-A0
	TRIS(ADDR_BUS_H) = 0x00;	// A15-A8
	// Set /MEMRQ, /RD and /WR as output
	LAT(SRAM_OE) = 1;			// deactivate /RD
	LAT(SRAM_WE) = 1;			// deactivate /WR
	TRIS(SRAM_OE) = 0;			// output
	TRIS(SRAM_WE) = 0;			// output
}

void emuz80_57q_exit_bus_master(void)
{
	// Set address bus as input
	TRIS(ADDR_BUS_L) = 0xff;	// A7-A0
	TRIS(ADDR_BUS_H) = 0xff;	// A15-A8
	TRIS(DATA_BUS) = 0xff;		// D7-D0 pin
	// Set /MEMRQ, /RD and /WR as input
	TRIS(SRAM_OE) = 1;			// input
	TRIS(SRAM_WE) = 1;			// input
}

uint8_t get_from_sram(uint16_t addr)
{
	union address_bus_u ab;
	uint8_t	ret_val;

	ab.w = addr;
	LAT(ADDR_BUS_H) = ab.h;
	LAT(ADDR_BUS_L) = ab.l;
	LAT(SRAM_OE) = 0;
	NOP();		// DO NOT remove this NOP, or SRAM would not respond in time.
	NOP();		// see compile list for the reason.
	ret_val =  PORT(DATA_BUS);
	LAT(SRAM_OE) = 1;
	return ret_val;
}

void emuz80_57q_write_to_sram(uint16_t addr, uint8_t *buf, unsigned int len)
{
	union address_bus_u ab;
	unsigned int i;

	ab.w = addr;
	LAT(ADDR_BUS_H) = ab.h;
	LAT(ADDR_BUS_L) = ab.l;
	TRIS(DATA_BUS) = 0x00;		// databus output
	for(i = 0; i < len; i++) {
		LAT(SRAM_WE) = 0;      // activate /WE
		LAT(DATA_BUS) = ((uint8_t*)buf)[i];
		LAT(SRAM_WE) = 1;      // deactivate /WE
		LAT(ADDR_BUS_L) = ++ab.l;
		if (ab.l == 0) {
			ab.h++;
			LAT(ADDR_BUS_H) = ab.h;
		}
	}
}

void emuz80_57q_read_from_sram(uint16_t addr, uint8_t *buf, unsigned int len)
{
	union address_bus_u ab;
	unsigned int i;

	ab.w = addr;
	LAT(ADDR_BUS_H) = ab.h;
	LAT(ADDR_BUS_L) = ab.l;
	TRIS(DATA_BUS) = 0xff;		// data bus input
	for(i = 0; i < len; i++) {
		LAT(SRAM_OE) = 0;      // activate /OE
		((uint8_t*)buf)[i] = PORT(DATA_BUS);
		LAT(SRAM_OE) = 1;      // deactivate /OE
		LAT(ADDR_BUS_L) = ++ab.l;
		if (ab.l == 0) {
			ab.h++;
			LAT(ADDR_BUS_H) = ab.h;
		}
	}
}

static void emuz80_57q_wait_for_programmer()
{
	//
	// Give a chance to use PRC (RB6/A6) and PRD (RB7/A7) to PIC programer.
	// It must prevent Z80 from driving A6 and A7 while this period.
	//
	printf("\n\r");
	printf("wait for programmer ...\r");
	__delay_ms(200);
	printf("                       \r");
}

static void emuz80_57q_SD_init()
{
// Initialize SD Card
	static int retry;
	for (retry = 0; 1; retry++) {
		if (20 <= retry) {
			printf("No SD Card?\n\r");
			while(1);
	}
		if (SDCard_init(SPI_CLOCK_100KHZ, SPI_CLOCK_8MHZ, /* timeout */ 100) == SDCARD_SUCCESS)
			break;
		__delay_ms(200);
	}
}

static void emuz80_57q_sys_init()
{
	// System initialize
	OSCFRQ = 0x08;      // 64MHz internal OSC

	// Disable analog function
	ANSELA = 0x00;
	ANSELB = 0x00;
	ANSELC = 0x00;
	ANSELD = 0x00;
	ANSELE0 = 0;
	ANSELE1 = 0;
	ANSELE2 = 0;
	ANSELF = 0x00;

	// RESET output pin
	LAT(CPU_RESET) = 0;         // Reset
	TRIS(CPU_RESET) = 0;        // Set as output

	LAT(SRAM_CS2) = 0;			// disable SRAM
	TRIS(SRAM_CS2) = 0;

//	RA2PPS = 0;
//	LATA2 = 1;					// buscut
//	TRISA2 = 0;

    // 8085 clock
#define CPU_CLK_NCO
#ifdef CPU_CLK_NCO
    PPS(CPU_CLK) = 0x3f;		// asign NCO1
    TRIS(CPU_CLK) = 0;			// NCO output pin
	NCO1INC = 0x20000;			// 4MHz	
//	NCO1INC = 0x28000;			// 5MHz	
//	NCO1INC = 0x30000;			// 6MHz	
//	NCO1INC = 0x40000;			// 8MHz	
//	NCO1INC = 0x49249;			// 9.14Hz	
//	NCO1INC = 0x55555;			// 10.6MHz	
//	NCO1INC = 0x80000;			// 16MHz	
	NCO1CLK = 0x00;				// Clock source Fosc
    NCO1PFM = 0;				// FDC mode
    NCO1OUT = 1;				// NCO output enable
    NCO1EN = 1;					// NCO enable
#else
    // Disable clock output for Z80 (Use external clock for Z80)
    PPS(CPU_CLK) = 0;			// select LATxy
    TRIS(CPU_CLK) = 1;			// set as input
	WPU(CPU_CLK) = 1;
    NCO1OUT = 0;				// NCO output disable
    NCO1EN = 0;					// NCO disable
#endif
	// HOLD output pin
	LAT(CPU_HOLD) = 1;			// HOLD request
	TRIS(CPU_HOLD) = 0;			// Set as output

	// HOLDA input pin
	WPU(CPU_HOLDA) = 1;			// Week pull up
	TRIS(CPU_HOLDA) = 1;		// Set as input

	LAT(CPU_RESET) = 1;			// LET 8228 release control bus
	while(!R(CPU_HOLDA)){}		// wait HOLDA

	// no use GPIO
	TRISC3 = 1;
	WPUC3 = 1;

	// UART3 initialize
	U3BRG = 416;        // 9600bps @ 64MHz
	U3RXEN = 1;         // Receiver enable
	U3TXEN = 1;         // Transmitter enable

	// UART3 Receiver
	WPUA7 = 1;
	TRISA7 = 1;         // RX set as input
	U3RXPPS = 0x07;     // RA7->UART3:RX3;

	// UART3 Transmitter
	LATA6 = 1;          // Default level
	TRISA6 = 0;         // TX set as output
	RA6PPS = 0x26;      // RA6->UART3:TX3;

	U3ON = 1;           // Serial port enable

	// Address bus
	LAT(ADDR_BUS_H) = 0x00;
	TRIS(ADDR_BUS_H) = 0x00;	// Set as output
	LAT(ADDR_BUS_L) = 0x00;
	TRIS(ADDR_BUS_L) = 0x00;	// Set as output

	// Data bus
	LAT(DATA_BUS) = 0x00;
	TRIS(DATA_BUS) = 0x00;		// Set as output

	// INT
	LAT(CPU_INT) = 0;			// deactive INT
	TRIS(CPU_INT) = 0;			// Set as output

	// /WE output pin
	LAT(SRAM_WE) = 1;	
	TRIS(SRAM_WE) = 0;			// Set as output

	// /OE output pin
	LAT(SRAM_OE) = 1;
	TRIS(SRAM_OE) = 0;			// Set as output

	LAT(SRAM_CS2) = 1;			// enable SRAM

	// SPI data and clock pins slew at maximum rate
	SLRCON(SPI_SD_PICO) = 0;
	SLRCON(SPI_SD_CLK) = 0;
	SLRCON(SPI_SD_POCI) = 0;
}

static void emuz80_57q_start_cpu(void)
{
	// Address bus A15-A8 pin
	WPU(ADDR_BUS_H) = 0xff;		// Week pull up
	TRIS(ADDR_BUS_H) = 0xff;	// Set as input

	// Address bus A7-A0 pin
	WPU(ADDR_BUS_L) = 0xff;		// Week pull up
	TRIS(ADDR_BUS_L) = 0xff;	// Set as input

	// Data bus D7-D0 input pin
	WPU(DATA_BUS) = 0xff;		// Week pull up
	TRIS(DATA_BUS) = 0xff;		// Set as input

	// /IORD input pin
	WPU(CPU_IORD) = 1;			// Week pull up
	TRIS(CPU_IORD) = 1;			// Set as input

	// /IOWR input pin
	WPU(CPU_IOWR) = 1;			// Week pull up
	TRIS(CPU_IOWR) = 1;			// Set as input

	// /RD input pin
	WPU(SRAM_OE) = 1;			// Week pull up
	TRIS(SRAM_OE) = 1;			// Set as input

	// /WR input pin
	WPU(SRAM_WE) = 1;			// Week pull up
	TRIS(SRAM_WE) = 1;			// Set as input

	// Unlock IVT
	IVTLOCK = 0x55;
	IVTLOCK = 0xAA;
	IVTLOCKbits.IVTLOCKED = 0x00;

	// Default IVT base address
	IVTBASE = 0x000008;

	// Lock IVT
	IVTLOCK = 0x55;
	IVTLOCK = 0xAA;
	IVTLOCKbits.IVTLOCKED = 0x01;

//========== CLC pin assign ===========
	CLCIN0PPS = 0x00;	// RA0 <- IORD
	CLCIN1PPS = 0x01;	// RA1 <- IOWR
	CLCIN2PPS = 0x0f;	// RB7 <- A7
	CLCIN3PPS = 0x0e;	// RB6 <- A6
	CLCIN4PPS = 0x15;	// RC5 <- WR
	CLCIN5PPS = 0x16;	// RC6 <- HOLDA
	CLCIN6PPS = 0x0d;	// RB5 <- A5
	CLCIN7PPS = 0x08;	// RB0 <- A0

//========== CLC1 IORD -> READY latch ===========
	CLCnSEL0 = 0;		// CLCIN0PPS <- RA0 <- IORD
	CLCnSEL1 = 127;		//
	CLCnSEL2 = 127;		//
	CLCnSEL3 = 127;		//

	CLCnGLS0 = 1;		// CLK <- IORD invert
	CLCnGLS1 = 0x10;	// D <- 1(I0 inv)
	CLCnGLS2 = 0;		// R NC
	CLCnGLS3 = 0;		// S NC

	CLCnPOL = 0x80;		// Q invert D no invert CLK no invert
	CLCnCON = 0x84;		// D-FF, no interrupt
	CLCDATA = 0x0;		// Clear all CLC outs

	CLC1IF = 0;			// Clear the CLC interrupt flag
	CLC1IE = 0;			// Interrupt is not enabled. This will be handled by polling.
	TRISA4 = 0;
	RA4PPS = 1;			// CLC1 -> READY
// ========== CLC2 IOWR 00xxxxx0  ==========
	CLCSELECT = 1;      // CLC2 select

	CLCnSEL0 = 1;		// IOWR
	CLCnSEL1 = 2;		// A7
	CLCnSEL2 = 3;		// A6
	CLCnSEL3 = 7;		// A0

	CLCnGLS0 = 0x01;	// IOWR invert
	CLCnGLS1 = 0x04;	// A7 invert
	CLCnGLS2 = 0x10;	// A6 invert
	CLCnGLS3 = 0x40;	// A0 invert

	CLCnPOL = 0x80;		// Q invert
	CLCnCON = 0x82;		// 4 input AND
// ========== CLC3 IO decode 000xxxx0  ==========
	CLCSELECT = 2;      // CLC5 select

	CLCnSEL0 = 51;		// CLC1
	CLCnSEL1 = 52;		// CLC2
	CLCnSEL2 = 127;		//
	CLCnSEL3 = 127;		//

	CLCnGLS0 = 0x02;	// CLC1 no invert
	CLCnGLS1 = 0x08;	// CLC2 no invert
	CLCnGLS2 = 0x10;	// 1(0 invert)
	CLCnGLS3 = 0x40;	// 1(0 invert)

	CLCnPOL = 0x00;		// no invert
	CLCnCON = 0x82;		// 4 input AND
// ========== CLC5 path through RC6->RA2 ==========
	CLCSELECT = 4;      // CLC5 select

	CLCnSEL0 = 5;		// CLCIN5PPS <- RC6 <- HOLDA
	CLCnSEL1 = 127;		// NC
	CLCnSEL2 = 127;		// NC
	CLCnSEL3 = 127;		// NC

	CLCnGLS0 = 0x02;	// CLC1 no invert
	CLCnGLS1 = 0x04;	// 1(0invert)
	CLCnGLS2 = 0x10;	// 1(0 invert)
	CLCnGLS3 = 0x40;	// 1(0 invert)

	CLCnPOL = 0x00;		// no invert
	CLCnCON = 0x82;		// 4 input AND

//	RA2PPS = 5;			// BUSCUT <- RA2 <- CLC5 <- RC6
//	TRISA2 = 0;
//	printf("connect HOLDA -> RC6 -> CLC5 -> RA2 -> BUSCUT\n\r");
//================================================
    CLCSELECT = 0;		// Select CLC1

	// CPU start
	LAT(CPU_RESET) = 0;	// SET PC=0 again
	__delay_ms(1);
	LAT(CPU_HOLD) = 0;	// HOLD = L
	LAT(CPU_RESET) = 1;	// Release reset
}

#include "../../drivers/pic18f47q43_spi.c"
#include "../../drivers/SDCard.c"

// main routine
void main(void)
{
	emuz80_57q_sys_init();
	emuz80_57q_wait_for_programmer();
	printf("Board: SBC8080 + EMUZ80-57Q\n\r");
	mem_init();

	if(NCO1EN) {
		printf("Use NCO1->RA3 %.2f MHz for 8085 clock\n\r", ((uint32_t)NCO1INC * 61 / 2) / 1000000.0);
	}
	printf("\n\r");

	emuz80_57q_SD_init();

	if (f_mount(&fs, "0://", 1) != FR_OK) {
		printf("Failed to mount SD Card.\n\r");
		while(1);
	}

	if (menu_select() < 0)
		while (1);

	// Transfer IPL to the SRAM
	emuz80_57q_write_to_sram(0x00000,(uint8_t*)rom, sizeof(rom));
	emuz80_57q_start_cpu();		// Start 8080

	GIE = 0;
	BSR = 0;
	goto IO_wait_loop;

IO_wait_loop0:
	TRIS(DATA_BUS) = 0;			// set as output
	G3POL = 1;					// set ready
	G3POL = 0;
	while(!R(CPU_IORD));		// wait IORD=H
	TRIS(DATA_BUS) = 0xff;		// set as input

IO_wait_loop:
//	while(CLC3OUT);				// waiting IO operation
	
//	if(!CLC2OUT){				// Check IOWR as a priority
//		U3TXB = PORT(DATA_BUS);	// CLC wait did not work for IOWR
//		while(!R(CPU_IOWR));	// No wait operation
//		goto IO_wait_loop;		// wait next IO
//	}

	asm(
	"CLC1_check:			\n"
//	"btfsc	CLCDATA,0,b		\n"	// polling CLC1 = L
	"btfsc	CLCDATA,2,b		\n"	// polling CLC3 = L
	"bra	CLC1_check		\n"	// repeat if it = H
	"btfsc	CLCDATA,1,b		\n"	// CLC2 = 0(OUT 00xxxxx0)?
	"bra	CLC2_skip		\n"	// goto IORD operation
	"movf	PORTF,w,c		\n"	// transmit
//	"movlb	2				\n"	// transmit
//	"movf	PORTF,w,c		\n"	// transmit
//	"movwf	U3TXB,b			\n"
//	"movlb	0				\n"	// transmit
	"CLC2_check:			\n"
	"btfsc	PORTA,1,c		\n"	// IOWR = 1?
	"bra	CLC2_exit		\n" // wait IOWR=H
	"movf	PORTF,w,c		\n"	// transmit
	"bra	CLC2_check		\n" // wait IOWR=H

	"CLC2_exit:				\n"
	"movlb	2				\n"
	"movwf	U3TXB,b			\n"
	"movlb	0				\n"
	"bra	CLC1_check		\n"	// wait next IO
	"CLC2_skip:				\n"
);

	io_addr = PORT(ADDR_BUS_L);

	switch (io_addr) {

	case UART_C8251:			// keep compatible to SBC8080 SUB board
		LAT(DATA_BUS) = U3TXIF + U3RXIF*2;
		goto IO_wait_loop0;

/*		asm(
		"setf	LATF,c			\n"
		"btfss	PIR9,1,c		\n" // U3TXIF = 1?
		"bcf	LATF,0,c		\n" // set status bit0
		"btfss	PIR9,0,c		\n" // U3RXIF = 1?
		"bcf	LATF,1,c		\n"	// set status bit1
		);
		goto IO_wait_loop0;
*/
	case UART_D8251:			// keep compatible to SBC8080 SUB board
		LAT(DATA_BUS) = U3RXB;
		goto IO_wait_loop0;
/*
	case UART_CREG:
		if (U3RXIF) {
			LAT(DATA_BUS) = 0xff;
		} else {
			LAT(DATA_BUS) = 0x00;
		}
		goto IO_wait_loop0;					// let CPU read data and wait next IO

	case UART_DREG:
		LAT(DATA_BUS) = (uint8_t)getch();	// Out the character
		goto IO_wait_loop0;					// let CPU read data and wait next IO

	case CONOUT_BY_IN:
		LAT(CPU_HOLD) = 1;					// set hold
		G3POL = 1;							// set ready
		G3POL = 0;
		emuz80_57q_enter_bus_master();
		putch((char)get_from_sram(BIOSDAT));
		emuz80_57q_exit_bus_master();
		LAT(CPU_HOLD) = 0;					// clear hold
		BSR = 0;
		goto IO_wait_loop;					// wait next IO
*/
	case DISK_REG_FDCST:
		LAT(DATA_BUS) = disk_stat;
		goto IO_wait_loop0;					// let CPU read data and wait next IO

	case DISK_READ:
		LAT(CPU_HOLD) = 1;					// set hold
		G3POL = 1;							// set ready
		G3POL = 0;
		emuz80_57q_enter_bus_master();
		io_handle();
		emuz80_57q_exit_bus_master();
		LAT(CPU_HOLD) = 0;					// clear hold
		BSR = 0;
		goto IO_wait_loop;					// wait next IO

	case DISK_WRITE:
		LAT(CPU_HOLD) = 1;					// set hold
		G3POL = 1;							// set ready
		G3POL = 0;
		emuz80_57q_enter_bus_master();
		io_handle();
		emuz80_57q_exit_bus_master();
		LAT(CPU_HOLD) = 0;					// clear hold
		BSR = 0;
		goto IO_wait_loop;					// wait next IO

	case GET_BDOSCCP:
		LAT(CPU_HOLD) = 1;					// set hold
		G3POL = 1;							// set ready
		G3POL = 0;
		emuz80_57q_enter_bus_master();
		emuz80_57q_write_to_sram(0xe400, (uint8_t*)bdosccp, 0x1600);
		emuz80_57q_exit_bus_master();
		LAT(CPU_HOLD) = 0;					// clear hold
		BSR = 0;
		goto IO_wait_loop;					// wait next IO

	case GET_BIOS:
		LAT(CPU_HOLD) = 1;					// set hold
		G3POL = 1;							// set ready
		G3POL = 0;
		emuz80_57q_enter_bus_master();
		emuz80_57q_write_to_sram(0xfa00, (uint8_t*)bios, sizeof(bios));
		emuz80_57q_exit_bus_master();
		LAT(CPU_HOLD) = 0;					// clear hold
		BSR = 0;
		goto IO_wait_loop;					// wait next IO
	}

	printf("WARNING: unknown I/O read (%02XH)\n\r", io_addr);
	BSR = 0;
	G3POL = 1;								// set ready
	G3POL = 0;
	goto IO_wait_loop;						// wait next IO
}