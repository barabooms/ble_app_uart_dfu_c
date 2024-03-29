/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "nordic_common.h"
#include "app_error.h"
#include "ble_db_discovery.h"
#include "app_timer.h"
#include "app_util.h"
#include "bsp.h"
#include "ble.h"
#include "ble_gap.h"
#include "ble_hci.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "ble_nus_c.h"
#include "ble_dfu_c.h"
#include "nrf_ble_gatt.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_ble_scan.h"
#include "nrf_delay.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "crc32.h"
#include "version.h"
#include "structures.h"
#include "uart.h"
#include "memory_storage.h"


#define SCAN_INTERVAL             0x00A0                                /**< Determines scan interval in units of 0.625 millisecond. */
#define SCAN_WINDOW               0x0050                                /**< Determines scan window in units of 0.625 millisecond. */
#define SCAN_TIMEOUT              0x000A                                /**< Timout when scanning. 0x0000 disables timeout. */

#define MIN_CONNECTION_INTERVAL   MSEC_TO_UNITS(7.5, UNIT_1_25_MS)      /**< Determines minimum connection interval in milliseconds. */
#define MAX_CONNECTION_INTERVAL   MSEC_TO_UNITS(75, UNIT_1_25_MS)       /**< Determines maximum connection interval in milliseconds. */
#define SLAVE_LATENCY             0                                     /**< Determines slave latency in terms of connection events. */
#define SUPERVISION_TIMEOUT       MSEC_TO_UNITS(4000, UNIT_10_MS)       /**< Determines supervision time-out in units of 10 milliseconds. */


#define APP_BLE_CONN_CFG_TAG    1                                       /**< Tag that refers to the BLE stack configuration set with @ref sd_ble_cfg_set. The default tag is @ref BLE_CONN_CFG_TAG_DEFAULT. */
#define APP_BLE_OBSERVER_PRIO   3                                       /**< BLE observer priority of the application. There is no need to modify this value. */


#define NUS_SERVICE_UUID_TYPE   BLE_UUID_TYPE_VENDOR_BEGIN              /**< UUID type for the Nordic UART Service (vendor specific). */
#define DFU_SERVICE_UUID_TYPE   BLE_UUID_TYPE_BLE


#define MAX_DFU_PKT_LEN                     (20)                                                    /**< Maximum length (in bytes) of the DFU Packet characteristic. */
#define PKT_CREATE_PARAM_LEN                (6)                                                     /**< Length (in bytes) of the parameters for Create Object request. */
#define PKT_SET_PRN_PARAM_LEN               (3)                                                     /**< Length (in bytes) of the parameters for Set Packet Receipt Notification request. */
#define PKT_READ_OBJECT_INFO_PARAM_LEN      (2)                                                     /**< Length (in bytes) of the parameters for Read Object Info request. */
#define MAX_RESPONSE_LEN                    (15)                                                    /**< Maximum length (in bytes) of the response to a Control Point command. */



void send_process_firmware(void);
uint32_t div_up(uint32_t x, uint32_t y);


typedef struct {
	uint32_t
		max_size,
		offset,
		ram_offset,
		flash_offset,
		crc32;
} resp_sel_com_t;
resp_sel_com_t resp_com;
bool resp_sel_com_ok = false;
uint8_t resp_step = 0;

resp_sel_com_t dongle_dfu_ip;
uint8_t* dongle_dfu_ip_ptr = NULL;
bool init_packet_is_sending = false;

resp_sel_com_t dongle_dfu_fi;
uint8_t* dongle_dfu_fi_ptr = NULL;
bool firmware_image_is_sending = false;

#define PRN_LEN		10
uint16_t dfu_prn_counter = PRN_LEN;
uint32_t dfu_data_object_counter = 0;
uint32_t dfu_data_object_len = 0;
uint8_t dfu_process_percent = 0;

#define UART_DEVICE_MAX_LEN		12
typedef struct {
	uint8_t mac[BLE_GAP_ADDR_LEN];
	int8_t rssi;
	uint8_t dfu;
	uint8_t reserved[UART_DEVICE_MAX_LEN - BLE_GAP_ADDR_LEN - 2];
} uart_device_t;

typedef struct {
	bool is_ok;
	bool is_dfu;
	bool is_connected;
	char name[32];
	uint8_t uuid_arr[16];
	uint16_t uuid;
	// ��� ������ � ����� ������� ������� � ����� ��� �������� �� ����, ������ ������ �� �������
	ble_gap_addr_t peer_addr;
	int8_t rssi;
	//----------------------//
	uint16_t conn_handle;
	uint16_t timeout;
} device_t;
device_t device_list[BLE_DEVICE_LEN];
uint8_t device_selected_idx = 0xFF;
uint8_t device_selected_addr[BLE_GAP_ADDR_LEN];
uint8_t device_counter = 0;

const char DEV_NAME[] = "Rstat EASY SMART";
const char DFU_NAME[] = "DFU_RstatEasySmart";

ble_buffer_t ble_rx;
ble_buffer_t ble_tx;

packet_counter_t ble_packets;


#define DATA_TO_SEND_LEN	MAX_DFU_PKT_LEN
nrf_dfu_obj_type_t who_sending = NRF_DFU_OBJ_TYPE_COMMAND;
uint16_t send_data_offset = 0;
uint16_t data_to_send_len = 0;
uint8_t data_to_send[DATA_TO_SEND_LEN] = {0};

APP_TIMER_DEF(m_second_timer_id);
#define SECOND_INTERVAL     APP_TIMER_TICKS(1000)
APP_TIMER_DEF(m_dfu_tx_timer_id);
#define DFU_TX_INTERVAL     APP_TIMER_TICKS(100)

uint8_t dfu_delay_counter = 0;

FLAGSmy_t _flags;
int32_t temperature;

BLE_DFU_C_DEF(m_ble_dfu_c);
BLE_NUS_C_DEF(m_ble_nus_c);                                             /**< BLE Nordic UART Service (NUS) client instance. */
NRF_BLE_GATT_DEF(m_gatt);                                               /**< GATT module instance. */
BLE_DB_DISCOVERY_DEF(m_db_disc);                                        /**< Database discovery module instance. */
NRF_BLE_SCAN_DEF(m_scan);                                               /**< Scanning Module instance. */
NRF_BLE_GQ_DEF(m_ble_gatt_queue,                                        /**< BLE GATT Queue instance. */
			   NRF_SDH_BLE_CENTRAL_LINK_COUNT,
			   NRF_BLE_GQ_QUEUE_SIZE);



static ble_gap_scan_params_t const m_scan_params =
{
    .active        = 0x01,
    .interval      = SCAN_INTERVAL,
    .window        = SCAN_WINDOW,
    .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,//BLE_GAP_SCAN_FP_WHITELIST,
    .timeout       = SCAN_TIMEOUT,
    .scan_phys     = BLE_GAP_PHY_1MBPS,
};


/**@brief NUS UUID. */
static ble_uuid_t const m_nus_uuid = {
	.uuid = BLE_UUID_NUS_SERVICE,
	.type = NUS_SERVICE_UUID_TYPE
};


/**@brief DFU UUID. */
static ble_uuid_t const m_dfu_uuid = {
	.uuid = BLE_UUID_DFU_SERVICE,
	.type = DFU_SERVICE_UUID_TYPE
};






//--------------------------------------------------------------------------
/**
 * @brief Parses advertisement data, providing length and location of the field in case
 *        matching data is found.
 *
 * @param[in]  type       Type of data to be looked for in advertisement data.
 * @param[in]  p_advdata  Advertisement report length and pointer to report.
 * @param[out] p_typedata If data type requested is found in the data report, type data length and
 *                        pointer to data will be populated here.
 *
 * @retval NRF_SUCCESS if the data type is found in the report.
 * @retval NRF_ERROR_NOT_FOUND if the data type could not be found.
 */
static uint32_t adv_report_parse(uint8_t type, uint8_array_t * p_advdata, uint8_array_t * p_typedata)
{
	uint32_t  index = 0;
	uint8_t * p_data;

	p_data = p_advdata->p_data;

	while (index < p_advdata->size) {
		uint8_t field_length = p_data[index];
		uint8_t field_type   = p_data[index + 1];

		if (field_type == type) {
			p_typedata->p_data = &p_data[index + 2];
			p_typedata->size   = field_length - 1;
			return NRF_SUCCESS;
		}
		index += field_length + 1;
	}
	return NRF_ERROR_NOT_FOUND;
}
//--------------------------------------------------------------------------

