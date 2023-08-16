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

#include <crypto/CHIPCryptoPAL.h>
#include <platform/FreeRTOS/SystemTimeSupport.h>
#include <platform/PlatformManager.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>
#include <platform/internal/GenericPlatformManagerImpl_FreeRTOS.ipp>

#include <lwip/tcpip.h>
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <platform/bouffalolab/BL702/wifi_mgmr_portable.h>
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD || ENABLE_OPENTHREAD_BORDER_ROUTER
#include <openthread_port.h>
#include <utils_list.h>
#endif

#if ENABLE_OPENTHREAD_BORDER_ROUTER
#include <openthread/thread.h>

#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/ip6.h>
#include <openthread_br.h>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
#include <platform/bouffalolab/BL702/EthernetInterface.h>
#endif // CHIP_DEVICE_CONFIG_ENABLE_ETHERNET

extern "C" {
#include <bl_sec.h>
}

namespace chip {
namespace DeviceLayer {

extern "C" void bl_rand_stream(unsigned char *, int);

static int app_entropy_source(void * data, unsigned char * output, size_t len, size_t * olen)
{
    bl_rand_stream(output, len);
    if (olen)
    {
        *olen = len;
    }

    return 0;
}

CHIP_ERROR PlatformManagerImpl::_InitChipStack(void)
{
    CHIP_ERROR err;
    TaskHandle_t backup_eventLoopTask;

    // Initialize LwIP.
    tcpip_init(NULL, NULL);
    
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    otRadio_opt_t opt;
    opt.bf.isFtd = true;
    opt.bf.isCoexEnable = true;

    ot_alarmInit();
    ot_radioInit(opt);
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD

    ReturnErrorOnFailure(System::Clock::InitClock_RealTime());

    err = chip::Crypto::add_entropy_source(app_entropy_source, NULL, 16);
    SuccessOrExit(err);

    // Call _InitChipStack() on the generic implementation base class
    // to finish the initialization process.
    /** weiyin, backup mEventLoopTask which is reset in _InitChipStack */
    backup_eventLoopTask = Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::mEventLoopTask;
    err                  = Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::_InitChipStack();
    SuccessOrExit(err);
    Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::mEventLoopTask = backup_eventLoopTask;

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    wifi_start_firmware_task();
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

#if CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
    ethernetInterface_init();
#endif // CHIP_DEVICE_CONFIG_ENABLE_ETHERNET

#if ENABLE_OPENTHREAD_BORDER_ROUTER
    otRadio_opt_t opt;
    opt.bf.isFtd = true;
    opt.bf.isCoexEnable = true;
    opt.bf.isLinkMetricEnable = true;

    otrStart(opt);
#endif

exit:
    return err;
}

} // namespace DeviceLayer
} // namespace chip

#if ENABLE_OPENTHREAD_BORDER_ROUTER

#define THREAD_CHANNEL      11
#define THREAD_PANID        0x1234
#define THREAD_EXTPANID     {0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22}
#define THREAD_NETWORK_KEY  {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}

extern "C" void otr_start_default(void) 
{
    otOperationalDataset ds;
    uint8_t default_network_key[] = THREAD_NETWORK_KEY;
    uint8_t default_extend_panid[] = THREAD_EXTPANID;

    if (!otDatasetIsCommissioned(otrGetInstance())) {

        if (OT_ERROR_NONE != otDatasetCreateNewNetwork(otrGetInstance(), &ds)) {
            printf("Failed to create dataset for Thread Network\r\n");
        }

        memcpy(&ds.mNetworkKey, default_network_key, sizeof(default_network_key));
        strncpy(ds.mNetworkName.m8, "OTBR-BL702", sizeof(ds.mNetworkName.m8));
        memcpy(&ds.mExtendedPanId, default_extend_panid, sizeof(default_extend_panid));
        ds.mChannel = THREAD_CHANNEL;
        ds.mPanId = THREAD_PANID;
        
        if (OT_ERROR_NONE != otDatasetSetActive(otrGetInstance(), &ds)) {
            printf("Failed to set active dataset\r\n");
        }
    }

    otIp6SetEnabled(otrGetInstance(), true);
    otThreadSetEnabled(otrGetInstance(), true);

    ChipLogProgress(DeviceLayer, "Start Thread network with default configuration.");
}

extern "C" void otrInitUser(otInstance * instance)
{
    otr_start_default();

    otbr_netif_init();
}
#endif
