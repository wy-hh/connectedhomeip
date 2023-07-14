/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <crypto/CHIPCryptoPAL.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/PlatformManager.h>
#include <platform/bouffalolab/BL616/NetworkCommissioningDriver.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/internal/GenericPlatformManagerImpl_FreeRTOS.ipp>

#include <lwip/tcpip.h>

//#include <aos/kernel.h>
//#include <bl60x_fw_api.h>
//#include <bl_sec.h>
//#include <event_device.h>
//#include <hal_wifi.h>
#include <lwip/tcpip.h>
#include <wifi_mgmr_ext.h>

extern "C" {
#include <bl616.h>
#include <bl_fw_api.h>
#include <bl616_glb.h>
#include <rfparam_adapter.h>
}

#define WIFI_STACK_SIZE  (1536)
#define TASK_PRIORITY_FW (16)

static TaskHandle_t wifi_fw_task;
namespace chip {
namespace DeviceLayer {

static wifi_conf_t conf = {
    .country_code = "CN",
};

static int app_entropy_source(void * data, unsigned char * output, size_t len, size_t * olen)
{
    //FIXME:app entropy source
    //bl_rand_stream(reinterpret_cast<uint8_t *>(output), static_cast<int>(len));
    *olen = len;

    return 0;
}

static void WifiStaDisconect(void)
{
    NetworkCommissioning::BLWiFiDriver::GetInstance().SetLastDisconnectReason(NULL);

    uint16_t reason = NetworkCommissioning::BLWiFiDriver::GetInstance().GetLastDisconnectReason();
    uint8_t associationFailureCause =
        chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum::kUnknown);
    WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();

    if (ConnectivityManagerImpl().GetWiFiStationState() == ConnectivityManager::kWiFiStationState_Disconnecting)
    {
        return;
    }

    ChipLogError(DeviceLayer, "WiFi station disconnect, reason %d.", reason);

    switch (reason)
    {
    case WLAN_FW_TX_ASSOC_FRAME_ALLOCATE_FAIILURE:
    case WLAN_FW_ASSOCIATE_FAIILURE:
    case WLAN_FW_4WAY_HANDSHAKE_ERROR_PSK_TIMEOUT_FAILURE:
        associationFailureCause =
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum::kAssociationFailed);
        if (delegate)
        {
            delegate->OnAssociationFailureDetected(associationFailureCause, reason);
        }
        break;
    case WLAN_FW_TX_AUTH_FRAME_ALLOCATE_FAIILURE:
    case WLAN_FW_AUTHENTICATION_FAIILURE:
    case WLAN_FW_AUTH_ALGO_FAIILURE:
    case WLAN_FW_DEAUTH_BY_AP_WHEN_NOT_CONNECTION:
    case WLAN_FW_DEAUTH_BY_AP_WHEN_CONNECTION:
    case WLAN_FW_4WAY_HANDSHAKE_TX_DEAUTH_FRAME_TRANSMIT_FAILURE:
    case WLAN_FW_4WAY_HANDSHAKE_TX_DEAUTH_FRAME_ALLOCATE_FAIILURE:
    case WLAN_FW_AUTH_OR_ASSOC_RESPONSE_TIMEOUT_FAILURE:
    case WLAN_FW_DISCONNECT_BY_USER_WITH_DEAUTH:
    case WLAN_FW_DISCONNECT_BY_USER_NO_DEAUTH:
        associationFailureCause =
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum::kAuthenticationFailed);
        if (delegate)
        {
            delegate->OnAssociationFailureDetected(associationFailureCause, reason);
        }
        break;
    case WLAN_FW_SCAN_NO_BSSID_AND_CHANNEL:
        associationFailureCause =
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum::kSsidNotFound);
        if (delegate)
        {
            delegate->OnAssociationFailureDetected(associationFailureCause, reason);
        }
        break;
    case WLAN_FW_BEACON_LOSS:
    case WLAN_FW_NETWORK_SECURITY_NOMATCH:
    case WLAN_FW_NETWORK_WEPLEN_ERROR:
    case WLAN_FW_DISCONNECT_BY_FW_PS_TX_NULLFRAME_FAILURE:
    case WLAN_FW_CREATE_CHANNEL_CTX_FAILURE_WHEN_JOIN_NETWORK:
    case WLAN_FW_ADD_STA_FAILURE:
    case WLAN_FW_JOIN_NETWORK_FAILURE:
        break;

    default:
        if (delegate)
        {
            delegate->OnAssociationFailureDetected(associationFailureCause, reason);
        }
        break;
    }

    if (delegate)
    {
        delegate->OnDisconnectionDetected(reason);
        delegate->OnConnectionStatusChanged(
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::ConnectionStatusEnum::kNotConnected));
    }

    ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManagerImpl::kWiFiStationState_Disconnecting);
}