device_t* get_device(uint8_t idx)
{
	return &device_list[idx];
}
//--------------------------------------------------------------------------

uint8_t get_device_idx(ble_gap_addr_t const * p_gap_addr)
{
	for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++)
		if (get_device(i)->is_ok)
			if (memcmp(&get_device(i)->peer_addr.addr, p_gap_addr->addr, BLE_GAP_ADDR_LEN) == 0)
				return i;
	return 0xFF;
}
//--------------------------------------------------------------------------

uint8_t get_device_mac_idx(uint8_t const * p_addr)
{
	for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++)
		if (get_device(i)->is_ok)
			if (memcmp(&get_device(i)->peer_addr.addr, p_addr, BLE_GAP_ADDR_LEN) == 0)
				return i;
	return 0xFF;
}
//--------------------------------------------------------------------------

bool find_device(scan_evt_t const * p_scan_evt)
{
	// For readibility.
	ble_gap_addr_t const * peer_addr  = &p_scan_evt->params.filter_match.p_adv_report->peer_addr;

	for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++)
		if (device_list[i].is_ok)
			if (memcmp(&device_list[i].peer_addr, peer_addr, sizeof(device_list[i].peer_addr)) == 0)
				return true;
	return false;
}
//--------------------------------------------------------------------------

void add_device(scan_evt_t const * p_scan_evt)
{
	uint32_t      err_code;
	uint8_array_t adv_data;
	uint8_array_t dev_name;
	
	bool found_name = false;
	if (p_scan_evt->params.filter_match.filter_match.name_filter_match) {
		// Prepare advertisement report for parsing.
		adv_data.p_data = (uint8_t *)p_scan_evt->params.filter_match.p_adv_report->data.p_data;
		adv_data.size   = p_scan_evt->params.filter_match.p_adv_report->data.len;
		err_code = adv_report_parse(BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, &adv_data, &dev_name);
		if (err_code != NRF_SUCCESS) {
			// Look for the short local name if it was not found as complete.
			err_code = adv_report_parse(BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, &adv_data, &dev_name);
			if (err_code != NRF_SUCCESS) return;
			else found_name = true;
		}
		else found_name = true;
	}
	
	// For readibility.
	ble_gap_addr_t const * peer_addr  = &p_scan_evt->params.filter_match.p_adv_report->peer_addr;
	if (found_name) {
		for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++) {
			if (!device_list[i].is_ok) {
				device_t* dev = &device_list[i];
				memset(&dev->name, 0x00, sizeof(dev->name));
				memcpy(&dev->name, dev_name.p_data, dev_name.size);
				dev->is_ok = true;
				dev->is_dfu = false;
				dev->rssi = p_scan_evt->params.filter_match.p_adv_report->rssi;
				memcpy(&dev->peer_addr, peer_addr, sizeof(dev->peer_addr));
				dev->conn_handle = BLE_CONN_HANDLE_INVALID;
				device_counter++;
				return;
			}
		}
	}
}
//--------------------------------------------------------------------------

void change_device(scan_evt_t const * p_scan_evt)
{
	uint32_t      err_code;
	uint8_array_t adv_data;
	uint8_array_t dev_name;
	uint8_array_t dev_uuid;

	// For readibility.
	ble_gap_addr_t const * peer_addr  = &p_scan_evt->params.filter_match.p_adv_report->peer_addr;

	// Search for advertising names.
	bool found_name = false;
	if (p_scan_evt->params.filter_match.filter_match.name_filter_match) {
		// Prepare advertisement report for parsing.
		adv_data.p_data = (uint8_t *)p_scan_evt->params.filter_match.p_adv_report->data.p_data;
		adv_data.size   = p_scan_evt->params.filter_match.p_adv_report->data.len;
		err_code = adv_report_parse(BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, &adv_data, &dev_name);
		if (err_code != NRF_SUCCESS) {
			// Look for the short local name if it was not found as complete.
			err_code = adv_report_parse(BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, &adv_data, &dev_name);
			if (err_code != NRF_SUCCESS) return;
			else found_name = true;
		}
		else found_name = true;
	}

	bool found_uuid = false;
	if (p_scan_evt->params.filter_match.filter_match.uuid_filter_match) {
		adv_data.p_data = (uint8_t *)p_scan_evt->params.filter_match.p_adv_report->data.p_data;
		adv_data.size   = p_scan_evt->params.filter_match.p_adv_report->data.len;
		err_code = adv_report_parse(BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE, &adv_data, &dev_uuid);
		if (err_code != NRF_SUCCESS) {
			// Look for the short local name if it was not found as complete.
			err_code = adv_report_parse(BLE_GAP_AD_TYPE_32BIT_SERVICE_UUID_COMPLETE, &adv_data, &dev_uuid);
			if (err_code != NRF_SUCCESS) {
				err_code = adv_report_parse(BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE, &adv_data, &dev_uuid);
				if (err_code != NRF_SUCCESS) {
					err_code = adv_report_parse(BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE, &adv_data, &dev_uuid);
					if (err_code != NRF_SUCCESS) {
						found_uuid = false;
					}
					else found_uuid = true;
				}
				else found_uuid = true;
			}
			else found_uuid = true;
		}
		else found_uuid = true;
	}

	for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++) {
		if (device_list[i].is_ok) {
			if (memcmp(&device_list[i].peer_addr, peer_addr, sizeof(device_list[i].peer_addr)) == 0) {
				device_t* dev = &device_list[i];
				dev->rssi = p_scan_evt->params.filter_match.p_adv_report->rssi;
				dev->timeout = 0;
				if (found_name) {
					memset(&dev->name, 0x00, sizeof(dev->name));
					memcpy(&dev->name, dev_name.p_data, dev_name.size);
				}
				if (found_uuid && dev_uuid.size <= 16) {
					memcpy(&dev->uuid_arr, dev_uuid.p_data, dev_uuid.size);
					if (dev_uuid.size == 16)
						memcpy(&dev->uuid, &dev_uuid.p_data[12], sizeof(dev->uuid));
					else if (dev_uuid.size == 2)
						memcpy(&dev->uuid, dev_uuid.p_data, sizeof(dev->uuid));
					if (dev->uuid == BLE_UUID_DFU_SERVICE)
						dev->is_dfu = true;
					else dev->is_dfu = false;
				}
				dev->conn_handle = BLE_CONN_HANDLE_INVALID;
				return;
			}
		}
	}
}
//--------------------------------------------------------------------------

device_t* get_device_connected(void)
{
	for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++)
		if (device_list[i].is_connected)
			return &device_list[i];
	return NULL;
}
//--------------------------------------------------------------------------

void clear_device_list(void)
{
	memset(get_device(0), 0x00, sizeof(device_list));
	device_counter = 0;
}
//--------------------------------------------------------------------------

void cpy_dev_to_uart_buff(device_t* dev_ptr, uint8_t* uart_ptr)
{
	uart_device_t udev;
	memset(&udev.reserved[0], 0x00, sizeof(udev.reserved));
	memcpy(&udev.mac[0], &dev_ptr->peer_addr.addr[0], BLE_GAP_ADDR_LEN);
	udev.rssi = dev_ptr->rssi;
	udev.dfu = dev_ptr->is_dfu;
	memcpy(uart_ptr, &udev, sizeof(udev));
}
//--------------------------------------------------------------------------






/**@brief Function for handling asserts in the SoftDevice.
 *
 * @details This function is called in case of an assert in the SoftDevice.
 *
 * @warning This handler is only an example and is not meant for the final product. You need to analyze
 *          how your product is supposed to react in case of assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num     Line number of the failing assert call.
 * @param[in] p_file_name  File name of the failing assert call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
	app_error_handler(0xDEADBEEF, line_num, p_file_name);
}


/**@brief Function for handling the Nordic UART Service Client errors.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nus_error_handler(uint32_t nrf_error)
{
	APP_ERROR_HANDLER(nrf_error);
}


static void dfu_error_handler(uint32_t nrf_error)
{
	APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function to start scanning. */
