
#include <bl_irq.h>
#include <bl_pds.h>
#include <bl_sec.h>
#include <bl_uart.h>
#include <bl_psram.h>
#include <hal_pds.h>
#include <hosal_uart.h>
#include <hosal_gpio.h>

#include <openthread/thread.h>
#include <lmac154.h>
#include <lmac154_lp.h>
#include <zb_timer.h>
#include <openthread_port.h>
#include <mboard.h>

#define PDS_TOLERANCE_TIME_32768CYCLE (164)
#define PDS_MIN_TIME_32768CYCLE (PDS_WARMUP_LATENCY_CNT + 33)
#define PDS_SLEEP_MAX_MS  3600000

static uint32_t low_power_pds_lmac154_backup[72];
static hosal_gpio_dev_t gpio_key = { .port = CHIP_RESET_PIN, .config = INPUT_PULL_DOWN, .priv = NULL };
static hosal_gpio_dev_t gpio_contact = { .port = CHIP_CONTACT_PIN, .config = INPUT_PULL_DOWN, .priv = NULL };
static hosal_gpio_irq_handler_t app_pds_irq_handler = NULL;
static int app_pds_wakeup_source  = -1;
static uint32_t app_pds_wakeup_pin = -1;

void vApplicationSleep( TickType_t xExpectedIdleTime )
{
    eSleepModeStatus eSleepStatus;
    uint32_t xExpectedSleepTime = xExpectedIdleTime;
    uint32_t sleepCycles;
    uint32_t sleepTime;
    
    eSleepStatus = eTaskConfirmSleepModeStatus();
    if(eSleepStatus == eAbortSleep){
        return;
    }
    
    if(xExpectedIdleTime > PDS_SLEEP_MAX_MS)
        xExpectedIdleTime = PDS_SLEEP_MAX_MS;    

    extern int ble_connection_number(void);
    if (OT_DEVICE_ROLE_CHILD != otThreadGetDeviceRole(otrGetInstance()) || ble_connection_number() || false == otr_isStackIdle()) {
        return;
    }

    bl_rtc_process_xtal_cnt_32k();

    bl_pds_set_psram_retention(1);
    lmac154_sleepStoreRegs(low_power_pds_lmac154_backup);

    sleepCycles = bl_rtc_ms_to_counter(xExpectedIdleTime);
    if(sleepCycles < PDS_TOLERANCE_TIME_32768CYCLE + PDS_MIN_TIME_32768CYCLE){
        return;
    }

    sleepTime = hal_pds_enter_with_time_compensation(31, sleepCycles);
    
    RomDriver_AON_Power_On_XTAL();
    RomDriver_HBN_Set_ROOT_CLK_Sel(HBN_ROOT_CLK_XTAL);

    if (lmac154_isDisabled()) {

        lmac154_sleepRestoreRegs(low_power_pds_lmac154_backup);
        lmac154_disableRx();

        zb_timer_cfg(bl_rtc_get_counter() * (32768 >> LMAC154_US_PER_SYMBOL_BITS));

        zb_timer_restore_events(true);

        bl_irq_register(M154_IRQn, lmac154_get2015InterruptHandler());
        bl_irq_enable(M154_IRQn);
    }
    bl_sec_init();

    extern hosal_uart_dev_t uart_stdio;
    bl_uart_init(uart_stdio.config.uart_id, uart_stdio.config.tx_pin, uart_stdio.config.rx_pin, 
        uart_stdio.config.cts_pin, uart_stdio.config.rts_pin, uart_stdio.config.baud_rate);

    extern BaseType_t TrapNetCounter, *pTrapNetCounter;
    if (app_pds_wakeup_source == PDS_WAKEUP_BY_RTC) {
        extern void * pxCurrentTCB;
        printf("[%lu] wakeup source: rtc. %lu vs %lu ms.\r\n", 
            (uint32_t)bl_rtc_get_timestamp_ms(), xExpectedSleepTime, sleepTime);
    } else if(app_pds_wakeup_source == PDS_WAKEUP_BY_GPIO) {

        if (((1 << CHIP_RESET_PIN) & app_pds_wakeup_pin) && app_pds_irq_handler) {
            app_pds_irq_handler(&gpio_key);
        }

        if (((1 << CHIP_CONTACT_PIN) & app_pds_wakeup_pin) && app_pds_irq_handler) {
            app_pds_irq_handler(&gpio_contact);
        }

        printf("[%lu] wakeup source: gpio -> 0x%08lX. %lu vs %lu ms.\r\n", 
            (uint32_t)bl_rtc_get_timestamp_ms(), app_pds_wakeup_pin, xExpectedSleepTime, sleepTime);
    } else {
        printf("[%lu] wakeup source: unknown. %lu vs %lu ms.\r\n", 
            (uint32_t)bl_rtc_get_timestamp_ms(), xExpectedSleepTime, sleepTime);
    }
}

void app_pds_config_pin(void) 
{
    uint8_t wakeup_pin [] = {gpio_key.port, gpio_contact.port};

    hosal_gpio_init(&gpio_key);
    hosal_gpio_init(&gpio_contact);
    hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_SYNC_FALLING_RISING_EDGE, app_pds_irq_handler, &gpio_key);
    hosal_gpio_irq_set(&gpio_contact, HOSAL_IRQ_TRIG_SYNC_FALLING_RISING_EDGE, app_pds_irq_handler, &gpio_contact);

    bl_pds_gpio_wakeup_cfg(wakeup_pin, sizeof(wakeup_pin) / sizeof(wakeup_pin[0]), PDS_GPIO_EDGE_BOTH);
}

void app_pds_fastboot_done_callback(void) 
{
    bl_psram_init();

    app_pds_config_pin();

    app_pds_wakeup_source = bl_pds_get_wakeup_source();
    app_pds_wakeup_pin = bl_pds_get_wakeup_gpio();
}

void app_pds_init(hosal_gpio_irq_handler_t pinHandler) 
{
    bl_pds_init();

    bl_pds_register_fastboot_done_callback(app_pds_fastboot_done_callback);

    app_pds_irq_handler = pinHandler;
    app_pds_config_pin();
}