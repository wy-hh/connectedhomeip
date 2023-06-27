/*
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
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

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <NetworkCommissioningDriver.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConnectivityManager.h>
#include <platform/internal/BLEManager.h>

#include <platform/DiagnosticDataProvider.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>

#include <hal_wifi.h>
#include <wifi_mgmr_ext.h>

#include <platform/internal/GenericConnectivityManagerImpl_UDP.ipp>

#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.ipp>
#endif

#include <platform/internal/GenericConnectivityManagerImpl_WiFi.ipp>

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include <platform/internal/GenericConnectivityManagerImpl_BLE.ipp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/internal/GenericConnectivityManagerImpl_Thread.ipp>
#endif

#include <wifi_mgmr_ext.h>

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/netifapi.h>
#include <wifi_mgmr_ext.h>

#include <FreeRTOS.h>
#include <stdio.h>
#include <string.h>
#include <task.h>

using namespace ::chip;
using namespace ::chip::Dnssd;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::TLV;
using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

void ConnectivityManagerImpl::OnWiFiStationDisconnected()
{
    NetworkCommissioning::BLWiFiDriver::GetInstance().SetLastDisconnectReason(NULL);
    uint16_t reason = NetworkCommissioning::BLWiFiDriver::GetInstance().GetLastDisconnectReason();
    uint8_t associationFailureCause =
        chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum::kUnknown);
    WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();

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
    case WLAN_FW_JOIN_NETWORK_SECURITY_NOMATCH:
    case WLAN_FW_JOIN_NETWORK_WEPLEN_ERROR:
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
}

} // namespace DeviceLayer
} // namespace chip
