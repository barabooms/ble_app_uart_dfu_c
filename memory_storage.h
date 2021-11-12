#ifndef MEMORY_STORAGE_H
#define MEMORY_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define RAM_IMAGE_SIZE			256


typedef enum {
	FLASH_INIT_PACKET,
	FLASH_FIRMWARE_IMAGE,
	FLASH_CONFIG
} flash_addr_type_t;

uint8_t* fstorage_init_packet_ptr(void);
uint8_t* fstorage_firmware_image_ptr(void);

void fstorage_init(void);
uint8_t* fstorage_get_image_ptr(void);
void fstorage_clear_flash(flash_addr_type_t type, uint32_t* page_offset_p);
uint8_t fstorage_write_flash(flash_addr_type_t type, uint8_t* data_p, uint32_t* size_p, uint32_t* page_offset_p);
uint8_t fstorage_read_flash(flash_addr_type_t type, uint8_t* data_p, uint32_t* size_p, uint32_t* page_offset_p);

#endif
