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
static DIR fsdir;
static FILINFO fileinfo;
static FIL files[NUM_FILES];
static int num_files = 0;
uint8_t tmp_buf[2][TMP_BUF_SIZE];
debug_t debug = {
    0,  // disk
    0,  // disk_read
    0,  // disk_write
    0,  // disk_verbose
    0,  // disk_mask
};

const unsigned char rom[] = {	// Initial program loader at 0x0000
#include "../../z80/ipl_8080.inc"
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

int menu_select(void)
{
    int i;
    unsigned int drive;
    //
    // Select disk image folder
    //
    if (f_opendir(&fsdir, "/")  != FR_OK) {
        printf("Failed to open SD Card..\n\r");
        return -3;
    }

    i = 0;
    int selection = -1;
    f_rewinddir(&fsdir);
    while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
        if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
            strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
            printf("%d: %s\n\r", i, fileinfo.fname);
            if (strcmp(fileinfo.fname, "CPMDISKS") == 0)
                selection = i;
            i++;
        }
    }

    if (1 < i) {
        printf("Select: ");
        while (1) {
			uint8_t c = (uint8_t)getch();  // Wait for input char
			if ('0' <= c && c <= '9' && c - '0' < i) {
				selection = c - '0';
				break;
			}
			if ((c == 0x0d || c == 0x0a) && 0 <= selection)
				break;
		}
		printf("%d\n\r", selection);
		f_rewinddir(&fsdir);
		i = 0;
		while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
			if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
				strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
				if (selection == i)
					break;
				i++;
			}
		}
		printf("%s is selected.\n\r", fileinfo.fname);
	} else {
		strcpy(fileinfo.fname, "CPMDISKS");
	}
	f_closedir(&fsdir);
	//
	// Open disk images
	//
	for (drive = 0; drive < num_drives && num_files < NUM_FILES; drive++) {
		char drive_letter = (char)('A' + drive);
		char * const buf = (char *)tmp_buf[0];
		sprintf(buf, "%s/DRIVE%c.DSK", fileinfo.fname, drive_letter);
		if (f_open(&files[num_files], buf, FA_READ|FA_WRITE) == FR_OK) {
			printf("Image file %s/DRIVE%c.DSK is assigned to drive %c\n\r",
				fileinfo.fname, drive_letter, drive_letter);
			drives[drive].filep = &files[num_files];
			num_files++;
		}
	}
	if (drives[0].filep == NULL) {
		printf("No boot disk.\n\r");
		return -4;
	}
return 0;
}

void mem_init()
{
    unsigned int i;
    uint32_t addr;

    // RAM check
    for (i = 0; i < TMP_BUF_SIZE; i += 2) {
        tmp_buf[0][i + 0] = 0xa5;
        tmp_buf[0][i + 1] = 0x5a;
    }
    for (addr = 0; addr < MAX_MEM_SIZE; addr += MEM_CHECK_UNIT) {
        printf("Memory 000000 - %06lXH\r", addr);
        tmp_buf[0][0] = (addr >>  0) & 0xff;
        tmp_buf[0][1] = (addr >>  8) & 0xff;
        tmp_buf[0][2] = (addr >> 16) & 0xff;
        board_write_to_sram((uint16_t)addr, tmp_buf[0], TMP_BUF_SIZE);
        board_read_from_sram((uint16_t)addr, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) != 0) {
            break;
        }
        if (addr == 0)
            continue;
        board_read_from_sram(0, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) == 0) {
            // the page at addr is the same as the first page
            break;
        }
    }

	printf("Memory 000000 - %06lXH %d KB OK\r\n", addr, (int)(addr / 1024));
}

static void emuz80_57q_bus_master(int enable)
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

static void emuz80_57q_set_ready_pin(uint8_t v)
{
	if (v == 1) {
		// Release wait (D-FF reset)
		G3POL = 1;
		G3POL = 0;
	} else {
		// not implemented
	}
}

