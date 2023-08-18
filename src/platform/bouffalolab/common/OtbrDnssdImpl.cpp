/*
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
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

#include "platform/CHIPDeviceLayer.h"
#include "lib/dnssd/platform/Dnssd.h"

#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <openthread/thread.h>
#include <openthread_br.h>
#include <matter_mdns.h>

using namespace ::chip::DeviceLayer;

namespace chip {
namespace Dnssd {

static constexpr uint32_t kTimeoutMilli = 3000;
static constexpr size_t kMaxResults     = 10;

CHIP_ERROR DnssdErrorCode(otbrError error) 
{
    switch (error) {
    case OTBR_ERROR_NONE:
        return CHIP_NO_ERROR;
    case OTBR_ERROR_MDNS:
        return CHIP_ERROR_INTERNAL;
    case OTBR_ERROR_INVALID_ARGS:
        return CHIP_ERROR_INVALID_ARGUMENT;
    case OTBR_ERROR_NO_MEM:
        return CHIP_ERROR_NO_MEMORY;
    default:
        return CHIP_ERROR_INTERNAL;
    }
}

CHIP_ERROR ChipDnssdInit(DnssdAsyncReturnCallback initCallback, DnssdAsyncReturnCallback errorCallback, void * context)
{
    initCallback(context, CHIP_NO_ERROR);
    return CHIP_NO_ERROR;
}

void ChipDnssdShutdown() {}

CHIP_ERROR ChipDnssdPublishService(const DnssdService * service, DnssdPublishCallback callback, void * context)
{

    otbrError error = OTBR_ERROR_NONE;
    const char * lProtocol = (service->mProtocol == DnssdServiceProtocol::kDnssdProtocolTcp ? "_tcp" : "_udp");
    txt_item_t *lTxtItem = NULL;
    int lTxtItemNum = 0;

    ChipLogProgress(DeviceLayer, "dnssd publish service %s.%s.%s @ %s", service->mName, service->mType, lProtocol, service->mHostName);

    if (service->mTextEntries && service->mTextEntrySize) {
        lTxtItem = static_cast<txt_item_t *>(chip::Platform::MemoryCalloc(service->mTextEntrySize, sizeof(txt_item_t)));
        lTxtItemNum = service->mTextEntrySize;
        if (nullptr == lTxtItem) {
            return CHIP_ERROR_NO_MEMORY;
        }

        for (size_t i = 0; i < service->mTextEntrySize; i++)
        {
            lTxtItem[i].mKey = service->mTextEntries[i].mKey;
            lTxtItem[i].mData = service->mTextEntries[i].mData;
            lTxtItem[i].mDataSize = service->mTextEntries[i].mDataSize;
        }
    }

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (ConnectivityMgr().IsWiFiStationConnected())
    {
        error = WiFiDnssdPublishService(service->mHostName, service->mName, service->mType, lProtocol,
            service->mPort, lTxtItem, lTxtItemNum, service->mSubTypes, service->mSubTypeSize);
    }
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
    error = WiFiDnssdPublishService(service->mHostName, service->mName, service->mType, lProtocol,
        service->mPort, lTxtItem, lTxtItemNum, service->mSubTypes, service->mSubTypeSize);
#endif

    if (lTxtItem) {
        free(lTxtItem);
    }

    return DnssdErrorCode(error);
}

CHIP_ERROR ChipDnssdRemoveServices()
{
    otbrError error = OTBR_ERROR_NONE;

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (ConnectivityMgr().IsWiFiStationConnected())
    {
        error = WiFiDnssdRemoveServices();
    }
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
    error = WiFiDnssdRemoveServices();
#endif

    return DnssdErrorCode(error);
}

CHIP_ERROR ChipDnssdFinalizeServiceUpdate()
{
    return CHIP_NO_ERROR;
}

void mdns_browse_callback(int aNumResult, mdns_discover_result_t *aResult, mdns_discover_param_t * aDiscoverParam)
{
    int iResult = 0;
    DnssdService * lService = nullptr;
    void * lContext;
    CHIP_ERROR error = CHIP_NO_ERROR;

    if (nullptr == aDiscoverParam || aDiscoverParam->arg2) {
        return;
    }
    lContext = static_cast<void *>(aDiscoverParam->arg1);

    do {
        if (0 == aNumResult) {
            error = CHIP_NO_ERROR;
            break;
        }
        if (aNumResult && nullptr == aResult) {
            error = CHIP_ERROR_NO_MEMORY;
            break;
        }

        lService = static_cast<DnssdService *>(chip::Platform::MemoryAlloc(aNumResult * sizeof(DnssdService)));
        if (nullptr == lService) {
            error = CHIP_ERROR_NO_MEMORY;
            break;
        }

        for (iResult = 0; iResult < aNumResult; iResult++)
        {
            Platform::CopyString(lService[iResult].mName, aResult[iResult].mInstanceName);
            Platform::CopyString(lService[iResult].mType, aResult[iResult].mType);
            lService[iResult].mProtocol      = strcmp("_tcp", aResult[iResult].mProtocol) == 0? DnssdServiceProtocol::kDnssdProtocolTcp : DnssdServiceProtocol::kDnssdProtocolUdp;
            if (aDiscoverParam->mAddressType == IPADDR_TYPE_ANY) {
                lService[iResult].mAddressType = Inet::IPAddressType::kAny;
            }
            else if (aDiscoverParam->mAddressType == IPADDR_TYPE_V6) {
                lService[iResult].mAddressType = Inet::IPAddressType::kIPv6;
            }
            else {
                lService[iResult].mAddressType = Inet::IPAddressType::kIPv4;
            }
            Platform::CopyString(lService[iResult].mHostName, aResult[iResult].mHostName);

            lService[iResult].mTransportType = lService[iResult].mAddressType; 
            lService[iResult].mPort          = aResult[iResult].mPort;
            lService[iResult].mInterface     = Inet::InterfaceId::Null();
            lService[iResult].mTextEntrySize = aResult[iResult].mTxtItemNum;
            lService[iResult].mTextEntries = static_cast<TextEntry *>(chip::Platform::MemoryAlloc(sizeof(TextEntry) * aResult[iResult].mTxtItemNum));
            if (nullptr == lService[iResult].mTextEntries) {
                break;
            }
            for (size_t i = 0; i < lService[iResult].mTextEntrySize; i ++) {
                lService[iResult].mTextEntries[i].mKey = aResult[iResult].mTxtItems[i].mKey;
                lService[iResult].mTextEntries[i].mData = aResult[iResult].mTxtItems[i].mData;
                lService[iResult].mTextEntries[i].mDataSize = aResult[iResult].mTxtItems[i].mDataSize;
            }
            lService[iResult].mSubTypes      = NULL;
            lService[iResult].mSubTypeSize   = 0;
            if (aResult[iResult].mAddress && aResult[iResult].mAddressNum)
            {
                Inet::IPAddress ipaddr(aResult[iResult].mAddress[0]);
                lService[iResult].mAddress.SetValue(ipaddr);
            }
        }
    } while (0);

    if (aDiscoverParam->mIsBrowse) {
        DnssdBrowseCallback lCallback = reinterpret_cast<DnssdBrowseCallback>(aDiscoverParam->arg2);
        if (lService && iResult) {
            lCallback(lContext, lService, iResult, true, CHIP_NO_ERROR);
        }
        else {
            lCallback(lContext, nullptr, 0, true, CHIP_NO_ERROR);
        }
    }
    else {
        DnssdResolveCallback lCallback = reinterpret_cast<DnssdResolveCallback>(aDiscoverParam->arg2);
        if (lService && iResult && aResult[0].mAddress && aResult[0].mAddressNum) {
            /** Only one result expected for resovling */
            Inet::IPAddress * lIpAddresses = static_cast<Inet::IPAddress *>(Platform::MemoryCalloc(aResult[0].mAddressNum, sizeof(Inet::IPAddress)));
            
            for (int i = 0; i < aResult[0].mAddressNum; i ++) {
                Inet::IPAddress ipaddr(aResult[0].mAddress[i]);
                memcpy(lIpAddresses[i].Addr, ipaddr.Addr, sizeof(ipaddr.Addr));
            }
            lCallback(lContext, lService, Span<Inet::IPAddress>(lIpAddresses, aResult[0].mAddressNum), CHIP_NO_ERROR);
        }
        else {
            lCallback(lContext, nullptr, Span<Inet::IPAddress>(nullptr, 0), CHIP_NO_ERROR);
        }
    }

    chip::Platform::MemoryFree(lService);
}

