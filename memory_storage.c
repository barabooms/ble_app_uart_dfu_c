
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "nrf.h"
#include "nrf_soc.h"
#include "nordic_common.h"
#include "app_util.h"
#include "nrf_fstorage.h"
#include "memory_strorage.h"

#ifdef SOFTDEVICE_PRESENT
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_fstorage_sd.h"
#else
#include "nrf_drv_clock.h"
#include "nrf_fstorage_nvmc.h"
#endif

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

static void ip_fstorage_evt_handler(nrf_fstorage_evt_t * p_evt);
static void fi_fstorage_evt_handler(nrf_fstorage_evt_t * p_evt);


NRF_FSTORAGE_DEF(nrf_fstorage_t ip_fstorage) =
{
    /* Set a handler for fstorage events. */
    .evt_handler = ip_fstorage_evt_handler,

    /* These below are the boundaries of the flash space assigned to this instance of fstorage.
     * You must set these manually, even at runtime, before nrf_fstorage_init() is called.
     * The function nrf5_flash_end_addr_get() can be used to retrieve the last address on the
     * last page of flash available to write data. */
    .start_addr = 0x40000,
    .end_addr   = 0x41000,
};
NRF_FSTORAGE_DEF(nrf_fstorage_t fi_fstorage) =
{
    /* Set a handler for fstorage events. */
    .evt_handler = fi_fstorage_evt_handler,

    /* These below are the boundaries of the flash space assigned to this instance of fstorage.
     * You must set these manually, even at runtime, before nrf_fstorage_init() is called.
     * The function nrf5_flash_end_addr_get() can be used to retrieve the last address on the
     * last page of flash available to write data. */
    .start_addr = 0x41000,
    .end_addr   = 0x51000,
};


uint8_t image_data[256] = {0};




/**@brief   Helper function to obtain the last address on the last page of the on-chip flash that
 *          can be used to write user data.
 */
static uint32_t nrf5_flash_end_addr_get()
{
    uint32_t const bootloader_addr = BOOTLOADER_ADDRESS;
    uint32_t const page_sz         = NRF_FICR->CODEPAGESIZE;
    uint32_t const code_sz         = NRF_FICR->CODESIZE;

    return (bootloader_addr != 0xFFFFFFFF ?
            bootloader_addr : (code_sz * page_sz));
}


static void ip_fstorage_evt_handler(nrf_fstorage_evt_t * p_evt)
{
    if (p_evt->result != NRF_SUCCESS)
    {
        NRF_LOG_INFO("--> Event received: ERROR while executing an fstorage operation.");
        return;
    }

    switch (p_evt->id)
    {
        case NRF_FSTORAGE_EVT_WRITE_RESULT:
        {
            NRF_LOG_INFO("--> Event received: wrote %d bytes at address 0x%x.",
                         p_evt->len, p_evt->addr);
        } break;

        case NRF_FSTORAGE_EVT_ERASE_RESULT:
        {
            NRF_LOG_INFO("--> Event received: erased %d page from address 0x%x.",
                         p_evt->len, p_evt->addr);
        } break;

        default:
            break;
    }
}


static void fi_fstorage_evt_handler(nrf_fstorage_evt_t * p_evt)
{
    if (p_evt->result != NRF_SUCCESS)
    {
        NRF_LOG_INFO("--> Event received: ERROR while executing an fstorage operation.");
        return;
    }

    switch (p_evt->id)
    {
        case NRF_FSTORAGE_EVT_WRITE_RESULT:
        {
            NRF_LOG_INFO("--> Event received: wrote %d bytes at address 0x%x.",
                         p_evt->len, p_evt->addr);
        } break;

        case NRF_FSTORAGE_EVT_ERASE_RESULT:
        {
            NRF_LOG_INFO("--> Event received: erased %d page from address 0x%x.",
                         p_evt->len, p_evt->addr);
        } break;

        default:
            break;
    }
}


void wait_for_flash_ready(nrf_fstorage_t const * p_fstorage)
{
    /* While fstorage is busy, sleep and wait for an event. */
    while (nrf_fstorage_is_busy(p_fstorage))
    {
        power_manage();
    }
}





void fstorage_init(void)
{
    ret_code_t rc;

    NRF_LOG_INFO("fstorage init started.");

    nrf_fstorage_api_t * p_fs_api;
    /* Initialize an fstorage instance using the nrf_fstorage_sd backend.
     * nrf_fstorage_sd uses the SoftDevice to write to flash. This implementation can safely be
     * used whenever there is a SoftDevice, regardless of its status (enabled/disabled). */
    p_fs_api = &nrf_fstorage_sd;

    rc = nrf_fstorage_init(&ip_fstorage, p_fs_api, NULL);
    APP_ERROR_CHECK(rc);

    /* It is possible to set the start and end addresses of an fstorage instance at runtime.
     * They can be set multiple times, should it be needed. The helper function below can
     * be used to determine the last address on the last page of flash memory available to
     * store data. */
    (void) nrf5_flash_end_addr_get();

    /* Let's write to flash. */
    NRF_LOG_INFO("Writing \"%x\" to flash.", m_data);
    rc = nrf_fstorage_write(&fstorage, 0x3e000, &m_data, sizeof(m_data), NULL);
    APP_ERROR_CHECK(rc);

    wait_for_flash_ready(&fstorage);
    NRF_LOG_INFO("Done.");

#ifdef SOFTDEVICE_PRESENT
    /* Enable the SoftDevice and the BLE stack. */
    NRF_LOG_INFO("Enabling the SoftDevice.");
    ble_stack_init();

    m_data = 0xDEADBEEF;

    NRF_LOG_INFO("Writing \"%x\" to flash.", m_data);
    rc = nrf_fstorage_write(&fstorage, 0x3e100, &m_data, sizeof(m_data), NULL);
    APP_ERROR_CHECK(rc);

    wait_for_flash_ready(&fstorage);
    NRF_LOG_INFO("Done.");
#endif

    NRF_LOG_INFO("Writing \"%s\" to flash.", m_hello_world);
    rc = nrf_fstorage_write(&fstorage, 0x3f000, m_hello_world, sizeof(m_hello_world), NULL);
    APP_ERROR_CHECK(rc);

    wait_for_flash_ready(&fstorage);
    NRF_LOG_INFO("Done.");

    NRF_LOG_INFO("Use 'read' to read bytes from the flash.");
    NRF_LOG_INFO("Use 'write' to write bytes to the flash.");
    NRF_LOG_INFO("Use 'erase' to erase flash pages.");
    NRF_LOG_INFO("Use 'flasharea' to print and configure the flash read boundaries.");

}