uint8_t get_from_sram(uint16_t addr)
{
	union address_bus_u ab;
	uint8_t	ret_val;

	ab.w = addr;
	LAT(ADDR_BUS_H) = ab.h;
	LAT(ADDR_BUS_L) = ab.l;
	LAT(SRAM_OE) = 0;
	NOP();
	ret_val =  PORT(DATA_BUS);
	LAT(SRAM_OE) = 1;
	return ret_val;
}

static void emuz80_common_write_to_sram(uint16_t addr, uint8_t *buf, unsigned int len)
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

static void emuz80_common_read_from_sram(uint16_t addr, uint8_t *buf, unsigned int len)
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

static uint8_t emuz80_common_addr_l_pins(void) { return PORT(ADDR_BUS_L); }
static void emuz80_common_set_data_pins(uint8_t v) { LAT(DATA_BUS) = v; }
static void emuz80_common_set_data_dir(uint8_t v) { TRIS(DATA_BUS) = v; }
static __bit emuz80_common_iord_pin(void) { return R(CPU_IORD); }
static void emuz80_common_set_hold_pin(uint8_t v) { LAT(CPU_HOLD) = (__bit)(v & 0x01); }
static void emuz80_common_set_ready_pin(uint8_t v) { LAT(CPU_READY) = (__bit)(v & 0x01); }

static void emuz80_common_wait_for_programmer()
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

	RA2PPS = 0;

	// RESET output pin
	LAT(CPU_RESET) = 0;         // Reset
	TRIS(CPU_RESET) = 0;        // Set as output

	// /BUSREQ output pin
	LAT(CPU_HOLD) = 1;         // BUS request
	TRIS(CPU_HOLD) = 0;        // Set as output

    // 8085 clock
#define CPU_CLK_NCO
#ifdef CPU_CLK_NCO
    PPS(CPU_CLK) = 0x3f;		// asign NCO1
    TRIS(CPU_CLK) = 0;			// NCO output pin
//	NCO1INC = 0x10000;			// 2MHz	
	NCO1INC = 0x20000;			// 4MHz	
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
    PPS(CPU_CLK) = 0;           // select LATxy
    TRIS(CPU_CLK) = 1;          // set as input
	WPU(CPU_CLK) = 1;
    NCO1OUT = 0;                // NCO output disable
    NCO1EN = 0;                 // NCO disable
#endif

	// no use GPIO
	TRISC3 = 1;
	WPUC3 = 1;
	TRISC7 = 1;
	WPUC7 = 1;

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

	RA2PPS = 5;			// BUSCUT <- RA2 <- CLC5 <- RC6
	TRISA2 = 0;
//================================================
    CLCSELECT = 0;		// Select CLC1

	// CPU start
	LAT(CPU_HOLD) = 0;	// HOLD = L
	LAT(CPU_RESET) = 1;	// Release reset
}

void board_init()
{
	board_bus_master_hook		= emuz80_57q_bus_master;
	board_write_to_sram_hook	= emuz80_common_write_to_sram;
	board_read_from_sram_hook	= emuz80_common_read_from_sram;
	board_addr_l_pins_hook		= emuz80_common_addr_l_pins;
	board_set_data_pins_hook	= emuz80_common_set_data_pins;
	board_set_data_dir_hook		= emuz80_common_set_data_dir;
	board_iord_pin_hook			= emuz80_common_iord_pin;
	board_set_hold_pin_hook		= emuz80_common_set_hold_pin;
	board_set_ready_pin_hook	= emuz80_57q_set_ready_pin;
}

#include "../../drivers/pic18f47q43_spi.c"
#include "../../drivers/SDCard.c"

// main routine
void main(void)
{
	emuz80_57q_sys_init();
	emuz80_common_wait_for_programmer();
	printf("Board: SBC8080 + EMUZ80-57Q\n\r");
	board_init();
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
	board_write_to_sram(0x00000,(uint8_t*)rom, sizeof(rom));
	emuz80_57q_start_cpu();		// Start 8080

	while(1) {
		while (CLC1OUT);		// Wait for IO access
		io_handle();
    }
}
