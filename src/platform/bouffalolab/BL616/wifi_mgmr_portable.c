#include <stdint.h>
#include <string.h>
#include <bl616_glb.h>

#include <wifi_mgmr.h>
//#include <wifi_mgmr_profile.h>
//FIXME:no wpa_supplicant
#if 0
#include <supplicant_api.h>

#include <wpa_supplicant/src/utils/common.h>

#include <wpa_supplicant/src/common/defs.h>
#include <wpa_supplicant/src/common/wpa_common.h>
#include <wpa_supplicant/src/rsn_supp/wpa_i.h>
#endif

int btblecontroller_em_config(void)
{
    extern uint8_t __LD_CONFIG_EM_SEL;
    volatile uint32_t em_size;

    em_size = (uint32_t)&__LD_CONFIG_EM_SEL;

    if (em_size == 0) {
        GLB_Set_EM_Sel(GLB_WRAM160KB_EM0KB);
    } else if (em_size == 32*1024) {
        GLB_Set_EM_Sel(GLB_WRAM128KB_EM32KB);
    } else if (em_size == 64*1024) {
        GLB_Set_EM_Sel(GLB_WRAM96KB_EM64KB);
    } else {
        GLB_Set_EM_Sel(GLB_WRAM96KB_EM64KB);
    }

    return 0;
}

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
    // return wifi_mgmr_sta_netif_get();
    return NULL;
}

void hal_reboot (void) 
{
    taskDISABLE_INTERRUPTS();
    GLB_SW_POR_Reset();
}