static void scan_start(void)
{
	ret_code_t ret;

	ret = nrf_ble_scan_start(&m_scan);
	APP_ERROR_CHECK(ret);

	ret = bsp_indication_set(BSP_INDICATE_SCANNING);
	APP_ERROR_CHECK(ret);
}


/**@brief Function for handling Scanning Module events.
 */
static void scan_evt_handler(scan_evt_t const * p_scan_evt)
{
	ret_code_t err_code;

	switch(p_scan_evt->scan_evt_id) {
	case NRF_BLE_SCAN_EVT_CONNECTING_ERROR: {
		err_code = p_scan_evt->params.connecting_err.err_code;
		APP_ERROR_CHECK(err_code);
	}
	break;

	case NRF_BLE_SCAN_EVT_CONNECTED: {
		ble_gap_evt_connected_t const * p_connected =
			p_scan_evt->params.connected.p_connected;
		// Scan is automatically stopped by the connection.
		NRF_LOG_INFO("Connecting to target %02x%02x%02x%02x%02x%02x",
					 p_connected->peer_addr.addr[0],
					 p_connected->peer_addr.addr[1],
					 p_connected->peer_addr.addr[2],
					 p_connected->peer_addr.addr[3],
					 p_connected->peer_addr.addr[4],
					 p_connected->peer_addr.addr[5]
					);
	}
	break;

	case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT: {
//		NRF_LOG_INFO("Scan timed out.");
		scan_start();
	}
	break;

	case NRF_BLE_SCAN_EVT_FILTER_MATCH: {
		if (p_scan_evt->params.filter_match.filter_match.name_filter_match) {
			if (!find_device(p_scan_evt))
				add_device(p_scan_evt);
		}
		else if (find_device(p_scan_evt)) 
			change_device(p_scan_evt);
	}
	break;

	default:
		break;
	}
}


/**@brief Function for initializing the scanning and setting the filters.
 */
static void scan_init(void)
{
	ret_code_t          err_code;
	nrf_ble_scan_init_t init_scan;

	memset(&init_scan, 0, sizeof(init_scan));

	init_scan.connect_if_match = false;
	init_scan.conn_cfg_tag     = APP_BLE_CONN_CFG_TAG;
//	init_scan.p_scan_param = &m_scan_params;
//	init_scan.p_conn_param = &m_conn_params;

	err_code = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_UUID_FILTER, &m_nus_uuid);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_UUID_FILTER, &m_dfu_uuid);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_NAME_FILTER, DEV_NAME);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_NAME_FILTER, DFU_NAME);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_UUID_FILTER | NRF_BLE_SCAN_NAME_FILTER, false);
	APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling database discovery events.
 *
 * @details This function is a callback function to handle events from the database discovery module.
 *          Depending on the UUIDs that are discovered, this function forwards the events
 *          to their respective services.
 *
 * @param[in] p_event  Pointer to the database discovery event.
 */
static void db_disc_handler(ble_db_discovery_evt_t * p_evt)
{
	ble_nus_c_on_db_disc_evt(&m_ble_nus_c, p_evt);
	ble_dfu_c_on_db_disc_evt(&m_ble_dfu_c, p_evt);
}


/**@snippet [Handling events from the ble_nus_c module] */
static void ble_nus_c_evt_handler(ble_nus_c_t * p_ble_nus_c, ble_nus_c_evt_t const * p_ble_nus_evt)
{
	ret_code_t err_code;

	switch (p_ble_nus_evt->evt_type) {
	case BLE_NUS_C_EVT_DISCOVERY_COMPLETE: {
		NRF_LOG_INFO("Discovery complete.");
		err_code = ble_nus_c_handles_assign(p_ble_nus_c, p_ble_nus_evt->conn_handle, &p_ble_nus_evt->handles);
		APP_ERROR_CHECK(err_code);

		err_code = ble_nus_c_tx_notif_enable(p_ble_nus_c);
		APP_ERROR_CHECK(err_code);
		NRF_LOG_INFO("Connected to device with Nordic UART Service.");
	}
	break;

	case BLE_NUS_C_EVT_NUS_TX_EVT: {
		memset(&ble_rx.buffer[0], 0x00, sizeof(ble_rx.buffer));
		memcpy(&ble_rx.buffer[0], p_ble_nus_evt->p_data, p_ble_nus_evt->data_len);
		ble_rx.len = p_ble_nus_evt->data_len;
		BLE_RX_F = 1;
		ble_packets.rx_counter++;
	}
	break;

	case BLE_NUS_C_EVT_DISCONNECTED: {
		NRF_LOG_INFO("Disconnected.");
		scan_start();
	}
	break;
	}
}
/**@snippet [Handling events from the ble_nus_c module] */


