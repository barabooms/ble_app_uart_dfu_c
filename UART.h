#ifndef UART_H
#define UART_H

#define UART_TX_FIFO_SIZE			256				/**< UART TX buffer size. */
#define UART_RX_FIFO_SIZE			256				/**< UART RX buffer size. */

#define UART_BUFF_SIZE				128

#define UART_TX_TIMER_INTERVAL		300		// us

#define UART_WAITING_ERROR			2000	// ms
#define UART_REPAIR_ERROR			30000	// ms

/*
�������� ������ �� Dongle										DEC	HEX
������ ���������� ������ (� ����� ������ �� 0���)				225	0xE1
������ ���������� ������ (� ������ ������ �� 0�55)				226	0xE2
������ XOR														227	0xE3
������ ������ ����������										228	0xE4
������, ��� ����������� �� bluetooth							229	0xE5
������ ������, ������� �������� ���������						230	0xE6
������, ���� �� ���� ������ �� ��� � ������� 5 ������			231	0xE7
������, ������ ������� ������� �������							232	0xE8
*/
//==============================================================
//					UART ERROR LIST
typedef enum
{
	UART_ERROR_NONE = 0x00,
	UART_ERROR_E1 = 0xE1,
	UART_ERROR_E2 = 0xE2,
	UART_ERROR_E3 = 0xE3,
	UART_ERROR_E4 = 0xE4,
	UART_ERROR_E5 = 0xE5,
	UART_ERROR_E6 = 0xE6,
	UART_ERROR_E7 = 0xE7,
	UART_ERROR_E8 = 0xE8
} uart_error_list_t;
//==============================================================
typedef union
{
    uint8_t Full;

    struct
    {
        unsigned start_reciver		: 1;
        unsigned char_55_complete	: 1;
        unsigned data_complete		: 1;
    }Bits;
} statRxDT_t;
//==============================================================
typedef union
{
    uint8_t Full;

    struct
    {
        unsigned start_transmitter		: 1;
        unsigned transmit_55_complete	: 1;
        unsigned transmit_complete		: 1;
    }Bits;
} statTxDT_t;
//==============================================================
void set_error(uart_error_list_t number);
void uart_waiting_timer_start(void);
void uart_waiting_timer_stop(void);
void uart_waiting_timer_restart(void);

void uart_start(void);
void uart_stop(void);

void send_message(void);
void rx_message(void);
void tx_message(void);

void SetUART_TX(void* data);
void SetUART_control_TX(uint8_t index, void* data, uint8_t len);

void* GetUART_RX(void);
void* GetUART_TX(void);
void* GetUART_addr(void);
void* GetUART_len(void);
void* GetUART_com(void);
void* GetUART_dat(void);
void* GetUART_xor(void);
uint8_t GetUART_data_len(void);

void reset_buff(void);
void uart_tx_buff_clear(void);

void* calc_crc(uint8_t* data, uint8_t len);

#endif
