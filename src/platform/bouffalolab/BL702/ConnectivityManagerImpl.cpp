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

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConnectivityManager.h>
#include <platform/internal/BLEManager.h>

#include <platform/internal/GenericConnectivityManagerImpl_WiFi.ipp>
#include <platform/DiagnosticDataProvider.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/bouffalolab/BL702/NetworkCommissioningDriver.h>
#include <platform/bouffalolab/BL702/WiFiInterface.h>

#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/ip6_addr.h>
#include <lwip/nd6.h>
#include <lwip/netif.h>
#include <lwip/ethip6.h>
#include <lwip/dhcp6.h>

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::TLV;
using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

CHIP_ERROR ConnectivityManagerImpl::InitWiFi()
{
    uint8_t wifi_macAddress[8];

    wifiInterface_init();
    wifiInterface_getMacAddress(wifi_macAddress);
    ChipLogProgress(NotSpecified, "Wi-Fi submodule initialized, mac %02X:%02X:%02X:%02X:%02X:%02X", 
        wifi_macAddress[0], wifi_macAddress[1], wifi_macAddress[2], wifi_macAddress[3], wifi_macAddress[4], 
        wifi_macAddress[5]);
    
    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::OnWiFiStationDisconnected()
{
    ChipDeviceEvent event;
    event.Type                          = DeviceEventType::kWiFiConnectivityChange;
    event.WiFiConnectivityChange.Result = kConnectivity_Lost;
    PlatformMgr().PostEventOrDie(&event);

    NetworkCommissioning::BLWiFiDriver::GetInstance().SetLastDisconnectReason(NULL);
    uint16_t reason = NetworkCommissioning::BLWiFiDriver::GetInstance().GetLastDisconnectReason();
    uint8_t associationFailureCause = chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::AssociationFailureCauseEnum::kUnknown);
    WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();
    if (delegate)
    {
        delegate->OnDisconnectionDetected(reason);
        delegate->OnAssociationFailureDetected(associationFailureCause, reason);
        delegate->OnConnectionStatusChanged(
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::ConnectionStatusEnum::kNotConnected));
    }
}