static void ble_dfu_control_point_received(uint8_t * p_data, uint16_t data_len)
{
//    ret_code_t ret_val;

	NRF_LOG_DEBUG("Receiving dfu data.");
	NRF_LOG_HEXDUMP_DEBUG(p_data, data_len);
	
	if (p_data[0] != BLE_DFU_OP_CODE_RESPONSE) {
		NRF_LOG_ERROR("BLE DFU OP CODE NOT RESPONSE");
		return;
	}

	switch (p_data[1]) {
		case BLE_DFU_OP_CODE_CREATE_OBJECT: {		// 1 - 6
			switch (p_data[2]) {
				case NRF_DFU_RES_CODE_SUCCESS: {
					if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND)
					resp_step = 2;
				else if (who_sending == NRF_DFU_OBJ_TYPE_DATA)
					resp_step = 7;
				} break;
				case NRF_DFU_RES_CODE_INVALID_PARAMETER: {
					if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND)
					resp_step = 0;
				else if (who_sending == NRF_DFU_OBJ_TYPE_DATA)
					resp_step = 5;
				} break;
				default: {
					NRF_LOG_ERROR("BLE DFU op code create object result: %d", p_data[2]);
					resp_step = 0xFF;
				} break;
			}
			/*if (p_data[2] == NRF_DFU_RES_CODE_SUCCESS) {
				if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND)
					resp_step = 2;
				else if (who_sending == NRF_DFU_OBJ_TYPE_DATA)
					resp_step = 7;
			}
			else {
				NRF_LOG_ERROR("BLE DFU op code create object result: %d", p_data[2]);
				resp_step = 0xFF;
			}*/
		} break;
		case BLE_DFU_OP_CODE_EXECUTE_OBJECT: {		// 4 - 9
			if (p_data[2] == NRF_DFU_RES_CODE_SUCCESS) {
				if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND) resp_step = 5;
				else if (who_sending == NRF_DFU_OBJ_TYPE_DATA) {
//					resp_step = 0xFE;
					dfu_data_object_counter++;
					NRF_LOG_DEBUG("Data object (%d/%d) executed", dfu_data_object_counter, dfu_data_object_len);
					if (dfu_data_object_len == 0) {
						resp_step = 0;
						break;
					}
					if (dfu_data_object_counter == dfu_data_object_len) {
						firmware_image_is_sending = true;
						resp_step = 0xFE;
					}
					else resp_step = 6;
				}
				else resp_step = 0xFF;
			}
			else {
				NRF_LOG_ERROR("BLE DFU op code create object result: %d", p_data[2]);
				resp_step = 0xFF;
			}
		} break;
		case BLE_DFU_OP_CODE_SET_RECEIPT_NOTIF: {
			
		} break;
		case BLE_DFU_OP_CODE_CALCULATE_CRC: {		// 3 - 8
			if (p_data[2] == NRF_DFU_RES_CODE_SUCCESS) {
				memcpy(&resp_com.offset, &p_data[3], sizeof(uint32_t));
				memcpy(&resp_com.crc32, &p_data[7], sizeof(uint32_t));
				uint32_t image_crc32_calc = crc32_compute(who_sending == NRF_DFU_OBJ_TYPE_COMMAND ? dongle_dfu_ip_ptr : dongle_dfu_fi_ptr, resp_com.offset, NULL);
				if (resp_com.crc32 == image_crc32_calc) {
					if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND) {
						if (init_packet_is_sending) resp_step = 4;
						else resp_step = 2;
					}
					else if (who_sending == NRF_DFU_OBJ_TYPE_DATA) {
						resp_step = 9;
					}
				}
				else {
					if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND) {
						resp_step = 1;
					}
					else if (who_sending == NRF_DFU_OBJ_TYPE_DATA) {
						resp_step = 6;
					}
//					resp_step = 0xFF;
					NRF_LOG_INFO("CRC32 is BAD  ");
					NRF_LOG_INFO("crc32_calc = 0x%x", image_crc32_calc);
					NRF_LOG_INFO("crc32_get = 0x%x", resp_com.crc32);
				}
			}
		} break;
		case BLE_DFU_OP_CODE_SELECT_OBJECT: {		// 0 - 5
			if (p_data[2] == NRF_DFU_RES_CODE_SUCCESS) {
				memcpy(&resp_com.max_size, &p_data[3], sizeof(uint32_t));
				memcpy(&resp_com.offset, &p_data[7], sizeof(uint32_t));
				memcpy(&resp_com.crc32, &p_data[11], sizeof(uint32_t));
				resp_sel_com_ok = true;
				
				if (who_sending == NRF_DFU_OBJ_TYPE_COMMAND) {
					init_packet_is_sending = false;
					if (resp_com.offset != dongle_dfu_ip.max_size || resp_com.crc32 != dongle_dfu_ip.crc32) {
						uint32_t image_crc32_calc = crc32_compute(dongle_dfu_ip_ptr, resp_com.offset, NULL);
						if (resp_com.offset == 0 || resp_com.offset > dongle_dfu_ip.max_size || resp_com.crc32 != image_crc32_calc) {
							resp_com.offset = 0;
							send_data_offset = 0;
							resp_step = 1;
						}
						else {
							send_data_offset = resp_com.offset;
							resp_step = 2;
						}
					}
					else {
						init_packet_is_sending = true;
						resp_step = 4;
					}
				}
				else if (who_sending == NRF_DFU_OBJ_TYPE_DATA) {
					dfu_data_object_len = div_up(dongle_dfu_fi.max_size, resp_com.max_size);
					firmware_image_is_sending = false;
					if (resp_com.offset != dongle_dfu_fi.max_size || resp_com.crc32 != dongle_dfu_fi.crc32) {
						uint32_t image_crc32_calc = crc32_compute(dongle_dfu_fi_ptr, resp_com.offset, NULL);
						if (resp_com.offset == 0 || resp_com.offset > dongle_dfu_fi.max_size || resp_com.crc32 != image_crc32_calc) {
							dfu_data_object_counter = 0;
							dongle_dfu_fi.flash_offset = resp_com.max_size;
							dongle_dfu_fi.ram_offset = 0;
							resp_com.offset = 0;
							send_data_offset = 0;
							resp_step = 6;
							NRF_LOG_INFO("Create new object");
						}
						else  {
							send_data_offset = resp_com.offset;
							dongle_dfu_fi.flash_offset = resp_com.max_size;
							if (send_data_offset > dongle_dfu_fi.flash_offset)
								dongle_dfu_fi.ram_offset = send_data_offset - ((send_data_offset / dongle_dfu_fi.flash_offset) * dongle_dfu_fi.flash_offset);
							else dongle_dfu_fi.ram_offset = dongle_dfu_fi.flash_offset - send_data_offset;
							dfu_data_object_counter = resp_com.offset / resp_com.max_size;
							if (dfu_data_object_counter > 0 && dongle_dfu_fi.ram_offset == 0) {
								resp_step = 8;
								dfu_data_object_counter--;
							}
							else
								resp_step = 7;
								
							NRF_LOG_INFO("Continue transfer object");
						}
					}
					else {
						resp_step = 9;
						NRF_LOG_INFO("Over transfer object");
					}
				}
				else resp_step = 0xFF;
				
				NRF_LOG_INFO("max size = %d \r\n", resp_com.max_size);
				NRF_LOG_INFO("offset = %d \r\n", resp_com.offset);
				NRF_LOG_INFO("crc32 = 0x%x \r\n", resp_com.crc32);
			}
			else {
				NRF_LOG_ERROR("BLE DFU op code select object result: %d", p_data[2]);
				resp_sel_com_ok = false;
				resp_step = 0xFF;
			}
		} break;
		default: {
			NRF_LOG_WARNING("Received unsupported OP code");
		} break;
	}
}


static void ble_dfu_c_evt_handler(ble_dfu_c_t * p_ble_dfu_c, ble_dfu_c_evt_t const * p_ble_dfu_evt)
{
	ret_code_t err_code;

	switch (p_ble_dfu_evt->evt_type) {
	case BLE_DFU_C_EVT_DISCOVERY_COMPLETE: {
		NRF_LOG_INFO("Discovery complete.");
		err_code = ble_dfu_c_handles_assign(p_ble_dfu_c, p_ble_dfu_evt->conn_handle, &p_ble_dfu_evt->handles);
		APP_ERROR_CHECK(err_code);

		err_code = ble_dfu_c_tx_notif_enable(p_ble_dfu_c);
		APP_ERROR_CHECK(err_code);
		NRF_LOG_INFO("Connected to device with DFU Service.");

		BLE_CON_STATE = true;
	}
	break;

	case BLE_DFU_C_EVT_DFU_TX_EVT: {
		ble_dfu_control_point_received(p_ble_dfu_evt->p_data, p_ble_dfu_evt->data_len);
	}
	break;

	case BLE_DFU_C_EVT_DISCONNECTED: {
		NRF_LOG_INFO("Disconnected.");
		scan_start();
		BLE_CON_STATE = false;
	}
	break;
	}
}


/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
	ret_code_t            err_code;
	ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;
	ble_gap_addr_t const * peer_addr  = &p_gap_evt->params.connected.peer_addr;

	switch (p_ble_evt->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED: {
		BLE_CON_STATE = true;

		err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
		APP_ERROR_CHECK(err_code);

		uint8_t idx = get_device_idx(peer_addr);
		if (idx != 0xFF) {
			device_t* dev = get_device(idx);
			if (dev != NULL) {
				dev->is_connected = true;
				dev->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
				if (dev->is_dfu) {
					err_code = ble_dfu_c_handles_assign(&m_ble_dfu_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
					APP_ERROR_CHECK(err_code);
					dfu_delay_counter = 5;	// 500ms
					APP_ERROR_CHECK(app_timer_start(m_dfu_tx_timer_id, DFU_TX_INTERVAL, NULL));
				}
				else {
					err_code = ble_nus_c_handles_assign(&m_ble_nus_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
					APP_ERROR_CHECK(err_code);
				}
			}
		}

		// start discovery of services. The NUS Client waits for a discovery result
		err_code = ble_db_discovery_start(&m_db_disc, p_ble_evt->evt.gap_evt.conn_handle);
		APP_ERROR_CHECK(err_code);
	}
	break;

	case BLE_GAP_EVT_DISCONNECTED: {
		app_timer_stop(m_dfu_tx_timer_id);
		BLE_CON_STATE = false;
		device_selected_idx = 0xFF;
		uint8_t idx = get_device_idx(peer_addr);
		if (idx != 0xFF) {
			get_device(idx)->is_connected = false;
			get_device(idx)->conn_handle = BLE_CONN_HANDLE_INVALID;
		}

		NRF_LOG_INFO("Disconnected. conn_handle: 0x%x, reason: 0x%x",
					 p_gap_evt->conn_handle,
					 p_gap_evt->params.disconnected.reason);
	}
	break;

	case BLE_GAP_EVT_TIMEOUT: {
		if (p_gap_evt->params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN) {
			NRF_LOG_INFO("Connection Request timed out.");
		}
	}
	break;

	case BLE_GAP_EVT_SEC_PARAMS_REQUEST: {
		// Pairing not supported.
		err_code = sd_ble_gap_sec_params_reply(p_ble_evt->evt.gap_evt.conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
		APP_ERROR_CHECK(err_code);
	}
	break;

	case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST: {
		// Accepting parameters requested by peer.
		err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle,
												&p_gap_evt->params.conn_param_update_request.conn_params);
		APP_ERROR_CHECK(err_code);
	}
	break;

	case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
		NRF_LOG_DEBUG("PHY update request.");
		ble_gap_phys_t const phys = {
			.rx_phys = BLE_GAP_PHY_AUTO,
			.tx_phys = BLE_GAP_PHY_AUTO,
		};
		err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
		APP_ERROR_CHECK(err_code);
	}
	break;

	case BLE_GATTC_EVT_TIMEOUT: {
		// Disconnect on GATT Client timeout event.
		NRF_LOG_DEBUG("GATT Client Timeout.");
		err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
										 BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_CHECK(err_code);
	}
	break;

	case BLE_GATTS_EVT_TIMEOUT: {
		// Disconnect on GATT Server timeout event.
		NRF_LOG_DEBUG("GATT Server Timeout.");
		err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
										 BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_CHECK(err_code);
	}
	break;

	default:
		break;
	}
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
	ret_code_t err_code;

	err_code = nrf_sdh_enable_request();
	APP_ERROR_CHECK(err_code);

	// Configure the BLE stack using the default settings.
	// Fetch the start address of the application RAM.
	uint32_t ram_start = 0;
	err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
	APP_ERROR_CHECK(err_code);

	// Enable BLE stack.
	err_code = nrf_sdh_ble_enable(&ram_start);
	APP_ERROR_CHECK(err_code);

	// Register a handler for BLE events.
	NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}


