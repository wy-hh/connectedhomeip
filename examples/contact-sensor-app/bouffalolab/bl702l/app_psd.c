#include "rom_hal_ext.h"
#include "bl_flash.h"
#include "bl_irq.h"
#include "bl_kys.h"

#if !defined(CFG_BUILD_FREERTOS)
#include "rom_freertos_ext.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#endif


#if defined(PDS_TEST)
#if !defined(CFG_BUILD_FREERTOS)
void test_vApplicationSleep( TickType_t xExpectedIdleTime )
#else
void vApplicationSleep( TickType_t xExpectedIdleTime )
#endif
{
    eSleepModeStatus eSleepStatus;
    uint32_t xExpectedSleepTime;
    uint32_t sleepCycles;
    uint32_t sleepTime;
    
    if(xTaskGetTickCount() < 1000){
        return;
    }
    
    eSleepStatus = eTaskConfirmSleepModeStatus();
    if(eSleepStatus == eAbortSleep){
        return;
    }
    
    if(xExpectedIdleTime < 5500){
        return;
    }
    
    xExpectedSleepTime = 5000;
    
#if defined(KEY_WAKEUP_TEST)
    bl_kys_result_t result;
    
    bl_kys_init(sizeof(test_row_pins), sizeof(test_col_pins), test_row_pins, test_col_pins);
    bl_kys_trigger_poll(&result);
    
    if(result.ghost_det){
        printf("ghost key detected!\r\n");
        return;
    }else{
        printf("key_num: %d\r\n", result.key_num);
        if(result.key_num > 0){
            printf("key: ");
            for(int i=0; i<result.key_num; i++){
                printf("(%d, %d) ", result.row_idx[i], result.col_idx[i]);
            }
            printf("\r\n");
        }
        
        bl_pds_set_white_keys(result.key_num, result.row_idx, result.col_idx);
    }
#endif
    
#if defined(PSRAM_RETENTION_TEST)
    uint8_t *psram_buf = (uint8_t *)0x26010000;
    uint32_t psram_size = 4*1024;
    int i;
    
    for(i=0; i<psram_size; i++){
        psram_buf[i] = (i & 0xFF);
    }
    
    bl_pds_set_psram_retention(1);
#endif
    
    bl_rtc_process_xtal_cnt_32k();
    printf("f32k: %u Hz\r\n", bl_rtc_frequency);
    
    printf("will sleep: %lu ms\r\n", xExpectedSleepTime);
    arch_delay_us(100);
    
    sleepCycles = rom_bl_rtc_ms_to_counter(xExpectedSleepTime);
    sleepTime = rom_hal_pds_enter_with_time_compensation(31, sleepCycles);
    
    printf("actually sleep: %lu ms\r\n", sleepTime);
    
#if defined(PSRAM_RETENTION_TEST)
    extern void bl_psram_init(void);
    bl_psram_init();
    
    for(i=0; i<psram_size; i++){
        if(psram_buf[i] != (i & 0xFF)){
            printf("psram check error\r\n");
            break;
        }
    }
    if(i == psram_size){
        printf("psram check success\r\n");
    }
#endif
}