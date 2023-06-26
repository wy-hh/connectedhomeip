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

#include <platform/internal/GenericConnectivityManagerImpl_UDP.ipp>

#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.ipp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <platform/internal/GenericConnectivityManagerImpl_WiFi.ipp>
#ifdef BL602
#include <platform/bouffalolab/BL602/NetworkCommissioningDriver.h>
#endif
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include <platform/internal/BLEManager.h>
#include <platform/internal/GenericConnectivityManagerImpl_BLE.ipp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/internal/GenericConnectivityManagerImpl_Thread.ipp>
#elif defined (BL702)
extern "C" {
#include <eth_bd.h>
#include <lwip/netifapi.h>
#include <lwip/dhcp6.h>
static struct dhcp6 dhcp6_val;
}
#endif

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::TLV;
using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;

#if !CHIP_DEVICE_CONFIG_ENABLE_THREAD && defined (BL702)

void netif_status_callback(struct netif *netif)
{
    if (netif->flags & NETIF_FLAG_UP) {
        if(!ip4_addr_isany(netif_ip4_addr(netif))) {
            printf("IP: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
            printf("MASK: %s\r\n", ip4addr_ntoa(netif_ip4_netmask(netif)));
            printf("Gateway: %s\r\n", ip4addr_ntoa(netif_ip4_gw(netif)));
        }

        for (uint32_t i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i ++ ) {
            if (!ip6_addr_isany(netif_ip6_addr(netif, i))
                && ip6_addr_ispreferred(netif_ip6_addr_state(netif, i))) {

                const ip6_addr_t* ip6addr = netif_ip6_addr(netif, i);
                if (ip6_addr_isany(ip6addr)) {
                    continue;
                }

                if(ip6_addr_islinklocal(ip6addr)){
                    printf("LOCAL IP6 addr %s\r\n", ip6addr_ntoa(ip6addr));
                }
                else{
                    printf("GLOBAL IP6 addr %s\r\n", ip6addr_ntoa(ip6addr));

                    ConnectivityManagerImpl::OnIPv6AddressAvailable();
                } 
            }
        }
    }
    else {
        printf("interface is down status.\n");
    }
}

int ethernet_callback(eth_link_state val)
{
    switch(val){
    case ETH_INIT_STEP_LINKUP:
        printf("Ethernet link up\r\n");
        break;
    case ETH_INIT_STEP_READY:
        netifapi_netif_set_default(&eth_mac);
        netifapi_netif_set_up(&eth_mac);

        //netifapi_netif_set_up((struct netif *)&obj->netif);
        netif_create_ip6_linklocal_address(&eth_mac, 1);
        eth_mac.ip6_autoconfig_enabled = 1;
        dhcp6_set_struct(&eth_mac, &dhcp6_val);
        dhcp6_enable_stateless(&eth_mac);

        printf("start dhcp...\r\n");

        /* start dhcp */
        netifapi_dhcp_start(&eth_mac);
        break;
    case ETH_INIT_STEP_LINKDOWN:
        printf("Ethernet link down\r\n");
        break;
    }

    return 0;
}
#endif

CHIP_ERROR ConnectivityManagerImpl::_Init()
{
    // Initialize the generic base classes that require it.
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    GenericConnectivityManagerImpl_Thread<ConnectivityManagerImpl>::_Init();
#elif defined (BL702)
    netif_add(&eth_mac, NULL, NULL, NULL, NULL, eth_init, ethernet_input);
    
    ethernet_init(ethernet_callback);

    /* Set callback to be called when interface is brought up/down or address is changed while up */
    netif_set_status_callback(&eth_mac, netif_status_callback);
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    CHIP_ERROR err = SetWiFiStationMode(kWiFiStationMode_Enabled);
    NetworkCommissioning::BLWiFiDriver::GetInstance().ReConnectWiFiNetwork();

    ReturnErrorOnFailure(err);
#endif

    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
    // Forward the event to the generic base classes as needed.
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    GenericConnectivityManagerImpl_Thread<ConnectivityManagerImpl>::_OnPlatformEvent(event);
#endif
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI || (defined(BL702) && !CHIP_DEVICE_CONFIG_ENABLE_THREAD)
void ConnectivityManagerImpl::OnIPv6AddressAvailable(void)
{
    ChipLogProgress(DeviceLayer, "IPv6 addr available.");

    ChipDeviceEvent event;
    event.Type                           = DeviceEventType::kInterfaceIpAddressChanged;
    event.InterfaceIpAddressChanged.Type = InterfaceIpChangeType::kIpV6_Assigned;
    PlatformMgr().PostEventOrDie(&event);
}
#endif

} // namespace DeviceLayer
} // namespace chip
