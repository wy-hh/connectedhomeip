/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include <lib/support/CHIPMemString.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <lwip/tcpip.h>

extern "C" {
#include <bflb_efuse.h>
//#include <bl60x_fw_api.h>
//#include <bl60x_wifi_driver/bl_main.h>
//#include <bl60x_wifi_driver/wifi_mgmr.h>
//#include <bl_sys.h>
#include <wifi_mgmr.h>
#include <wifi_mgmr_portable.h>
}

namespace chip {
namespace DeviceLayer {

CHIP_ERROR DiagnosticDataProviderImpl::GetBootReason(BootReasonType & bootReason)
{
#if 0
    BL_RST_REASON_E bootCause = bl_sys_rstinfo_get();

    if (bootCause == BL_RST_POWER_OFF)
    {
        bootReason = BootReasonType::kPowerOnReboot;
    }
    else if (bootCause == BL_RST_HARDWARE_WATCHDOG)
    {
        bootReason = BootReasonType::kHardwareWatchdogReset;
    }
    else if (bootCause == BL_RST_SOFTWARE_WATCHDOG)
    {
        bootReason = BootReasonType::kSoftwareWatchdogReset;
    }
    else if (bootCause == BL_RST_SOFTWARE)
    {
        bootReason = BootReasonType::kSoftwareReset;
    }
    else
    {
        bootReason = BootReasonType::kUnspecified;
    }
#endif
    return CHIP_NO_ERROR;
}

static int bl_netif_get_all_ip6(struct netif * netif, ip6_addr_t if_ip6[])
{
    if (netif == NULL || if_ip6 == NULL)
    {
        return 0;
    }

    int addr_count = 0;
    for (int i = 0; (i < LWIP_IPV6_NUM_ADDRESSES) && (i < kMaxIPv6AddrCount); i++)
    {
        if (!ip_addr_cmp(&netif->ip6_addr[i], IP6_ADDR_ANY))
        {
            memcpy(&if_ip6[addr_count++], &netif->ip6_addr[i], sizeof(ip6_addr_t));
        }
    }

    return addr_count;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetNetworkInterfaces(NetworkInterface ** netifpp)
{
    //FIXME:no netif can get
#if 0
    NetworkInterface * ifp = new NetworkInterface();
    struct netif * netif = NULL;

    netif = (struct netif *)&wifiMgmr.wlan_sta.netif;
    if (netif)
    {
        Platform::CopyString(ifp->Name, netif->name);
        ifp->name          = CharSpan::fromCharString(ifp->Name);
        ifp->isOperational = true;
        ifp->type          = EMBER_ZCL_INTERFACE_TYPE_ENUM_WI_FI;
        ifp->offPremiseServicesReachableIPv4.SetNull();
        ifp->offPremiseServicesReachableIPv6.SetNull();
        bflb_efuse_read_mac_address_opt(0, ifp->MacAddress, 1);
        ifp->hardwareAddress = ByteSpan(ifp->MacAddress, 6);

        uint32_t ip, gw, mask, dns;
        wifi_sta_ip4_addr_get(&ip, &mask, &gw, &dns);

        memcpy(ifp->Ipv4AddressesBuffer[0], &ip, kMaxIPv4AddrSize);
        ifp->Ipv4AddressSpans[0] = ByteSpan(ifp->Ipv4AddressesBuffer[0], kMaxIPv4AddrSize);
        ifp->IPv4Addresses       = chip::app::DataModel::List<chip::ByteSpan>(ifp->Ipv4AddressSpans, 1);

        uint8_t ipv6_addr_count = 0;
        ip6_addr_t ip6_addr[kMaxIPv6AddrCount];
        ipv6_addr_count = bl_netif_get_all_ip6(netif, ip6_addr);
        for (uint8_t idx = 0; idx < ipv6_addr_count; ++idx)
        {
            memcpy(ifp->Ipv6AddressesBuffer[idx], ip6_addr[idx].addr, kMaxIPv6AddrSize);
            ifp->Ipv6AddressSpans[idx] = ByteSpan(ifp->Ipv6AddressesBuffer[idx], kMaxIPv6AddrSize);
        }
        ifp->IPv6Addresses = chip::app::DataModel::List<chip::ByteSpan>(ifp->Ipv6AddressSpans, ipv6_addr_count);
    }

    *netifpp = ifp;
#endif
    return CHIP_NO_ERROR;
}

void DiagnosticDataProviderImpl::ReleaseNetworkInterfaces(NetworkInterface * netifp)
{
    while (netifp)
    {
        NetworkInterface * del = netifp;
        netifp                 = netifp->Next;
        delete del;
    }
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiBssId(MutableByteSpan & BssId)
{
    return CopySpanToMutableSpan(ByteSpan(wifiMgmr.wifi_mgmr_stat_info.bssid), BssId);
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiSecurityType(app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum & securityType)
{
    if (ConnectivityMgrImpl()._IsWiFiStationConnected())
    {
        if (wifi_mgmr_security_type_is_open())
        {
            securityType = app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum::kNone;
        }
        else if (wifi_mgmr_security_type_is_wpa())
        {
            securityType = app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum::kWpa;
        }
        else if (wifi_mgmr_security_type_is_wpa2())
        {
            securityType = app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum::kWpa2;
        }
        else if (wifi_mgmr_security_type_is_wpa3())
        {
            securityType = app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum::kWpa3;
        }
        else
        {
            securityType = app::Clusters::WiFiNetworkDiagnostics::SecurityTypeEnum::kWep;
        }

        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_READ_FAILED;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiVersion(app::Clusters::WiFiNetworkDiagnostics::WiFiVersionEnum & wifiVersion)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiChannelNumber(uint16_t & channelNumber)
{
    if (ConnectivityMgrImpl()._IsWiFiStationConnected())
    {
        channelNumber = 6;// FIXME: 616 wifiMgmr no channel wifiMgmr.channel;
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_READ_FAILED;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiRssi(int8_t & rssi)
{
    int tmp_rssi;
    if (ConnectivityMgrImpl()._IsWiFiStationConnected())
    {
        wifi_mgmr_sta_rssi_get(&tmp_rssi);
        rssi = (int8_t)tmp_rssi;
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_READ_FAILED;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiBeaconLostCount(uint32_t & beaconLostCount)
{
    //TODO:add wifi diagnosis info
#if 0 
    wifi_diagnosis_info_t * info;

    info = bl_diagnosis_get();
    if (info)
    {
        beaconLostCount = info->beacon_loss;
    }
#endif
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiCurrentMaxRate(uint64_t & currentMaxRate)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketMulticastRxCount(uint32_t & packetMulticastRxCount)
{
    //TODO:add wifi diagnosis info
#if 0
    wifi_diagnosis_info_t * info;

    info = bl_diagnosis_get();
    if (info)
    {
        packetMulticastRxCount = info->multicast_recv;
    }
#endif
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketMulticastTxCount(uint32_t & packetMulticastTxCount)
{
    //TODO:add wifi diagnosis info
#if 0
    wifi_diagnosis_info_t * info;

    info = bl_diagnosis_get();
    if (info)
    {
        packetMulticastTxCount = info->multicast_send;
    }
#endif
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketUnicastRxCount(uint32_t & packetUnicastRxCount)
{
    //TODO:add wifi diagnosis info
#if 0
    wifi_diagnosis_info_t * info;

    info = bl_diagnosis_get();
    if (info)
    {
        packetUnicastRxCount = info->unicast_recv;
    }
#endif
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketUnicastTxCount(uint32_t & packetUnicastTxCount)
{
    //TODO:add wifi diagnosis info
#if 0
    wifi_diagnosis_info_t * info;

    info = bl_diagnosis_get();
    if (info)
    {
        packetUnicastTxCount = info->multicast_send;
    }
#endif
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiOverrunCount(uint64_t & overrunCount)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR DiagnosticDataProviderImpl::ResetWiFiNetworkDiagnosticsCounts()
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiBeaconRxCount(uint32_t & beaconRxCount)
{
    //TODO:add wifi diagnosis info
#if 0
    wifi_diagnosis_info_t * info;

    info = bl_diagnosis_get();
    if (info)
    {
        beaconRxCount = info->beacon_recv;
    }
#endif

    return CHIP_NO_ERROR;
}

} // namespace DeviceLayer
} // namespace chip