static void WifiStaConnected(void)
{
    char ap_ssid[64];
    WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();

    if (ConnectivityManagerImpl().GetWiFiStationState() == ConnectivityManager::kWiFiStationState_Connected)
    {
        return;
    }

    memset(ap_ssid, 0, sizeof(ap_ssid));
    // wifi_mgmr_sta_ssid_get(ap_ssid);
    // wifi_mgmr_ap_item_t * ap_info = mgmr_get_ap_info_handle();
    // wifi_mgmr_get_scan_result_filter(ap_info, ap_ssid);

    ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManagerImpl::kWiFiStationState_Connected);
    ConnectivityMgrImpl().OnWiFiStationConnected();
    if (delegate)
    {
        delegate->OnConnectionStatusChanged(
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::ConnectionStatusEnum::kConnected));
    }
}

void OnWiFiPlatformEvent(uint32_t code, void * private_data)
{
    switch (code)
    {
    case CODE_WIFI_ON_INIT_DONE: {
        wifi_mgmr_init(&conf);
    }
    break;
    case CODE_WIFI_ON_MGMR_DONE: {
    }
    break;
    case CODE_WIFI_ON_CONNECTED: {
        ChipLogProgress(DeviceLayer, "WiFi station connected.");
    }
    break;
    case CODE_WIFI_ON_SCAN_DONE: {
        chip::DeviceLayer::PlatformMgr().LockChipStack();
        NetworkCommissioning::BLWiFiDriver::GetInstance().OnScanWiFiNetworkDone();
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    break;
    case CODE_WIFI_ON_CONNECTING: {
        ChipLogProgress(DeviceLayer, "WiFi station starts connecting.");
    }
    break;
    case CODE_WIFI_ON_DISCONNECT: {
        //ChipLogProgress(DeviceLayer, "WiFi station disconnect, reason %s.", wifi_mgmr_status_code_str(event->value));
        ChipLogProgress(DeviceLayer, "WiFi station disconnect, reason .");

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        WifiStaDisconect();
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    break;
    case CODE_WIFI_CMD_RECONNECT: {
        ChipLogProgress(DeviceLayer, "WiFi station reconnect.");
    }
    break;
    case CODE_WIFI_ON_GOT_IP: {

        ChipLogProgress(DeviceLayer, "WiFi station gets IPv4 address.");

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        WifiStaConnected();
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    break;
    case CODE_WIFI_ON_GOT_IP6: {
        ChipLogProgress(DeviceLayer, "WiFi station gets IPv6 address.");

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        ConnectivityMgrImpl().OnIPv6AddressAvailable();
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    break;
    default: {
        ChipLogProgress(DeviceLayer, "WiFi station gets unknow code %lu.", code);
        /*nothing*/
    }
    }
}

extern "C" void wifi_event_handler(uint32_t code)
{
    OnWiFiPlatformEvent(code, NULL);
}

int wifi_start_firmware_task(void)
{
    //LOG_I("Starting wifi ...\r\n");

    /* enable wifi clock */

    // GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_IP_WIFI_PHY | GLB_AHB_CLOCK_IP_WIFI_MAC_PHY | GLB_AHB_CLOCK_IP_WIFI_PLATFORM);
    // GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_WIFI);

    // /* set ble controller EM Size */

    // GLB_Set_EM_Sel(GLB_WRAM160KB_EM0KB);

    // if (0 != rfparam_init(0, NULL, 0)) {
    //     //LOG_I("PHY RF init failed!\r\n");
    //     return 0;
    // }

    // //LOG_I("PHY RF init success!\r\n");

    // /* Enable wifi irq */

    // extern void interrupt0_handler(void);
    // bflb_irq_attach(WIFI_IRQn, (irq_callback)interrupt0_handler, NULL);
    // bflb_irq_enable(WIFI_IRQn);

    xTaskCreate(wifi_main, (char *)"fw", WIFI_STACK_SIZE, NULL, TASK_PRIORITY_FW, &wifi_fw_task);

    return 0;
}

CHIP_ERROR PlatformManagerImpl::_InitChipStack(void)
{
    CHIP_ERROR err                 = CHIP_NO_ERROR;
    static uint8_t stack_wifi_init = 0;
    TaskHandle_t backup_eventLoopTask;

    // Initialize LwIP.
    tcpip_init(NULL, NULL);
    //aos_register_event_filter(EV_WIFI, OnWiFiPlatformEvent, NULL);

    if (1 == stack_wifi_init)
    {
        ChipLogError(DeviceLayer, "Wi-Fi already initialized!");
        return CHIP_NO_ERROR;
    }

    //hal_wifi_start_firmware_task();

    wifi_start_firmware_task();
    stack_wifi_init = 1;
    //aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);

    err = chip::Crypto::add_entropy_source(app_entropy_source, NULL, 16);
    SuccessOrExit(err);

    // Call _InitChipStack() on the generic implementation base class
    // to finish the initialization process.
    /** weiyin, backup mEventLoopTask which is reset in _InitChipStack */
    backup_eventLoopTask = Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::mEventLoopTask;
    err                  = Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::_InitChipStack();
    SuccessOrExit(err);
    Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::mEventLoopTask = backup_eventLoopTask;

exit:
    return err;
}

} // namespace DeviceLayer
} // namespace chip
