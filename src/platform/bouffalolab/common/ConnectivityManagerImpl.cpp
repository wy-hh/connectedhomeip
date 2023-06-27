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
#include <platform/DiagnosticDataProvider.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/bouffalolab/common/BLConfig.h>

#include <platform/internal/GenericConnectivityManagerImpl_UDP.ipp>

#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.ipp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <platform/internal/GenericConnectivityManagerImpl_WiFi.ipp>
#ifdef BL602
#include <platform/bouffalolab/BL602/NetworkCommissioningDriver.h>
#endif
#ifdef BL702
#include <platform/bouffalolab/BL702/NetworkCommissioningDriver.h>
#endif
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include <platform/internal/GenericConnectivityManagerImpl_BLE.ipp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/internal/GenericConnectivityManagerImpl_Thread.ipp>
#endif

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::TLV;
using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;

CHIP_ERROR ConnectivityManagerImpl::_Init()
{
    // Initialize the generic base classes that require it.
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    GenericConnectivityManagerImpl_Thread<ConnectivityManagerImpl>::_Init();
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    ReturnErrorOnFailure(InitWiFi());
    mWiFiStationState = ConnectivityManager::kWiFiStationState_NotConnected;
    ReturnErrorOnFailure(SetWiFiStationMode(kWiFiStationMode_Enabled));
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

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
bool ConnectivityManagerImpl::_IsWiFiStationEnabled(void)
{
    return GetWiFiStationMode() == kWiFiStationMode_Enabled;
}

bool ConnectivityManagerImpl::_IsWiFiStationProvisioned(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    char ssid[64]  = { 0 };
    char psk[64]   = { 0 };
    size_t ssidLen = 0;
    size_t pskLen  = 0;

    err = PersistedStorage::KeyValueStoreMgr().Get(BLConfig::kConfigKey_WiFiSSID, (void *) ssid, 64, &ssidLen, 0);
    SuccessOrExit(err);

    err = PersistedStorage::KeyValueStoreMgr().Get(BLConfig::kConfigKey_WiFiPassword, (void *) psk, 64, &pskLen, 0);
    SuccessOrExit(err);

    return (ssidLen != 0);
exit:
    return false;
}

CHIP_ERROR ConnectivityManagerImpl::_SetWiFiStationMode(WiFiStationMode val)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrExit(val != kWiFiStationMode_NotSupported, err = CHIP_ERROR_INVALID_ARGUMENT);

    if (val != kWiFiStationMode_ApplicationControlled)
    {
        DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);
    }

    if (mWiFiStationMode != val)
    {
        ChipLogProgress(DeviceLayer, "WiFi station mode change: %s -> %s", WiFiStationModeToStr(mWiFiStationMode),
                        WiFiStationModeToStr(val));
    }

    mWiFiStationMode = val;

exit:
    return err;
}

void ConnectivityManagerImpl::ChangeWiFiStationState(WiFiStationState newState)
{
    if (mWiFiStationState != newState)
    {
        ChipLogProgress(DeviceLayer, "WiFi station state change: %s -> %s", WiFiStationStateToStr(mWiFiStationState),
                        WiFiStationStateToStr(newState));
        mWiFiStationState = newState;
        ConnectivityMgrImpl().DriveStationState();
        SystemLayer().ScheduleLambda([]() { NetworkCommissioning::BLWiFiDriver::GetInstance().OnNetworkStatusChange(); });
    }
}

void ConnectivityManagerImpl::_ClearWiFiStationProvision(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    err = PersistedStorage::KeyValueStoreMgr().Delete(BLConfig::kConfigKey_WiFiSSID);
    SuccessOrExit(err);

    err =PersistedStorage::KeyValueStoreMgr().Delete(BLConfig::kConfigKey_WiFiPassword);
    SuccessOrExit(err);
    
exit:
    return;
}

void ConnectivityManagerImpl::_OnWiFiStationProvisionChange(void) 
{
    DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);
}

