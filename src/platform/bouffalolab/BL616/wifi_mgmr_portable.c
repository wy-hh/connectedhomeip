#include <stdint.h>
#include <string.h>

#include <bl616.h>
#include <bl616_glb.h>

#include <FreeRTOS.h>
#include <bl_fw_api.h>
#include <wifi_mgmr.h>
#include <lwip/tcpip.h>
#include <log.h>

#include <wifi_mgmr_portable.h>
//#include <wifi_mgmr_profile.h>
//FIXME:no wpa_supplicant
#if 0
#include <supplicant_api.h>

#include <wpa_supplicant/src/utils/common.h>

#include <wpa_supplicant/src/common/defs.h>
#include <wpa_supplicant/src/common/wpa_common.h>
#include <wpa_supplicant/src/rsn_supp/wpa_i.h>
#endif

#define WIFI_STACK_SIZE  (1536)
#define TASK_PRIORITY_FW (16)

static TaskHandle_t wifi_fw_task;
static netif_ext_callback_t netifExtCallback;

extern struct wpa_sm gWpaSm;

int wifi_mgmr_get_bssid(uint8_t * bssid)
{
    int i;

    for (i = 0; i < sizeof(wifiMgmr.wifi_mgmr_stat_info.bssid); i++)
    {
        bssid[i] = wifiMgmr.wifi_mgmr_stat_info.bssid[i];
    }

    return 0;
}

static inline int wifi_mgmr_scan_item_is_timeout(wifi_mgmr_t *mgmr, wifi_mgmr_scan_item_t *item)
{
    return ((unsigned int)rtos_now(0) - (unsigned int)item->timestamp_lastseen) >= mgmr->scan_item_timeout ? 1 : 0;
}

int wifi_mgmr_get_scan_ap_num(void)
{
    int num, count;

    num   = sizeof(wifiMgmr.scan_items) / sizeof(wifiMgmr.scan_items[0]);
    count = 0;

    // for (int i = 0; i < num; i++)
    // {
    //     if (wifiMgmr.scan_items[i].is_used && (!wifi_mgmr_scan_item_is_timeout(&wifiMgmr, &wifiMgmr.scan_items[i])))
    //     {
    //         count++;
    //     }
    // }

    return count;
}

void wifi_mgmr_get_scan_result(wifi_mgmr_scan_item_t * result, int * num, uint8_t scan_type, char * ssid)
{
    int i, count, iter;

    count = sizeof(wifiMgmr.scan_items) / sizeof(wifiMgmr.scan_items[0]);
    iter  = 0;

    // for (i = 0; i < count; i++)
    // {
    //     if (wifiMgmr.scan_items[i].is_used && (!wifi_mgmr_scan_item_is_timeout(&wifiMgmr, &wifiMgmr.scan_items[i])))
    //     {
    //         if (scan_type)
    //         {
    //             if (memcmp(ssid, wifiMgmr.scan_items[i].ssid, wifiMgmr.scan_items[i].ssid_len) != 0)
    //             {
    //                 continue;
    //             }
    //         }
    //         memcpy(result[iter].ssid, wifiMgmr.scan_items[i].ssid, wifiMgmr.scan_items[i].ssid_len);
    //         result[iter].ssid[wifiMgmr.scan_items[i].ssid_len] = 0;
    //         result[iter].ssid_tail[0]                          = 0;
    //         result[iter].ssid_len                              = wifiMgmr.scan_items[i].ssid_len;
    //         memcpy((&(result[iter]))->bssid, wifiMgmr.scan_items[i].bssid, 6);
    //         result[iter].channel = wifiMgmr.scan_items[i].channel;
    //         result[iter].auth    = wifiMgmr.scan_items[i].auth;
    //         result[iter].rssi    = wifiMgmr.scan_items[i].rssi;
    //         iter++;
    //     }
    // }

    *num = iter;
}

int wifi_mgmr_get_scan_result_filter(wifi_mgmr_scan_item_t * result, char * ssid)
{
    int i, count;

    count = sizeof(wifiMgmr.scan_items) / sizeof(wifiMgmr.scan_items[0]);
    for (i = 0; i < count; i++)
    {
        // if (wifiMgmr.scan_items[i].is_used && (!wifi_mgmr_scan_item_is_timeout(&wifiMgmr, &wifiMgmr.scan_items[i])) &&
        //     !strncmp(ssid, wifiMgmr.scan_items[i].ssid, wifiMgmr.scan_items[i].ssid_len))
        // {
        //     memcpy(result->ssid, wifiMgmr.scan_items[i].ssid, wifiMgmr.scan_items[i].ssid_len);
        //     result->ssid[wifiMgmr.scan_items[i].ssid_len] = 0;
        //     result->ssid_tail[0]                          = 0;
        //     result->ssid_len                              = wifiMgmr.scan_items[i].ssid_len;
        //     memcpy(result->bssid, wifiMgmr.scan_items[i].bssid, 6);
        //     result->channel = wifiMgmr.scan_items[i].channel;
        //     result->auth    = wifiMgmr.scan_items[i].auth;
        //     result->rssi    = wifiMgmr.scan_items[i].rssi;
        //     return 0;
        // }
    }

    return -1;
}

int wifi_mgmr_profile_ssid_get(uint8_t * ssid)
{
    //FIXME:no mgmr profile
#if 0
    wifi_mgmr_profile_msg_t profile_msg;

    wifi_mgmr_profile_get_by_idx(&wifiMgmr, &profile_msg, wifiMgmr.profile_active_index);

    memcpy(ssid, profile_msg.ssid, profile_msg.ssid_len);

    return profile_msg.ssid_len;
#endif
    return 0;
}

