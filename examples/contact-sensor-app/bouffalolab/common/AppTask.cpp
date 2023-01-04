/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

#include "AppConfig.h"
#include "LEDWidget.h"

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/util/attribute-storage.h>

#include <app/server/Dnssd.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ErrorStr.h>
#include <system/SystemClock.h>
#ifdef OTA_ENABLED
#include "OTAConfig.h"
#endif // OTA_ENABLED

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <NetworkCommissioningDriver.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <route_hook/bl_route_hook.h>
#endif
#include <PlatformManagerImpl.h>

#if HEAP_MONITORING
#include "MemMonitoring.h"
#endif

#if CHIP_ENABLE_OPENTHREAD
#include <ThreadStackManagerImpl.h>
#include <platform/OpenThread/OpenThreadUtils.h>
#include <platform/ThreadStackManager.h>
#include <utils_list.h>
#endif

#if PW_RPC_ENABLED
#include "PigweedLogger.h"
#include "Rpc.h"
#endif

#if CONFIG_ENABLE_CHIP_SHELL
#include <ChipShellCollection.h>
#include <lib/shell/Engine.h>
#endif

#if CONFIG_ENABLE_CHIP_SHELL || PW_RPC_ENABLED
#include "uart.h"
#endif

extern "C" {
#include "board.h"
#include <bl_gpio.h>
#include <easyflash.h>
#include <hal_gpio.h>
#include <hosal_gpio.h>
}

#include "AppTask.h"

namespace {

Identify sIdentify = {
    APP_LIGHT_ENDPOINT_ID,
    AppTask::IdentifyStartHandler,
    AppTask::IdentifyStopHandler,
    EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_VISIBLE_LIGHT,
};

} // namespace

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

#if CONFIG_ENABLE_CHIP_SHELL
using namespace chip::Shell;
#endif

AppTask AppTask::sAppTask;
StackType_t AppTask::appStack[APP_TASK_STACK_SIZE / sizeof(StackType_t)];
StaticTask_t AppTask::appTaskStruct;

void PlatformManagerImpl::PlatformInit(void)
{
#if CONFIG_ENABLE_CHIP_SHELL || PW_RPC_ENABLED
    uartInit();
#endif

#if PW_RPC_ENABLED
    PigweedLogger::pw_init();
#elif CONFIG_ENABLE_CHIP_SHELL
    AppTask::StartAppShellTask();
#endif

#if HEAP_MONITORING
    MemMonitoring::startHeapMonitoring();
#endif

    ChipLogProgress(NotSpecified, "Initializing CHIP stack");
    CHIP_ERROR ret = PlatformMgr().InitChipStack();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().InitChipStack() failed");
        appError(ret);
    }

    chip::DeviceLayer::ConnectivityMgr().SetBLEDeviceName("MatterLight");

#if CHIP_ENABLE_OPENTHREAD

#if CONFIG_ENABLE_CHIP_SHELL
    cmd_otcli_init();
#endif

    ChipLogProgress(NotSpecified, "Initializing OpenThread stack");
    ret = ThreadStackMgr().InitThreadStack();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "ThreadStackMgr().InitThreadStack() failed");
        appError(ret);
    }

#if CHIP_DEVICE_CONFIG_THREAD_FTD
    ret = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_Router);
#else
    ret = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
#endif
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "ConnectivityMgr().SetThreadDeviceType() failed");
        appError(ret);
    }

#elif CHIP_DEVICE_CONFIG_ENABLE_WIFI

    ret = sWiFiNetworkCommissioningInstance.Init();
    if (CHIP_NO_ERROR != ret)
    {
        ChipLogError(NotSpecified, "sWiFiNetworkCommissioningInstance.Init() failed");
    }
#endif

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    // Initialize device attestation config
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

#if CHIP_DEVICE_CONFIG_ENABLE_EXTENDED_DISCOVERY
    chip::app::DnssdServer::Instance().SetExtendedDiscoveryTimeoutSecs(EXT_DISCOVERY_TIMEOUT_SECS);
