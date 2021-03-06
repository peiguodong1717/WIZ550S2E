/**
 * @file main.c
 * @brief S2E Bootloader Main Source File.
 * @version 0.1.0
 * @author Sang-sik Kim
 */

/* Includes -----------------------------------------------------*/

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>

#include <string.h>
#include "common.h"
#include "TFTP/tftp.h"
#include "sspHandler.h"
#include "flashHandler.h"
#include "socket.h"
#include "netutil.h"
#include "eepromHandler.h"
#include "timerHandler.h"
#include "ConfigData.h"
#include "ConfigMessage.h"
#include "uartHandler.h"

/* define -------------------------------------------------------*/

/* typedef ------------------------------------------------------*/

/* Extern Variable ----------------------------------------------*/

/* Extern Functions ---------------------------------------------*/
extern bool Board_hwtrig_get(void);

/* Global Variable ----------------------------------------------*/
int dbg_level = (INFO_DBG | ERROR_DBG | IPC_DBG);
uint8_t g_socket_rcv_buf[MAX_MTU_SIZE];
uint8_t g_op_mode = NORMAL_MODE;

/* static function define ---------------------------------------*/

/* Functions ----------------------------------------------------*/
void application_jump(void)
{
	//DBG_PRINT(INFO_DBG, "\r\n### Application Start... ###\r\n");

	__disable_irq();

	/* Set Stack Pointer */
	asm volatile("ldr r0, =0x6000");
	asm volatile("ldr r0, [r0]");
	asm volatile("mov sp, r0");

	/* Jump to Application ResetISR */
	asm volatile("ldr r0, =0x6004");
	asm volatile("ldr r0, [r0]");
	asm volatile("mov pc, r0");
}

int application_update(void)
{
	Firmware_Upload_Info firmware_upload_info;
	uint8_t firmup_flag = 0;

	read_eeprom(0, &firmware_upload_info, sizeof(Firmware_Upload_Info));
	if(firmware_upload_info.wiznet_header.stx == STX) {
		firmup_flag = 1;
	}

	if(firmup_flag) {
		uint32_t tftp_server;
		uint8_t *filename;
		int ret;

		//DBG_PRINT(INFO_DBG, "### Application Update... ###\r\n");
		tftp_server = (firmware_upload_info.tftp_info.ip[0] << 24) | (firmware_upload_info.tftp_info.ip[1] << 16) | (firmware_upload_info.tftp_info.ip[2] << 8) | (firmware_upload_info.tftp_info.ip[3]);
		filename = firmware_upload_info.filename;

		TFTP_read_request(tftp_server, filename);

		while(1) {
			ret = TFTP_run();
			if(ret != TFTP_PROGRESS)
				break;
		}

		if(ret == TFTP_SUCCESS) {
			reply_firmware_upload_done(SOCK_CONFIG);

			memset(&firmware_upload_info, 0 ,sizeof(Firmware_Upload_Info));
			write_eeprom(0, &firmware_upload_info, sizeof(Firmware_Upload_Info));
		}

		return ret;
	}

	return 0;
}

int main(void) 
{
	int ret;

    // Read clock settings and update SystemCoreClock variable
    SystemCoreClockUpdate();

    // Set up and initialize all required blocks and
    // functions related to the board hardware
    Board_Init();

    // Set the LED to the state of "On"
    Board_LED_Set(0, true);
    Board_LED_Set(1, false);

	/* Load Configure Infomation */
	load_S2E_Packet_from_eeprom(); 

	/* Check MAC Address */
	check_mac_address();

	UART_Init();
	SPI_Init();
	W5500_Init();
	timer_Init();

	Net_Conf();
	TFTP_init(SOCK_TFTP, g_socket_rcv_buf);

	/* Application Firmware Update Request Process */
	ret = application_update();

	if(Board_hwtrig_get() && (ret != TFTP_FAIL)) {
		uint32_t tmp;

		tmp = *(volatile uint32_t *)APP_BASE;

		if((tmp & 0xffffffff) != 0xffffffff) {
			application_jump();
		}
	}

	while(1) {
		if(g_op_mode == NORMAL_MODE) {
			do_udp_config(SOCK_CONFIG);
		} else {
			if(TFTP_run() != TFTP_PROGRESS)
				g_op_mode = NORMAL_MODE;
		}
	}

    return 0;
}