bool wifi_mgmr_security_type_is_open(void)
{
    return strlen(wifiMgmr.wifi_mgmr_stat_info.passphr) == 0;
}

bool wifi_mgmr_security_type_is_wpa(void)
{
#if 0
    return WPA_PROTO_WPA == gWpaSm.proto;
#endif
    return 0;
}

bool wifi_mgmr_security_type_is_wpa2(void)
{
#if 0
    if (WPA_PROTO_RSN == gWpaSm.proto)
    {
        return (gWpaSm.key_mgmt &
                (WPA_KEY_MGMT_IEEE8021X | WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_PSK_SHA256 | WPA_KEY_MGMT_FT_PSK |
                 WPA_KEY_MGMT_IEEE8021X_SHA256 | WPA_KEY_MGMT_FT_IEEE8021X)) != 0;
    }
#endif
    return false;
}

bool wifi_mgmr_security_type_is_wpa3(void)
{
#if 0
    if (WPA_PROTO_RSN == gWpaSm.proto)
    {
        return (gWpaSm.key_mgmt & (WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_FT_SAE)) != 0;
    }
#endif
    return false;
}

struct netif * deviceInterface_getNetif(void)
{
    // LOCK_TCPIP_CORE();
    struct netif *net_if = netif_find("wl1");
    // UNLOCK_TCPIP_CORE();

    return net_if;
}

void hal_reboot (void) 
{
    taskDISABLE_INTERRUPTS();
    GLB_SW_POR_Reset();
}



// void wifi_event_handler(uint32_t code)
// {
//     switch (code) {
//         case CODE_WIFI_ON_INIT_DONE: {
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\r\n", __func__);
//             wifi_mgmr_init(&conf);
//         } break;
//         case CODE_WIFI_ON_MGMR_DONE: {
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_MGMR_DONE\r\n", __func__);
//         } break;
//         case CODE_WIFI_ON_SCAN_DONE: {
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_SCAN_DONE\r\n", __func__);
//             printf ("CODE_WIFI_ON_SCAN_DONE = %p\r\n", deviceInterface_getNetif());
//             wifi_event_scaned();
//         } break;
//         case CODE_WIFI_ON_CONNECTED: {
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTED\r\n", __func__);
//             void mm_sec_keydump();
//             mm_sec_keydump();
//             printf ("CODE_WIFI_ON_CONNECTED = %p\r\n", deviceInterface_getNetif());
//             wifi_event_connected();
//         } break;
//         case CODE_WIFI_ON_GOT_IP: {
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP\r\n", __func__);
//             LOG_I("[SYS] Memory left is %d Bytes\r\n", kfree_size());
//             printf ("CODE_WIFI_ON_GOT_IP = %p\r\n", deviceInterface_getNetif());
//             wifi_event_got_ip();
//         } break;
//         case CODE_WIFI_ON_GOT_IP6: {
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP6\r\n", __func__);
//             printf ("CODE_WIFI_ON_GOT_IP6 = %p\r\n", deviceInterface_getNetif());
//             wifi_event_got_ip();
//         }
//         break;
//         case CODE_WIFI_ON_DISCONNECT: {
//             // wifi_state = 0;
//             LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_DISCONNECT\r\n", __func__);
//             wifi_event_disconnected();
//         } break;
//         default: {
//             LOG_I("[APP] [EVT] Unknown code %u \r\n", code);
//         }
//     }
// }

int wifi_start_scan(const uint8_t * ssid, uint32_t length) 
{
    wifi_mgmr_scan_params_t config;

    memset(&config, 0, sizeof(wifi_mgmr_scan_params_t));
    if (length && length <= MGMR_SSID_LEN) {
        memcpy(config.ssid_array, ssid, length);
    }

    return wifi_mgmr_sta_scan(&config);
}

// void test_wifi(void *param)
// {
//     vTaskDelay(5 * 1000);

//     char wifi_ssid[64] = { 0 };
//     char passwd[65]    = { 0 };
//     memcpy(wifi_ssid, "CMCC-5TtU", strlen("CMCC-5TtU"));
//     memcpy(passwd, "99dn6zb5", strlen("99dn6zb5"));
//     wifi_sta_connect(wifi_ssid, passwd, NULL, NULL, 1, 0, 0, 1);
//     while(1){
//         vTaskDelay(10 * 1000);
//         printf("hello \r\n");
//     }
// }

void wifi_start_firmware_task(void)
{
    memset(&netifExtCallback, 0, sizeof(netifExtCallback));

    GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_IP_WIFI_PHY | GLB_AHB_CLOCK_IP_WIFI_MAC_PHY | GLB_AHB_CLOCK_IP_WIFI_PLATFORM);
    GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);

    extern void interrupt0_handler(void);
    bflb_irq_attach(WIFI_IRQn, (irq_callback)interrupt0_handler, NULL);
    bflb_irq_enable(WIFI_IRQn);

    netif_add_ext_callback(&netifExtCallback, network_netif_ext_callback);

    xTaskCreate(wifi_main, (char *)"fw", WIFI_STACK_SIZE, NULL, TASK_PRIORITY_FW, &wifi_fw_task);
    // xTaskCreate(test_wifi, "connect wifi", 512, NULL, 15, NULL);
}
