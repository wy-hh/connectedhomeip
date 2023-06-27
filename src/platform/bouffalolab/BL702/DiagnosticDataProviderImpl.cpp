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

#include <lib/support/CHIPMemString.h>

#include <platform/DiagnosticDataProvider.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#if !CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <lwip/tcpip.h>
#endif

extern "C" {
#include <bl_sys.h>
#if !CHIP_DEVICE_CONFIG_ENABLE_THREAD && !CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <eth_bd.h>
#endif
}
namespace chip {
namespace DeviceLayer {

CHIP_ERROR DiagnosticDataProviderImpl::GetBootReason(BootReasonType & bootReason)
{
    BL_RST_REASON_E bootCause = bl_sys_rstinfo_get();

    if (BL_RST_POR == bootCause)
    {
        bootReason = BootReasonType::kPowerOnReboot;
    }
    else if (BL_RST_BOR == bootCause)
    {
        bootReason = BootReasonType::kBrownOutReset;
    }
    else if (BL_RST_WDT == bootCause)
    {
        bootReason = BootReasonType::kHardwareWatchdogReset;
    }
    else if (BL_RST_SOFTWARE == bootCause)
    {
        bootReason = BootReasonType::kSoftwareReset;
    }
    else
    {
        bootReason = BootReasonType::kUnspecified;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetNetworkInterfaces(NetworkInterface ** netifpp)
{
    NetworkInterface * ifp = new NetworkInterface();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    const char * threadNetworkName = otThreadGetNetworkName(ThreadStackMgrImpl().OTInstance());
    ifp->name                      = Span<const char>(threadNetworkName, strlen(threadNetworkName));
    ifp->isOperational             = true;
    ifp->offPremiseServicesReachableIPv4.SetNull();
    ifp->offPremiseServicesReachableIPv6.SetNull();
    ifp->type = EMBER_ZCL_INTERFACE_TYPE_ENUM_THREAD;
    uint8_t macBuffer[ConfigurationManager::kPrimaryMACAddressLength];
    ConfigurationMgr().GetPrimary802154MACAddress(macBuffer);
    ifp->hardwareAddress = ByteSpan(macBuffer, ConfigurationManager::kPrimaryMACAddressLength);

#else

    struct netif * netif = NULL;
#if !CHIP_DEVICE_CONFIG_ENABLE_WIFI
    netif = &eth_mac;
#endif

    Platform::CopyString(ifp->Name, netif->name);
    ifp->name          = CharSpan::fromCharString(ifp->Name);
    ifp->isOperational = true;
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    ifp->type          = EMBER_ZCL_INTERFACE_TYPE_ENUM_WI_FI;
#else
    ifp->type          = EMBER_ZCL_INTERFACE_TYPE_ENUM_ETHERNET;
#endif
    ifp->offPremiseServicesReachableIPv4.SetNull();
    ifp->offPremiseServicesReachableIPv6.SetNull();

    memcpy(ifp->MacAddress, netif->hwaddr, sizeof(netif->hwaddr));
    ifp->hardwareAddress = ByteSpan(ifp->MacAddress, sizeof(netif->hwaddr));

    memcpy(ifp->Ipv4AddressesBuffer[0], netif_ip_addr4(netif), kMaxIPv4AddrSize);
    ifp->Ipv4AddressSpans[0] = ByteSpan(ifp->Ipv4AddressesBuffer[0], kMaxIPv4AddrSize);
    ifp->IPv4Addresses       = chip::app::DataModel::List<chip::ByteSpan>(ifp->Ipv4AddressSpans, 1);

    int addr_count = 0;
    for (size_t i = 0; (i < LWIP_IPV6_NUM_ADDRESSES) && (i < kMaxIPv6AddrCount); i++)
    {
        if (!ip6_addr_isany(&(netif->ip6_addr[i].u_addr.ip6)))
        {
            memcpy(ifp->Ipv6AddressesBuffer[addr_count], &(netif->ip6_addr[i].u_addr.ip6), sizeof(ip6_addr_t));
            ifp->Ipv6AddressSpans[addr_count] = ByteSpan(ifp->Ipv6AddressesBuffer[addr_count], kMaxIPv6AddrSize);
        }
    }
    ifp->IPv6Addresses = chip::app::DataModel::List<chip::ByteSpan>(ifp->Ipv6AddressSpans, addr_count);

#endif

    *netifpp = ifp;

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

} // namespace DeviceLayer
} // namespace chip