CHIP_ERROR ConnectivityManagerImpl::ConnectProvisionedWiFiNetwork(void)
{
    char ssid[64]  = { 0 };
    char psk[64]   = { 0 };
    size_t ssidLen = 0;
    size_t pskLen  = 0;

    ReturnErrorOnFailure(
        PersistedStorage::KeyValueStoreMgr().Get(BLConfig::kConfigKey_WiFiSSID, (void *) ssid, 64, &ssidLen, 0));
    ReturnErrorOnFailure(
        PersistedStorage::KeyValueStoreMgr().Get(BLConfig::kConfigKey_WiFiPassword, (void *) psk, 64, &pskLen, 0));

    NetworkCommissioning::BLWiFiDriver::GetInstance().ConnectWiFiNetwork(ssid, ssidLen, psk, pskLen);

    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::OnWiFiStationConnected()
{
    ChipDeviceEvent event;
    event.Type                          = DeviceEventType::kWiFiConnectivityChange;
    event.WiFiConnectivityChange.Result = kConnectivity_Established;
    PlatformMgr().PostEventOrDie(&event);

    WiFiDiagnosticsDelegate * delegate = GetDiagnosticDataProvider().GetWiFiDiagnosticsDelegate();
    if (delegate)
    {
        delegate->OnConnectionStatusChanged(
            chip::to_underlying(chip::app::Clusters::WiFiNetworkDiagnostics::ConnectionStatusEnum::kConnected));
    }
}

void ConnectivityManagerImpl::OnIPv4AddressAvailable()
{
    ChipLogProgress(DeviceLayer, "IPv4 addr available.");

    ChipDeviceEvent event;
    event.Type                           = DeviceEventType::kInterfaceIpAddressChanged;
    event.InterfaceIpAddressChanged.Type = InterfaceIpChangeType::kIpV4_Assigned;
    PlatformMgr().PostEventOrDie(&event);
}

void ConnectivityManagerImpl::OnIPv6AddressAvailable()
{
    ChipLogProgress(DeviceLayer, "IPv6 addr available.");

    ChipDeviceEvent event;
    event.Type                           = DeviceEventType::kInterfaceIpAddressChanged;
    event.InterfaceIpAddressChanged.Type = InterfaceIpChangeType::kIpV6_Assigned;
    PlatformMgr().PostEventOrDie(&event);
}

void ConnectivityManagerImpl::DriveStationState() 
{
    ChipLogProgress(DeviceLayer, "DriveStationState: mWiFiStationState=%s", WiFiStationStateToStr(mWiFiStationState));
    switch(mWiFiStationState)
    {
        case ConnectivityManager::kWiFiStationState_NotConnected:
        {
            if (GetWiFiStationMode() == ConnectivityManager::kWiFiStationMode_Enabled && IsWiFiStationProvisioned())
            {
                ConnectProvisionedWiFiNetwork();
            }
        }
        break;
        case ConnectivityManager::kWiFiStationState_Connecting:
        {
            ChipLogProgress(DeviceLayer, "Wi-Fi station is connecting to AP");
        }
        break;
        case ConnectivityManager::kWiFiStationState_Connecting_Succeeded:
        {
            ChipLogProgress(DeviceLayer, "Wi-Fi station successfully connects to AP");
            mConnectivityFlag.ClearAll();
            mConnectivityFlag.Set(ConnectivityFlags::kAwaitingConnectivity);
        }
        break;
        case ConnectivityManager::kWiFiStationState_Connecting_Failed:
        {
            ChipLogProgress(DeviceLayer, "Wi-Fi station connecting failed");
            mConnectivityFlag.ClearAll();
            OnWiFiStationDisconnected();
            if (ConnectivityManager::kWiFiStationState_Connecting == mWiFiStationState) {
                SystemLayer().ScheduleLambda([]() { NetworkCommissioning::BLWiFiDriver::GetInstance().OnConnectWiFiNetwork(false); });
            }
        }
        break;
        case ConnectivityManager::kWiFiStationState_Connected:
        {
            ChipLogProgress(DeviceLayer, "Wi-Fi stattion connected.");
            OnWiFiStationConnected();
            SystemLayer().ScheduleLambda([]() { NetworkCommissioning::BLWiFiDriver::GetInstance().OnConnectWiFiNetwork(true); });
        }
        break;
        case ConnectivityManager::kWiFiStationState_Disconnecting:
        {
            ChipLogProgress(DeviceLayer, "Wi-Fi station is disconnecting to AP");
            mConnectivityFlag.ClearAll();
        }
        break;
        default:
        break;
    }
}

void ConnectivityManagerImpl::DriveStationState(::chip::System::Layer * aLayer, void * aAppState)
{
    ConnectivityMgrImpl().DriveStationState();
}

#endif

} // namespace DeviceLayer
} // namespace chip
