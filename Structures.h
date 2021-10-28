#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdint.h>
#include <stdbool.h>
//==============================================================
#define FIRMWARE_VERSION					"211021"
//#define DEBUG_VERSION
//==============================================================
#define BLE_BUFF_LEN						128
//==============================================================
#define BOOTLOADER_DFU_START            	0xB1        /**< Magic value written to retention register when starting buttonless DFU. */

#define UART_ADDR_IDX						0

#define SENSOR_START_BYTE					0x7F
#define SENSOR_LEN_IDX						1
#define SENSOR_DATA_IDX						2

#define DONGLE_START_BYTE					0x80
#define DONGLE_LEN_IDX						1
#define DONGLE_COMMAND_IDX					2
#define DONGLE_DATA_IDX						3
#define DONGLE_ERROR_IDX					5
#define DONGLE_XOR_IDX						BLE_BUFF_LEN - 1

#define DONGLE_DFU_START_BYTE				0x8F

#define BLE_DEVICE_LEN							10

#define UART_ERROR_LEN						4

#define BLE_DELAY_RECONNECT					2	// second
#define BLE_DELAY_ERROR						3	// second

#define TIMEOUT_RSSI_CLEAR					10

//==============================================================
//						Bluetooth buffers
//==============================================================
typedef struct
{
	uint8_t buffer[BLE_BUFF_LEN];
	uint16_t len;
}ble_buffer_t;
//==============================================================
typedef union
{
	uint32_t Full;
	struct
	{
		unsigned
//			_ble_scan_state	: 1,
			_ble_con_state	: 1,	// 1
			_ble_con_f		: 1,	// 2
			_ble_discon_f	: 1,	// 3
			_ble_rx_f		: 1,	// 4
			_ble_tx_f		: 1,	// 5
//			_ble_con_res_f	: 1,	// 6
			_uart_rx_f		: 1,	// 7
			_uart_tx_f		: 1,	// 8
			_uart_tx_complete : 1,
			_reset_list_f	: 1,	// 9
//			_send_error_f	: 1,	// 10
			_temperature_f	: 1,	// 11
			_prepare_dfu_f	: 1,	// 12
			_second_f		: 1;
	}Bits;
} FLAGSmy_t;
//==============================================================
//#define BLE_SCAN_STATE	_flags.Bits._ble_scan_state
#define BLE_CON_STATE	_flags.Bits._ble_con_state
#define BLE_CON_F		_flags.Bits._ble_con_f
#define BLE_DISCON_F	_flags.Bits._ble_discon_f
#define BLE_RX_F		_flags.Bits._ble_rx_f
#define BLE_TX_F		_flags.Bits._ble_tx_f
//#define BLE_CON_RES_F	_flags.Bits._ble_con_res_f
#define UART_RX_F		_flags.Bits._uart_rx_f
#define UART_TX_F		_flags.Bits._uart_tx_f
#define UART_TX_COMPLETE		_flags.Bits._uart_tx_complete
#define RESET_LIST_F	_flags.Bits._reset_list_f
//#define SEND_ERROR_F	_flags.Bits._send_error_f
#define TEMPERATURE_F	_flags.Bits._temperature_f
#define PREPARE_DFU_F	_flags.Bits._prepare_dfu_f
#define SECOND_F		_flags.Bits._second_f

//==============================================================
//						Packet counters
//==============================================================
typedef struct
{
	uint16_t rx_counter;
	uint16_t tx_counter;
	uint16_t errors;
}packet_counter_t;
//==============================================================

#endif