#endif

    // Init ZCL Data Model
    static chip::CommonCaseDeviceServerInitParams initParams;
    (void) initParams.InitializeStaticResourcesBeforeServerInit();

    ret = chip::Server::GetInstance().Init(initParams);
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "chip::Server::GetInstance().Init(initParams) failed");
        appError(ret);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

#if CHIP_ENABLE_OPENTHREAD
    ChipLogProgress(NotSpecified, "Starting OpenThread task");
    // Start OpenThread task
    ret = ThreadStackMgrImpl().StartThreadTask();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "ThreadStackMgr().StartThreadTask() failed");
        appError(ret);
    }
#endif

    ConfigurationMgr().LogDeviceConfig();

    PrintOnboardingCodes(chip::RendezvousInformationFlag(chip::RendezvousInformationFlag::kBLE));
    PlatformMgr().AddEventHandler(AppTask::ChipEventHandler, 0);

#ifdef OTA_ENABLED
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OTAConfig::Init();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
#endif // OTA_ENABLED

#if PW_RPC_ENABLED
    chip::rpc::Init();
#endif

    GetAppTask().PostEvent(AppTask::APP_EVENT_TIMER);

    vTaskResume(GetAppTask().sAppTaskHandle);
}

void StartAppTask(void)
{
    GetAppTask().sAppTaskHandle = xTaskCreateStatic(GetAppTask().AppTaskMain, APP_TASK_NAME, ArraySize(GetAppTask().appStack), NULL,
                                                    APP_TASK_PRIORITY, GetAppTask().appStack, &GetAppTask().appTaskStruct);
    if (GetAppTask().sAppTaskHandle == NULL)
    {
        ChipLogError(NotSpecified, "Failed to create app task");
        appError(APP_ERROR_EVENT_QUEUE_FAILED);
    }
}

#if CONFIG_ENABLE_CHIP_SHELL
void AppTask::AppShellTask(void * args)
{
    Engine::Root().RunMainLoop();
}

CHIP_ERROR AppTask::StartAppShellTask()
{
    static TaskHandle_t shellTask;

    Engine::Root().Init();

    cmd_misc_init();

    xTaskCreate(AppTask::AppShellTask, "chip_shell", 1024 / sizeof(configSTACK_DEPTH_TYPE), NULL, APP_TASK_PRIORITY, &shellTask);

    return CHIP_NO_ERROR;
}
#endif

void AppTask::PostEvent(app_event_t event)
{
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPrioTaskWoken = pdFALSE;
        xTaskNotifyFromISR(sAppTaskHandle, event, eSetBits, &higherPrioTaskWoken);
    }
    else
    {
        xTaskNotify(sAppTaskHandle, event, eSetBits);
    }
}

void AppTask::AppTaskMain(void * pvParameter)
{
    app_event_t appEvent;
    bool isStateReady = false;

    ButtonInit();

    ChipLogProgress(NotSpecified, "Starting Platform Manager Event Loop");
    CHIP_ERROR ret = PlatformMgr().StartEventLoopTask();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().StartEventLoopTask() failed");
        appError(ret);
    }
    vTaskSuspend(NULL);

#ifndef LED_BTN_RESET
    GetAppTask().mButtonPressedTime = chip::System::SystemClock().GetMonotonicMilliseconds64().count() + 1;
    if (ConnectivityMgr().IsThreadProvisioned())
    {
        GetAppTask().PostEvent(APP_EVENT_SYS_PROVISIONED);
    }
#endif

    GetAppTask().mIsConnected = false;

    ChipLogProgress(NotSpecified, "App Task started, with heap %d left\r\n", xPortGetFreeHeapSize());

    while (true)
    {
        appEvent                 = APP_EVENT_NONE;
        BaseType_t eventReceived = xTaskNotifyWait(0, APP_EVENT_ALL_MASK, (uint32_t *) &appEvent, portMAX_DELAY);

        if (eventReceived)
        {
            PlatformMgr().LockChipStack();

            if (APP_EVENT_IDENTIFY_MASK & appEvent)
            {
                IdentifyHandleOp(appEvent);
            }

            if (APP_EVENT_FACTORY_RESET & appEvent)
            {
                DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
            }

            PlatformMgr().UnlockChipStack();
        }
    }
}