void ConnectivityManagerImpl::UpdateWiFiConnectivity(struct netif * interface)
{
    bool haveIPv4Conn = false;
    bool haveIPv6Conn = false;
    const bool hadIPv4Conn  = mConnectivityFlag.Has(ConnectivityFlags::kHaveIPv4InternetConnectivity);
    const bool hadIPv6Conn  = mConnectivityFlag.Has(ConnectivityFlags::kHaveIPv6InternetConnectivity);
    IPAddress addr;

    if (interface != NULL && netif_is_up(interface) && netif_is_link_up(interface))
    {
        mConnectivityFlag.Clear(ConnectivityFlags::kAwaitingConnectivity);

        if (!ip4_addr_isany(netif_ip4_addr(interface)) && !ip4_addr_isany(netif_ip4_gw(interface)))
        {
            haveIPv4Conn = true;
            char addrStr[INET_ADDRSTRLEN];
            ip4addr_ntoa_r(netif_ip4_addr(interface), addrStr, sizeof(addrStr));
            IPAddress::FromString(addrStr, addr);
            if (0 != memcmp(netif_ip4_addr(interface), &m_ip4addr, sizeof(ip4_addr_t))) {
                ChipLogProgress(DeviceLayer, "IPv4 Address Assigned, %s", ip4addr_ntoa(netif_ip4_addr(interface)));
                memcpy(&m_ip4addr, netif_ip4_addr(interface), sizeof(ip4_addr_t));
                ConnectivityMgrImpl().OnIPv4AddressAvailable();
            }
        }

        // Search among the IPv6 addresses assigned to the interface for a Global Unicast
        // address (2000::/3) that is in the valid state.  If such an address is found...
        for (uint32_t i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
        {
            if (!ip6_addr_islinklocal(netif_ip6_addr(interface, i)) &&
                ip6_addr_isvalid(netif_ip6_addr_state(interface, i)))
            {
                haveIPv6Conn = true;
                if (0 != memcmp(netif_ip6_addr(interface, i), m_ip6addr + i, sizeof(ip6_addr_t))) {
                    ChipLogProgress(DeviceLayer, "IPv6 Address Assigned, %s", ip6addr_ntoa(netif_ip6_addr(interface, i)));
                    memcpy(m_ip6addr + i, netif_ip6_addr(interface, i), sizeof(ip6_addr_t));
                    ConnectivityMgrImpl().OnIPv6AddressAvailable();
                }
            }
        }
    }

    // If the internet connectivity state has changed...
    if (haveIPv4Conn != hadIPv4Conn || haveIPv6Conn != hadIPv6Conn)
    {
        // Update the current state.
        mConnectivityFlag.Set(ConnectivityFlags::kHaveIPv4InternetConnectivity, haveIPv4Conn);
        mConnectivityFlag.Set(ConnectivityFlags::kHaveIPv6InternetConnectivity, haveIPv6Conn);

        // Alert other components of the state change.
        ChipDeviceEvent event;
        event.Type                                 = DeviceEventType::kInternetConnectivityChange;
        event.InternetConnectivityChange.IPv4      = GetConnectivityChange(hadIPv4Conn, haveIPv4Conn);
        event.InternetConnectivityChange.IPv6      = GetConnectivityChange(hadIPv6Conn, haveIPv6Conn);
        event.InternetConnectivityChange.ipAddress = addr;
        PlatformMgr().PostEventOrDie(&event);

        if (haveIPv4Conn != hadIPv4Conn)
        {
            ChipLogProgress(DeviceLayer, "%s Internet connectivity %s", "IPv4", (haveIPv4Conn) ? "ESTABLISHED" : "LOST");
        }

        if (haveIPv6Conn != hadIPv6Conn)
        {
            ChipLogProgress(DeviceLayer, "%s Internet connectivity %s", "IPv6", (haveIPv6Conn) ? "ESTABLISHED" : "LOST");
        }
    }
}

extern "C" void wifiInterface_eventConnected(struct netif * interface) 
{
    ChipLogProgress(DeviceLayer, "wifiInterface_eventConnected");
    ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connecting_Succeeded);
}

extern "C" void wifiInterface_eventDisconnected(struct netif * interface) 
{
    ChipLogProgress(DeviceLayer, "wifiInterface_eventDisconnected");
    if (ConnectivityManager::kWiFiStationState_Connecting == ConnectivityMgrImpl().GetWiFiStationState())
    {
        ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connecting_Failed);
    }
    else
    {
        ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_NotConnected);
    }
}

extern "C" void wifiInterface_eventLinkStatusDone(struct netif * interface, netbus_fs_link_status_ind_cmd_msg_t *pkg_data) 
{
    ChipLogProgress(DeviceLayer, "wifiInterface_eventLinkStatusDone");

    struct bflbwifi_ap_record* record = &pkg_data->record;
    if (record->link_status == BF1B_WIFI_LINK_STATUS_UP) {
        ChipLogProgress(DeviceLayer, "link status up!");
        // if (ConnectivityManager::kWiFiStationState_Disconnecting == ConnectivityMgrImpl().GetWiFiStationState()) {
        //     ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connecting_Succeeded);
        // }
    } else if (record->link_status == BF1B_WIFI_LINK_STATUS_DOWN){
        ChipLogProgress(DeviceLayer, "link status down!");
        ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Disconnecting);
    } else {
        ChipLogProgress(DeviceLayer, "link status unknown!");
    }
}

extern "C" void wifiInterface_eventGotIP(struct netif * interface) 
{
    ChipLogProgress(DeviceLayer, "wifiInterface_eventGotIP");
    ConnectivityMgrImpl().UpdateWiFiConnectivity(interface);
    ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connected);
}

extern "C" void wifiInterface_eventScanDone(struct netif * interface, netbus_fs_scan_ind_cmd_msg_t* pmsg)
{
    ChipLogProgress(DeviceLayer, "wifiInterface_eventScanDone");
    NetworkCommissioning::BLWiFiDriver::GetInstance().OnScanWiFiNetworkDone(pmsg);
}

} // namespace DeviceLayer
} // namespace chip