CHIP_ERROR ChipDnssdBrowse(const char * type, DnssdServiceProtocol protocol, chip::Inet::IPAddressType addressType,
                           chip::Inet::InterfaceId interface, DnssdBrowseCallback callback, void * context,
                           intptr_t * browseIdentifier)
{
    const char * lProtocol = (protocol == DnssdServiceProtocol::kDnssdProtocolTcp ? "_tcp" : "_udp");
    otbrError error = OTBR_ERROR_NONE;
    enum lwip_ip_addr_type lAddrType;

    ChipLogProgress(DeviceLayer, "Dnssd browse *.%s.%s", type, lProtocol);

    if (Inet::IPAddressType::kAny == addressType) {
        lAddrType = IPADDR_TYPE_ANY;
    }
    else if (Inet::IPAddressType::kIPv6 == addressType) {
        lAddrType = IPADDR_TYPE_V6;
    }
    else {
        lAddrType = IPADDR_TYPE_V4;
    }

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (ConnectivityMgr().IsWiFiStationConnected())
    {
        error = WiFiDnssdBrowse(type, lProtocol, lAddrType, mdns_browse_callback, kTimeoutMilli, kMaxResults, context, reinterpret_cast<void *>(callback));
    }
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
    error = WiFiDnssdBrowse(type, lProtocol, lAddrType, mdns_browse_callback, kTimeoutMilli, kMaxResults, context, reinterpret_cast<void *>(callback));
#endif

    return DnssdErrorCode(error);
}

CHIP_ERROR ChipDnssdStopBrowse(intptr_t browseIdentifier)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}


CHIP_ERROR ChipDnssdResolve(DnssdService * service, chip::Inet::InterfaceId interface, DnssdResolveCallback callback,
                            void * context)
{
    otbrError error = OTBR_ERROR_NONE;
    const char * lProtocol = (service->mProtocol == DnssdServiceProtocol::kDnssdProtocolTcp ? "_tcp" : "_udp");

    ChipLogProgress(DeviceLayer, "Dnssd resolve %s.%s.%s", service->mName ? service->mName: "null", service->mType, lProtocol);

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (ConnectivityMgr().IsWiFiStationConnected())
    {
        error = WiFiDnssdResolve(service->mName, service->mType, lProtocol, mdns_browse_callback, kTimeoutMilli, 1, context, reinterpret_cast<void *>(callback));
    }
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
    error = WiFiDnssdResolve(service->mName, service->mType, lProtocol, mdns_browse_callback, kTimeoutMilli, 1, context, reinterpret_cast<void *>(callback));
#endif
    
    return DnssdErrorCode(error);
}

void ChipDnssdResolveNoLongerNeeded(const char * instanceName) {}

CHIP_ERROR ChipDnssdReconfirmRecord(const char * hostname, chip::Inet::IPAddress address, chip::Inet::InterfaceId interface)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

} // namespace Dnssd
} // namespace chip
