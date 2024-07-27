/*
 * UART, disk I/O and monitor firmware for SuperMEZ80-SPI
 *
 * Based on main.c by Tetsuya Suzuki and emuz80_z80ram.c by Satoshi Okue
 * Modified by @hanyazou https://twitter.com/hanyazou
 */
#ifndef __SUPERMEZ80_H__
#define __SUPERMEZ80_H__

#include "../src/picconfig.h"
#include <xc.h>
#include <stdint.h>
#include "../fatfs/ff.h"
#include "../drivers/utils.h"

#define NUM_FILES        6
#define SECTOR_SIZE      128
#define TMP_BUF_SIZE     256

#define MEM_CHECK_UNIT   TMP_BUF_SIZE * 16 // 2 KB
#define MAX_MEM_SIZE     0x00100000        // 1 MB

//
// Constant value definitions
//
#define	PIC_IOBASE			0x40	
#define GET_BDOSCCP			PIC_IOBASE+6	// 06h LOAD BDOS and CCP from PIC ROM
#define GET_BIOS			PIC_IOBASE+7	// 07h LOAD BIOS from PIC ROM
#define DISK_REG_FDCST		PIC_IOBASE+14	// OEh fdc-port: status
#define DISK_READ			PIC_IOBASE+18	// disk read command
#define DISK_WRITE			PIC_IOBASE+19	// disk write command

#define	UART_D8251			0x00
#define UART_C8251			0x01

#define DISK_ST_SUCCESS		0x00
#define DISK_ST_ERROR		0x01

#define	DRV_NO				0xfb20
#define	TRK_NO				0xfb21
#define	SEC_NO				0xfb22
#define	DMA_L				0xfb23
#define	DMA_H				0xfb24
#define	DISK_OP				0xfb25
#define	BIOSDAT				0xfb26

// Address Bus
union address_bus_u {
	unsigned int w;             // 16 bits Address
	struct {
		unsigned char l;        // Address low
		unsigned char h;        // Address high
	};
};

typedef struct {
	unsigned int sectors;
	FIL *filep;
} drive_t;

typedef struct {
	uint8_t disk;
	uint8_t disk_read;
	uint8_t disk_write;
	uint8_t disk_verbose;
	uint16_t disk_mask;
} debug_t;

typedef struct {
	uint8_t *addr;
	uint16_t offs;
	unsigned int len;
} param_block_t;

//
// Global variables and function prototypes
//

extern uint8_t tmp_buf[2][TMP_BUF_SIZE];
extern debug_t debug;

extern int getch(void);
extern drive_t drives[];
extern const int num_drives;
extern void disk_io_handle(void);
extern void mem_init(void);
extern int menu_select(void);
extern uint8_t io_addr;
extern uint8_t disk_stat;
extern void SD_init();
extern void wait_for_programmer();

// board
extern uint8_t emuz80_57q_get_from_sram(uint16_t address);
extern void emuz80_57q_enter_bus_master(void);
extern void emuz80_57q_exit_bus_master(void);
extern void emuz80_57q_write_to_sram(uint16_t addr, uint8_t *buf, unsigned int len);
extern void emuz80_57q_read_from_sram(uint16_t addr, uint8_t *buf, unsigned int len);

#include "chk_borad_dpend.h"

#endif  // __SUPERMEZ80_H__