/**@brief Function for handling events from the GATT library. */
void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt)
{
	if (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)
	{
	    NRF_LOG_INFO("ATT MTU exchange completed.");

	    uint16_t m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
	    NRF_LOG_INFO("Ble NUS max data length set to 0x%X(%d)", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
	}
	NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x",
                  p_gatt->att_mtu_desired_central,
                  p_gatt->att_mtu_desired_periph);
}


/**@brief Function for initializing the GATT library. */
void gatt_init(void)
{
	ret_code_t err_code;

	err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_gatt_att_mtu_central_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
	APP_ERROR_CHECK(err_code);
	
//	err_code = nrf_ble_gatt_data_length_set(&m_gatt, BLE_CONN_HANDLE_INVALID, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
//	APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Nordic UART Service (NUS) client. */
static void nus_c_init(void)
{
	ret_code_t       err_code;
	ble_nus_c_init_t init;

	init.evt_handler   = ble_nus_c_evt_handler;
	init.error_handler = nus_error_handler;
	init.p_gatt_queue  = &m_ble_gatt_queue;

	err_code = ble_nus_c_init(&m_ble_nus_c, &init);
	APP_ERROR_CHECK(err_code);
}


static void dfu_c_init(void)
{
	ret_code_t       err_code;
	ble_dfu_c_init_t init;

	init.evt_handler   = ble_dfu_c_evt_handler;
	init.error_handler = dfu_error_handler;
	init.p_gatt_queue  = &m_ble_gatt_queue;

	m_ble_dfu_c.uuid_type = BLE_UUID_TYPE_BLE;

	err_code = ble_dfu_c_init(&m_ble_dfu_c, &init);
	APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing buttons and leds. */
static void buttons_leds_init(void)
{
	ret_code_t err_code;

	err_code = bsp_init(BSP_INIT_LEDS, NULL);//bsp_event_handler);
	APP_ERROR_CHECK(err_code);
}


static void second_timer_handler(void * p_context)
{
	UNUSED_PARAMETER(p_context);
	SECOND_F = 1;
	/*if (!BLE_CON_STATE) {
		send_data = false;
		if (second_counter++ > 10) {
			second_counter = 0;
			for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++) {
				device_t* dev = get_device(i);
				if (dev->is_dfu) {
					device_selected_idx = i;
					break;
				}
			}
		}
	}
	else
		send_data = true;*/
}


static void dfu_tx_timer_handler(void * p_context)
{
	if (dfu_delay_counter == 0) {
//	send_process_firmware();
	BLE_TX_F = 1;
	}
	else dfu_delay_counter--;
}


/**@brief Function for initializing the timer. */
static void timer_init(void)
{
	ret_code_t err_code = app_timer_init();
	APP_ERROR_CHECK(err_code);

	// Create timers.
	err_code = app_timer_create(&m_second_timer_id,
								APP_TIMER_MODE_REPEATED,
								second_timer_handler);
	APP_ERROR_CHECK(err_code);
	
	err_code = app_timer_create(&m_dfu_tx_timer_id,
								APP_TIMER_MODE_REPEATED,
								dfu_tx_timer_handler);
	APP_ERROR_CHECK(err_code);
	
}


/**@brief Function for initializing the nrf log module. */
static void log_init(void)
{
	ret_code_t err_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CHECK(err_code);

	NRF_LOG_DEFAULT_BACKENDS_INIT();
}


/** @brief Function for initializing the database discovery module. */
static void db_discovery_init(void)
{
	ble_db_discovery_init_t db_init;

	memset(&db_init, 0, sizeof(ble_db_discovery_init_t));

	db_init.evt_handler  = db_disc_handler;
	db_init.p_gatt_queue = &m_ble_gatt_queue;

	ret_code_t err_code = ble_db_discovery_init(&db_init);
	APP_ERROR_CHECK(err_code);
}
//--------------------------------------------------------------------------

void second_f(void)
{
	if (!BLE_CON_STATE) {
		for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++) {
			if (get_device(i)->is_ok) {
				if (get_device(i)->timeout < TIMEOUT_RSSI_CLEAR)
					get_device(i)->timeout++;
				if (get_device(i)->timeout >= TIMEOUT_RSSI_CLEAR) {
					memset(get_device(i), 0x00, sizeof(device_t));
					if (device_counter > 0) device_counter--;
				}
			}
		}
	}
}
//--------------------------------------------------------------------------

void execute_dongle_command(void)
{
	uint8_t com = *(uint8_t*)GetUART_com();			// ������ ������ �������
	uint8_t buff[UART_BUFF_SIZE];					// ��������� ����� �� �������� �� uart
	
	memset(buff, 0x00, sizeof(buff));				// ����� ������
	buff[UART_ADDR_IDX] = *(uint8_t*)GetUART_addr();	// ������ ������ �����������
	buff[DONGLE_COMMAND_IDX] = com;					// ������ ������ �������
	
	switch (com) {
		// ��������� ������ ������� ���������
		case 0xC1: {
			buff[DONGLE_LEN_IDX] = 106;
			for (uint8_t i = 0; i < BLE_DEVICE_LEN; i++) {
				device_t* dev = get_device(i);
				if (dev->is_ok) {
					//memcpy(&buff[DONGLE_DATA_IDX + (i * (BLE_GAP_ADDR_LEN + sizeof(int8_t)))], &dev->peer_addr.addr, BLE_GAP_ADDR_LEN + sizeof(int8_t));
					cpy_dev_to_uart_buff(dev, &buff[DONGLE_DATA_IDX + (i * UART_DEVICE_MAX_LEN)]);
				}
			}
		} break;
		// ������������ � ���������� �� �����
		case 0xC2: {
			memcpy(device_selected_addr, (uint8_t*)GetUART_dat(), sizeof(device_selected_addr));
			memcpy(buff + DONGLE_DATA_IDX, device_selected_addr, sizeof(device_selected_addr));
			buff[DONGLE_DATA_IDX + sizeof(device_selected_addr)] = BLE_CON_STATE;	// �������� ���������� �����������
			buff[DONGLE_LEN_IDX] = sizeof(device_selected_addr) + sizeof(com) + 1;
			// ����������� ������� ���������� � �������� ���������� ������������
			uint8_t idx = get_device_mac_idx(device_selected_addr);
			if (idx != 0xFF) device_selected_idx = idx;
		} break;
		// �������� ������ ��������� � �����������
		case 0xC3: {
			buff[DONGLE_LEN_IDX] = 1;
			device_selected_idx = 0xFF;
			RESET_LIST_F = 1;
		} break;
		// ��������� dongle � ����� ����������
		case 0xCB: {
			buff[DONGLE_LEN_IDX] = 1;
			PREPARE_DFU_F = 1;
		} break;
		// �������� ������ �������� dongle
		case 0xC0: {
			memset(buff + DONGLE_DATA_IDX, VERSION_INT, sizeof(uint32_t));
			buff[DONGLE_LEN_IDX] = sizeof(uint32_t) + sizeof(com);
		} break;
		
		default:
			break;
	}
	// ������� crc ������ ��� ������
	memcpy(&buff[DONGLE_XOR_IDX], calc_crc(buff, UART_BUFF_SIZE - 1), 1);
	// ������� ���������� ������ � uart � ��������
	SetUART_TX(buff);
}
//--------------------------------------------------------------------------

void send_process_firmware(void)
{
	uint8_t buff[UART_BUFF_SIZE] = {0};
	// ������ ������ �����������
	buff[UART_ADDR_IDX] = DONGLE_DFU_START_BYTE;
	// ������ ������ �������
	buff[DONGLE_COMMAND_IDX] = 0xDF;
	// �������� ������
	buff[DONGLE_DATA_IDX] = dfu_process_percent;
	// ��� ����� ������������ ����������
	if (device_selected_idx < BLE_DEVICE_LEN)
		memcpy(&buff[DONGLE_DATA_IDX + sizeof(dfu_process_percent)], get_device(device_selected_idx)->peer_addr.addr, BLE_GAP_ADDR_LEN);;
	// ������ �������� ������, � ��������
	buff[DONGLE_LEN_IDX] = 1 + sizeof(dfu_process_percent) + BLE_GAP_ADDR_LEN;
	// ������� crc ������ ��� ������
	memcpy(&buff[DONGLE_XOR_IDX], calc_crc(buff, UART_BUFF_SIZE - 1), 1);
	// ������� ���������� ������ � uart � ��������
	SetUART_TX(buff);
}
//--------------------------------------------------------------------------

void execute_dongle_dfu_command(void)
{
	// ������ ������ �������
	uint8_t com = *(uint8_t*)GetUART_com();
	// ��������� ����� �� �������� �� uart
	uint8_t buff[UART_BUFF_SIZE] = {0};	
//	memset(buff, 0x00, sizeof(buff));				// ����� ������
	// ������ ������ �����������
	buff[UART_ADDR_IDX] = *(uint8_t*)GetUART_addr();
	// ������ ������ �������
	buff[DONGLE_COMMAND_IDX] = com;
	
	switch (com) {
		// Init Packet
		// �������� ����� ��� Init Packet
		case 0xD1: {
			memset(&dongle_dfu_ip, 0x00, sizeof(dongle_dfu_ip));
			memcpy(&dongle_dfu_ip.max_size, (uint8_t*)GetUART_dat(), sizeof(dongle_dfu_ip.max_size));
			dongle_dfu_ip.crc32 = 0;
			dongle_dfu_ip_ptr = fstorage_get_image_ptr();
			fstorage_clear_flash(FLASH_INIT_PACKET, &dongle_dfu_ip.flash_offset);// clear flash page
			fstorage_clear_flash(FLASH_CONFIG, &dongle_dfu_ip.flash_offset);// clear flash page
//			memset(dongle_dfu_ip_ptr, 0x00, RAM_IMAGE_SIZE);	// test
			buff[DONGLE_LEN_IDX] = sizeof(com);
		} break;
		// �������� ������ Init Packet
		case 0xD2: {
			uint8_t size = *(uint8_t*)GetUART_len() - 1;
			memcpy(dongle_dfu_ip_ptr + dongle_dfu_ip.ram_offset, (uint8_t*)GetUART_dat(), size);	// write to test buff
			dongle_dfu_ip.crc32 = crc32_compute(dongle_dfu_ip_ptr + dongle_dfu_ip.ram_offset, size, &dongle_dfu_ip.crc32);
			dongle_dfu_ip.ram_offset += size;
			dongle_dfu_ip.flash_offset += size;
			if (dongle_dfu_ip.max_size <= dongle_dfu_ip.flash_offset || RAM_IMAGE_SIZE <= dongle_dfu_ip.ram_offset) {
				uint32_t page_offset = (dongle_dfu_ip.flash_offset / dongle_dfu_ip.ram_offset) - 1;
				fstorage_write_flash(FLASH_INIT_PACKET, dongle_dfu_ip_ptr, &dongle_dfu_ip.ram_offset, &page_offset);// write to flash
				dongle_dfu_ip.ram_offset = 0;
			}
			memcpy(&buff[DONGLE_DATA_IDX], &dongle_dfu_ip.flash_offset, sizeof(dongle_dfu_ip.flash_offset));
			memcpy(&buff[DONGLE_DATA_IDX + sizeof(dongle_dfu_ip.flash_offset)], &dongle_dfu_ip.crc32, sizeof(dongle_dfu_ip.crc32));
			buff[DONGLE_LEN_IDX] = 1 + sizeof(dongle_dfu_ip.flash_offset) + sizeof(dongle_dfu_ip.crc32);
		} break;
		// ��������� CRC32 Init Packet
		case 0xDC: {
			memcpy(&buff[DONGLE_DATA_IDX], &dongle_dfu_ip.flash_offset, sizeof(dongle_dfu_ip.flash_offset));
			memcpy(&buff[DONGLE_DATA_IDX + sizeof(dongle_dfu_ip.flash_offset)], &dongle_dfu_ip.crc32, sizeof(dongle_dfu_ip.crc32));
			buff[DONGLE_LEN_IDX] = 1 + sizeof(dongle_dfu_ip.flash_offset) + sizeof(dongle_dfu_ip.crc32);
		} break;
		// Firmware Image
		// �������� ����� ��� Firmware Image
		case 0xF1: {
			memset(&dongle_dfu_fi, 0x00, sizeof(dongle_dfu_fi));
			memcpy(&dongle_dfu_fi.max_size, (uint8_t*)GetUART_dat(), sizeof(dongle_dfu_fi.max_size));
			dongle_dfu_fi.crc32 = 0;
			dongle_dfu_fi_ptr = fstorage_get_image_ptr();
			fstorage_clear_flash(FLASH_FIRMWARE_IMAGE, &dongle_dfu_fi.flash_offset);// clear flash page
//			memset(dongle_dfu_ip_ptr, 0x00, RAM_IMAGE_SIZE);	// test
			buff[DONGLE_LEN_IDX] = sizeof(com);
		} break;
		// �������� ������ Firmware Image
		case 0xF2: {
			uint8_t size = *(uint8_t*)GetUART_len() - 1;
			memcpy(dongle_dfu_fi_ptr + dongle_dfu_fi.ram_offset, (uint8_t*)GetUART_dat(), size);	// write to test buff
			dongle_dfu_fi.crc32 = crc32_compute(dongle_dfu_fi_ptr + dongle_dfu_fi.ram_offset, size, &dongle_dfu_fi.crc32);
			dongle_dfu_fi.ram_offset += size;
			dongle_dfu_fi.flash_offset += size;
			if (dongle_dfu_fi.max_size <= dongle_dfu_fi.flash_offset || RAM_IMAGE_SIZE <= dongle_dfu_fi.ram_offset) {
				uint32_t page_offset = (dongle_dfu_fi.flash_offset - size) / RAM_IMAGE_SIZE;
				fstorage_write_flash(FLASH_FIRMWARE_IMAGE, dongle_dfu_fi_ptr, &dongle_dfu_fi.ram_offset, &page_offset);// write to flash
				dongle_dfu_fi.ram_offset = 0;
				
				if (dongle_dfu_fi.max_size <= dongle_dfu_fi.flash_offset) {
					resp_sel_com_t cfg[2];
					uint32_t cfg_s = sizeof(cfg);
					memcpy(&cfg[0], &dongle_dfu_ip, sizeof(resp_sel_com_t));
					memcpy(&cfg[1], &dongle_dfu_fi, sizeof(resp_sel_com_t));
					fstorage_write_flash(FLASH_CONFIG, (uint8_t*)cfg, &cfg_s, NULL);
				}
			}
			memcpy(&buff[DONGLE_DATA_IDX], &dongle_dfu_fi.flash_offset, sizeof(dongle_dfu_fi.flash_offset));
			memcpy(&buff[DONGLE_DATA_IDX + sizeof(dongle_dfu_fi.flash_offset)], &dongle_dfu_fi.crc32, sizeof(dongle_dfu_fi.crc32));
			buff[DONGLE_LEN_IDX] = 1 + sizeof(dongle_dfu_fi.flash_offset) + sizeof(dongle_dfu_fi.crc32);
		} break;
		// ��������� CRC32 Firmware Image
		case 0xFC: {
			memcpy(&buff[DONGLE_DATA_IDX], &dongle_dfu_fi.flash_offset, sizeof(dongle_dfu_fi.flash_offset));
			memcpy(&buff[DONGLE_DATA_IDX + sizeof(dongle_dfu_fi.flash_offset)], &dongle_dfu_fi.crc32, sizeof(dongle_dfu_fi.crc32));
			buff[DONGLE_LEN_IDX] = 1 + sizeof(dongle_dfu_fi.flash_offset) + sizeof(dongle_dfu_fi.crc32);
		} break;
		// �������� ������� �������� ��������
		case 0xDF: {
			send_process_firmware();
			return;
		} //break;
		default:
			break;
	}
	// ������� crc ������ ��� ������
	memcpy(&buff[DONGLE_XOR_IDX], calc_crc(buff, UART_BUFF_SIZE - 1), 1);
	// ������� ���������� ������ � uart � ��������
	SetUART_TX(buff);
}
//--------------------------------------------------------------------------

void uart_rx_f()
{
	// �������� ������� ��� dongle
	if (*(uint8_t*)GetUART_addr() == DONGLE_START_BYTE && GetUART_data_len() > 0) {
		execute_dongle_command();	// ���������� �������
	}
	// �������� ������� ��� dongi dfu
	else if (*(uint8_t*)GetUART_addr() == DONGLE_DFU_START_BYTE && GetUART_data_len() > 0) {
		execute_dongle_dfu_command();
	}
	// �������� ������� ��� smart
	else if (*(uint8_t*)GetUART_addr() == SENSOR_START_BYTE && GetUART_data_len() > 0) {
		BLE_TX_F = 1;		// �������� ��������� ���� ��� �������, ������������� ��� ����
		memset(&ble_tx.buffer[0], 0x00, sizeof(ble_tx.buffer));
		ble_tx.len = GetUART_data_len();
		memcpy(&ble_tx.buffer[0], (uint8_t*)GetUART_RX() + SENSOR_DATA_IDX, ble_tx.len);
	}
	else set_error(UART_ERROR_E4);
}
//--------------------------------------------------------------------------

uint32_t div_up(uint32_t x, uint32_t y)
{
    return (x - 1) / y + 1;
}

//--------------------------------------------------------------------------
device_t* dev_ptr;
void flag_handler(void)
{
	uint32_t ret_val = NRF_SUCCESS;
	
	// ��������� ������
	if (SECOND_F) {
		SECOND_F = 0;
		second_f();
	}
	// ���� ����������� �� ���������� �� �������
	if (BLE_DISCON_F) {
		BLE_DISCON_F = 0;											// ����� �����
		device_selected_idx = 0xFF;									// ����� ������� ���������� ���������� ��� �����������, ��� �� �����������
	}
	// ������ ��������� �� uart
	if (UART_RX_F) {
		UART_RX_F = 0;
		uart_rx_f();
	}
	// �������� ��������� �� uart, ���� ��������� �� ��������� ��������
	if (UART_TX_F) {
		tx_message();
	}
	// ������ ��������� �� ������
	if (BLE_RX_F) {
		BLE_RX_F = 0;
		// ��������� ����� �� �������� �� uart
		uint8_t buff[UART_BUFF_SIZE] = {0};
		memset(&buff[UART_ADDR_IDX], SENSOR_START_BYTE, 1);
		memcpy(&buff[SENSOR_LEN_IDX], &ble_rx.len, 1);
		memcpy(&buff[SENSOR_DATA_IDX], &ble_rx.buffer[0], ble_rx.len);
		memcpy(&buff[DONGLE_XOR_IDX], calc_crc(buff, UART_BUFF_SIZE - 1), 1);
		// ������� ���������� ������ � uart � ��������
		SetUART_TX(buff);
	}
	// �������� ����������� �����������
	if (TEMPERATURE_F) {
		TEMPERATURE_F = 0;
		sd_temp_get(&temperature);
	}
	// ��������� ��������� �������� ��������� �� ����
	if (UART_TX_COMPLETE) {
		UART_TX_COMPLETE = 0;
		if (PREPARE_DFU_F) {
			PREPARE_DFU_F = 0;
			nrf_delay_ms(1000);
			NRF_POWER->GPREGRET = BOOTLOADER_DFU_START;
			NVIC_SystemReset();
		}
	}
	// ���� ��� ����������� � ���������� ���
	if (!BLE_CON_STATE) {
		// ���� ������� ���������� ��� �����������
		if (device_selected_idx != 0xFF) {
			// �������� ������� ���������� ���������� ��� �����������
			if (device_selected_idx < BLE_DEVICE_LEN) {
				dev_ptr = get_device(device_selected_idx);
				// �������� ���������� ���������� ��� �����������
				if (dev_ptr != NULL && dev_ptr->is_ok) {
					// Stop scanning.
//					nrf_ble_scan_stop();
					ret_val = sd_ble_gap_connect(&dev_ptr->peer_addr, &m_scan.scan_params, &m_scan.conn_params, m_scan.conn_cfg_tag);
					if (ret_val != NRF_SUCCESS) {
						NRF_LOG_ERROR("Connection Request Failed, reason %d", ret_val);
					}
				}
			}
		}
		// ���� ���� ��������� ������ �� ������������ ���������� ���
		if (BLE_TX_F) {
			BLE_TX_F = 0;
			set_error(UART_ERROR_E5);
		}
	}
	// ���� ���� ����������� � ���������� ���
	else {
		// ���� ������� ������ ������������� ����������, �����������
		if (device_selected_idx == 0xFF) {
			// �������� ������������ ����������
			dev_ptr = get_device_connected();
			if (dev_ptr != NULL && dev_ptr->is_ok) {
				// ����������� �� ���������� ���
				ret_val = sd_ble_gap_disconnect(dev_ptr->conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);	// ����������� �� ����������
				if (ret_val != NRF_ERROR_INVALID_STATE) {
					APP_ERROR_CHECK(ret_val);	// �������� �� ������
				}
			}
		}
		// ���� ������ ������������� ���������� � �������
		else {
			// ���� ���� ��������� ������ �� ������������ ���������� ���
			if (BLE_TX_F) {
				BLE_TX_F = 0;
				device_t* dev = get_device_connected();
				if (dev->is_dfu)
					NRF_LOG_DEBUG("Step: %d", resp_step);
				do {
					// ���� ��������� � ���������� ��� ��� ���������� ��������
					if (dev->is_dfu) {
						switch (resp_step) {
							//---------------------------//
							// Transfer of a Init Packet //
							//---------------------------//
							// Select command
							case 0: {
								dfu_process_percent = 0;
								who_sending = NRF_DFU_OBJ_TYPE_COMMAND;
								dongle_dfu_ip.ram_offset = 0;
								dongle_dfu_ip.flash_offset = 0;
								dongle_dfu_ip_ptr = fstorage_init_packet_ptr();
								data_to_send[0] = BLE_DFU_OP_CODE_SELECT_OBJECT;
								data_to_send[1] = NRF_DFU_OBJ_TYPE_COMMAND;
								data_to_send_len = PKT_READ_OBJECT_INFO_PARAM_LEN;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Create command
							case 1: {
								dfu_delay_counter = 4;	// 400ms
								uint32_t size = dongle_dfu_ip.max_size - resp_com.offset;
								data_to_send[0] = BLE_DFU_OP_CODE_CREATE_OBJECT;
								data_to_send[1] = NRF_DFU_OBJ_TYPE_COMMAND;
								memcpy(&data_to_send[2], &size, sizeof(uint32_t));
								data_to_send_len = PKT_CREATE_PARAM_LEN;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Transfer data
							case 2: {
								uint32_t size = (dongle_dfu_ip.max_size - send_data_offset < sizeof(data_to_send)) ? dongle_dfu_ip.max_size - send_data_offset : sizeof(data_to_send);
								memcpy(data_to_send, &dongle_dfu_ip_ptr[send_data_offset], size);
								send_data_offset += size;
								if (send_data_offset >= dongle_dfu_ip.max_size) {
//									dfu_delay_counter = 1;	// 100ms
									init_packet_is_sending = true;
									resp_step = 3;
								}
								dfu_process_percent = (send_data_offset * 100) / (dongle_dfu_ip.max_size + dongle_dfu_fi.max_size);
								data_to_send_len = size;
								ret_val = ble_dfu_packet_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Calculate CRC Success
							case 3: {
								data_to_send[0] = BLE_DFU_OP_CODE_CALCULATE_CRC;
								data_to_send_len = 1;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Execute command
							case 4: {
								dfu_delay_counter = 1;	// 100ms
								data_to_send[0] = BLE_DFU_OP_CODE_EXECUTE_OBJECT;
								data_to_send_len = 1;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							//------------------------------//
							// Transfer of a firmware image //
							//------------------------------//
							// Select command
							case 5: {
								who_sending = NRF_DFU_OBJ_TYPE_DATA;
								dongle_dfu_fi.ram_offset = 0;
								dongle_dfu_fi.flash_offset = 0;
								dongle_dfu_fi_ptr = fstorage_firmware_image_ptr();
								data_to_send[0] = BLE_DFU_OP_CODE_SELECT_OBJECT;
								data_to_send[1] = NRF_DFU_OBJ_TYPE_DATA;
								data_to_send_len = PKT_READ_OBJECT_INFO_PARAM_LEN;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Create command
							case 6: {
								dfu_delay_counter = 4;	// 400ms
								uint32_t size = dongle_dfu_fi.max_size - send_data_offset < resp_com.max_size ? dongle_dfu_fi.max_size - send_data_offset : resp_com.max_size;
								if (dongle_dfu_fi.max_size - send_data_offset < resp_com.max_size)
									NRF_LOG_INFO("Last object created");
								NRF_LOG_INFO("Create command, size = %d \r\n", size);
								data_to_send[0] = BLE_DFU_OP_CODE_CREATE_OBJECT;
								data_to_send[1] = NRF_DFU_OBJ_TYPE_DATA;
								memcpy(&data_to_send[2], &size, sizeof(uint32_t));
								data_to_send_len = PKT_CREATE_PARAM_LEN;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Transfer data
							case 7: {
								uint32_t size = (dongle_dfu_fi.max_size - send_data_offset < sizeof(data_to_send)) ? dongle_dfu_fi.max_size - send_data_offset : sizeof(data_to_send);
								if (size > (dongle_dfu_fi.flash_offset - dongle_dfu_fi.ram_offset))
									size = dongle_dfu_fi.flash_offset - dongle_dfu_fi.ram_offset;
								memcpy(data_to_send, &dongle_dfu_fi_ptr[send_data_offset], size);
								send_data_offset += size;
								dongle_dfu_fi.ram_offset += size;
								NRF_LOG_DEBUG("Full: 0x%x / 0x%x", send_data_offset, dongle_dfu_fi.max_size);
								NRF_LOG_DEBUG("Page: 0x%x / 0x%x", dongle_dfu_fi.ram_offset, dongle_dfu_fi.flash_offset);
								if (send_data_offset >= dongle_dfu_fi.max_size || dongle_dfu_fi.ram_offset >= dongle_dfu_fi.flash_offset) {
									dongle_dfu_fi.ram_offset = 0;
//									dfu_delay_counter = 1;	// 100ms
									resp_step = 8;
								}
								dfu_process_percent = ((send_data_offset + dongle_dfu_ip.max_size) * 100) / (dongle_dfu_ip.max_size + dongle_dfu_fi.max_size);
								data_to_send_len = size;
								ret_val = ble_dfu_packet_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Calculate CRC Success
							case 8: {
								data_to_send[0] = BLE_DFU_OP_CODE_CALCULATE_CRC;
								data_to_send_len = 1;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							// Execute command
							case 9: {
								dfu_delay_counter = 1;	// 100ms
								data_to_send[0] = BLE_DFU_OP_CODE_EXECUTE_OBJECT;
								data_to_send_len = 1;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							//-----------------//
							// 		Test       //
							//-----------------//
							case 0xA0: {
								data_to_send[0] = BLE_DFU_OP_CODE_SET_RECEIPT_NOTIF;
								uint16_t PRN = PRN_LEN;
								memcpy(&data_to_send[1], &PRN, sizeof(uint16_t));
								data_to_send_len = 3;
								ret_val = ble_dfu_control_point_send(&m_ble_dfu_c, data_to_send, data_to_send_len);
							} break;
							//-----------------//
							// Transfer Result //
							//-----------------//
							// Bootloader is over, Happy End
							case 0xFE: {
								app_timer_stop(m_dfu_tx_timer_id);
								dfu_process_percent = 100;
								resp_step = 0;
								memset(&resp_com, 0x00, sizeof(resp_com));
							} break;
							// ERROR
							case 0xFF: {
								app_timer_stop(m_dfu_tx_timer_id);
								device_selected_idx = 0xFF;
								memset(GetUART_addr(), SENSOR_START_BYTE, 1);
								memset(GetUART_com(), 0x70, 1);
								set_error(UART_ERROR_E5);
							} break;
						}
					}
					// ���� ��������� � ���������� ��� ��� ������ �������
					else {
						ret_val = ble_nus_c_string_send(&m_ble_nus_c, &ble_tx.buffer[0], ble_tx.len);
					}
					/*if ( (ret_val != NRF_ERROR_INVALID_STATE) && (ret_val != NRF_ERROR_RESOURCES) ) {
						APP_ERROR_CHECK(ret_val);
					}*/
				}
				while (ret_val == NRF_ERROR_RESOURCES);
				if (dev->is_dfu){
					if (resp_step < 0xFE) {
						NRF_LOG_DEBUG("Ready to send data");
						NRF_LOG_HEXDUMP_DEBUG(data_to_send, data_to_send_len);
					}
				}
				else {
					NRF_LOG_DEBUG("Ready to send data");
					NRF_LOG_HEXDUMP_DEBUG(ble_tx.buffer, ble_tx.len);
				}
				ble_packets.tx_counter++;
			}
		}
	}
	if (RESET_LIST_F) {
		RESET_LIST_F = 0;
		clear_device_list();
		scan_start();
	}
}


static void application_timers_start(void)
{
	ret_code_t err_code;

	// Start application timers.
	err_code = app_timer_start(m_second_timer_id, SECOND_INTERVAL, NULL);
	APP_ERROR_CHECK(err_code);
//	err_code = app_timer_start(m_dfu_tx_timer_id, DFU_TX_INTERVAL, NULL);
//	APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling the idle state (main loop).
 *
 * @details Handles any pending log operations, then sleeps until the next event occurs.
 */
static void idle_state_handle(void)
{
	if (NRF_LOG_PROCESS() == false) {
//        nrf_pwr_mgmt_run();
		flag_handler();
	}
}


int main(void)
{
	uint32_t ret_val = NRF_SUCCESS;
	
	// Initialize.
	log_init();
	timer_init();
	buttons_leds_init();
	db_discovery_init();
	ble_stack_init();
	gatt_init();
	nus_c_init();
	dfu_c_init();
	scan_init();
	uart_start();
	application_timers_start();
	fstorage_init();

	ret_val = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_SCAN_INIT, NULL, 4);
	APP_ERROR_CHECK(ret_val);
	
	resp_sel_com_t cfg[2];
	uint32_t cfg_s = sizeof(cfg);
	fstorage_read_flash(FLASH_CONFIG, (uint8_t*)cfg, &cfg_s, NULL);
	memcpy(&dongle_dfu_ip, &cfg[0], sizeof(resp_sel_com_t));
	memcpy(&dongle_dfu_fi, &cfg[1], sizeof(resp_sel_com_t));
	
	if (dongle_dfu_ip.ram_offset != 0 || dongle_dfu_fi.ram_offset != 0) {
		memset(&dongle_dfu_ip, 0x00, sizeof(dongle_dfu_ip));
		memset(&dongle_dfu_fi, 0x00, sizeof(dongle_dfu_fi));
	}
	
	// Start execution.
	NRF_LOG_INFO("BLE UART central example started.");
	scan_start();

	// Enter main loop.
	for (;;) {
		idle_state_handle();
	}
}
