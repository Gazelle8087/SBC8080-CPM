/*
 * Copyright (c) 2024 by Gazelle
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
 *
 * This software is modified by Gazelle based on Hanyazou san's products.
 *	https://github.com/hanyazou/SuperMEZ80
 *
 * 2024/7/28 first release https://github.com/Gazelle8087/SBC8080-CPM
 */
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
 * UART, disk I/O and monitor firmware for SuperMEZ80-SPI
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

#include "../src/supermez80.h"
#include <stdio.h>
#include <string.h>
#include "../drivers/SDCard.h"
#include "../drivers/SPI.h"
#include "../drivers/utils.h"
#include "../fatfs/ff.h"

drive_t drives[] = {
    { 26 },
    { 26 },
    { 26 },
    { 26 },
    { 0 },
    { 0 },
    { 0 },
    { 0 },
    { 128 },
    { 128 },
    { 128 },
    { 128 },
    { 0 },
    { 0 },
    { 0 },
    { 16484 },
};
const int num_drives = (sizeof(drives)/sizeof(*drives));

static uint8_t disk_buf[SECTOR_SIZE];
uint8_t disk_stat = DISK_ST_ERROR;

//static FATFS fs;
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
//================================================================================
// UART3 Transmit
void putch(char c) {
	while(!U3TXIF);             // Wait or Tx interrupt flag set
	U3TXB = c;                  // Write data
}
//================================================================================
// UART3 Recive
int getch(void) {
	while(!U3RXIF);             // Wait for Rx interrupt flag set
	return U3RXB;               // Read data
}
//================================================================================
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
//================================================================================
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
        emuz80_57q_write_to_sram((uint16_t)addr, tmp_buf[0], TMP_BUF_SIZE);
        emuz80_57q_read_from_sram((uint16_t)addr, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) != 0) {
            break;
        }
        if (addr == 0)
            continue;
        emuz80_57q_read_from_sram(0, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) == 0) {
            // the page at addr is the same as the first page
            break;
        }
    }

	printf("Memory 000000 - %06lXH %d KB OK\r\n", addr, (int)(addr / 1024));
}
//================================================================================
void wait_for_programmer()
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
//================================================================================
void SD_init()
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
//================================================================================
void disk_io_handle() {
	static uint8_t disk_drive = 0;
	static uint8_t disk_track = 0;
	static uint16_t disk_sector = 0;
	static uint8_t disk_dmal = 0;
	static uint8_t disk_dmah = 0;
	static uint8_t *disk_datap = NULL;

	// Do disk I/O
	uint32_t sector = 0;
	disk_drive = emuz80_57q_get_from_sram(DRV_NO);	
   	disk_track = emuz80_57q_get_from_sram(TRK_NO);
	disk_sector =  (disk_sector & 0xff00) | emuz80_57q_get_from_sram(SEC_NO);
	disk_dmal = emuz80_57q_get_from_sram(DMA_L);
	disk_dmah = emuz80_57q_get_from_sram(DMA_H);
	
	if (num_drives <= disk_drive || drives[disk_drive].filep == NULL) {
		printf("select ERROR %d\n\r", disk_drive);
		disk_stat = DISK_ST_ERROR;	
		goto disk_io_done;
	}

	sector = disk_track * drives[disk_drive].sectors + disk_sector - 1;
	FIL *filep = drives[disk_drive].filep;
	unsigned int n;
	FRESULT fres;
	if ((fres = f_lseek(filep, sector * SECTOR_SIZE)) != FR_OK) {
		printf("f_lseek(): ERROR %d\n\r", fres);
		disk_stat = DISK_ST_ERROR;
		goto disk_io_done;
	}

	if (io_addr == DISK_READ) {
		// DISK read
		// read from the DISK
		if ((fres = f_read(filep, disk_buf, SECTOR_SIZE, &n)) != FR_OK || n != SECTOR_SIZE) {
			printf("f_read(): ERROR res=%d, n=%d\n\r", fres, n);
			disk_stat = DISK_ST_ERROR;
			goto disk_io_done;
		}
		// Store disk I/O status here so that io_invoke_target_cpu() can return the status in it
		disk_stat = DISK_ST_SUCCESS;
		// transfer read data to SRAM
		uint16_t addr = ((uint16_t)disk_dmah << 8) | disk_dmal;
		emuz80_57q_write_to_sram(addr, (uint8_t*)disk_buf, SECTOR_SIZE);
		disk_datap = NULL;

	} else
	if (io_addr == DISK_WRITE) {
		// DISK write
		// transfer write data from SRAM to the buffer
		uint16_t addr = ((uint16_t)disk_dmah << 8) | disk_dmal;
		emuz80_57q_read_from_sram(addr, (uint8_t*)disk_buf, SECTOR_SIZE);

		// write buffer to the DISK
		if ((fres = f_write(filep, disk_buf, SECTOR_SIZE, &n)) != FR_OK || n != SECTOR_SIZE) {
			printf("f_write(): ERROR res=%d, n=%d\n\r", fres, n);
			disk_stat = DISK_ST_ERROR;
			goto disk_io_done;
		}
		if ((fres = f_sync(filep)) != FR_OK) {
			printf("f_sync(): ERROR %d\n\r", fres);
			disk_stat = DISK_ST_ERROR;
			goto disk_io_done;
		}
		disk_stat = DISK_ST_SUCCESS;
		disk_datap = NULL;
	}
	disk_io_done:
	return;
}