void AppTask::ChipEventHandler(const ChipDeviceEvent * event, intptr_t arg)
{
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:

        if (ConnectivityMgr().NumBLEConnections())
        {
            GetAppTask().PostEvent(APP_EVENT_SYS_BLE_CONN);
        }
        else
        {
            GetAppTask().PostEvent(APP_EVENT_SYS_BLE_ADV);
            GetAppTask().mIsConnected = ConnectivityMgr().IsThreadAttached();
        }
        ChipLogProgress(NotSpecified, "BLE adv changed, connection number: %d\r\n", ConnectivityMgr().NumBLEConnections());
        break;

    case DeviceEventType::kThreadStateChange:

        ChipLogProgress(NotSpecified, "Thread state changed, IsThreadAttached: %d\r\n", ConnectivityMgr().IsThreadAttached());
        if (!GetAppTask().mIsConnected && ConnectivityMgr().IsThreadAttached())
        {
            GetAppTask().PostEvent(APP_EVENT_SYS_PROVISIONED);
            GetAppTask().mIsConnected = true;
        }
        break;

    case DeviceEventType::kFailSafeTimerExpired:

        GetAppTask().mIsConnected = ConnectivityMgr().IsThreadAttached();

        break;
    default:
        break;
    }
}

void AppTask::IdentifyStartHandler(Identify *)
{
    GetAppTask().PostEvent(APP_EVENT_IDENTIFY_START);
}

void AppTask::IdentifyStopHandler(Identify *)
{
    GetAppTask().PostEvent(APP_EVENT_IDENTIFY_STOP);
}

void AppTask::IdentifyHandleOp(app_event_t event)
{
    static uint32_t identifyState = 0;

    if (APP_EVENT_IDENTIFY_START & event)
    {
        identifyState = 1;
        ChipLogProgress(NotSpecified, "identify start");
    }

    if ((APP_EVENT_IDENTIFY_IDENTIFY & event) && identifyState)
    {
        ChipLogProgress(NotSpecified, "identify");
    }

    if (APP_EVENT_IDENTIFY_STOP & event)
    {
        identifyState = 0;
        ChipLogProgress(NotSpecified, "identify stop");
    }
}

void AppTask::ButtonEventHandler(uint8_t btnIdx, uint8_t btnAction)
{
    GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
}

hosal_gpio_dev_t gpio_key = { .port = LED_BTN_RESET, .config = INPUT_HIGH_IMPEDANCE, .priv = NULL };

void AppTask::ButtonInit(void)
{
    GetAppTask().mButtonPressedTime     = 0;
    GetAppTask().mIsFactoryResetIndicat = false;

    hosal_gpio_init(&gpio_key);
    hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_POS_PULSE, GetAppTask().ButtonEventHandler, NULL);
}

bool AppTask::ButtonPressed(void)
{
    uint8_t val = 1;
    hosal_gpio_input_get(&gpio_key, &val);
    return val == 1;
}

void AppTask::ButtonEventHandler(void * arg)
{
    uint32_t presstime;

    if (ButtonPressed())
    {
        hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_NEG_LEVEL, GetAppTask().ButtonEventHandler, NULL);

        GetAppTask().mButtonPressedTime = chip::System::SystemClock().GetMonotonicMilliseconds64().count();
        GetAppTask().PostEvent(APP_EVENT_BTN_FACTORY_RESET_PRESS);
    }
    else
    {
        hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_POS_PULSE, GetAppTask().ButtonEventHandler, NULL);

        if (GetAppTask().mButtonPressedTime)
        {

            presstime = chip::System::SystemClock().GetMonotonicMilliseconds64().count() - GetAppTask().mButtonPressedTime;
            if (presstime >= APP_BUTTON_PRESS_LONG)
            {
                GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
            }
            else if (presstime <= APP_BUTTON_PRESS_SHORT && presstime >= APP_BUTTON_PRESS_JITTER)
            {
                GetAppTask().PostEvent(APP_EVENT_BTN_SHORT);
            }
            else
            {
                GetAppTask().PostEvent(APP_EVENT_BTN_FACTORY_RESET_CANCEL);
            }
        }

        GetAppTask().mButtonPressedTime = 0;
    }
}
