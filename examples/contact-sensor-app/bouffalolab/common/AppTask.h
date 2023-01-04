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

#pragma once

#include <stdbool.h>
#include <stdint.h>

// #include "AppEvent.h"

#include "FreeRTOS.h"
#include "timers.h"
#include <platform/CHIPDeviceLayer.h>

using namespace ::chip;
using namespace ::chip::DeviceLayer;

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED CHIP_APPLICATION_ERROR(0x06)

struct Identify;

class AppTask
{
public:
    enum app_event_t
    {
        APP_EVENT_NONE = 0x00000000,

        APP_EVENT_BTN_PRESS_SHORT                   = 0x00000010,
        APP_EVENT_BTN_PRESS_FACTORY_RESET_CANCEL    = 0x00000020,
        APP_EVENT_BTN_PRESS_FACTORY_RESET           = 0x00000040,
        APP_EVENT_BTN_ALL_MASK          =
            APP_EVENT_BTN_PRESS_SHORT | APP_EVENT_BTN_PRESS_FACTORY_RESET_CANCEL | APP_EVENT_BTN_PRESS_FACTORY_RESET,

        APP_EVENT_SYS_BLE_ADV           = 0x00001000,
        APP_EVENT_SYS_BLE_CONN          = 0x00002000,
        APP_EVENT_SYS_PROVISIONED       = 0x00004000,
        APP_EVENT_SYS_LIGHT_TOGGLE      = 0x00008000,

        APP_EVENT_SYS_ALL_MASK          =
            APP_EVENT_SYS_BLE_ADV | APP_EVENT_SYS_BLE_CONN | APP_EVENT_SYS_PROVISIONED,


        APP_EVENT_IDENTIFY_START        = 0x01000000,
        APP_EVENT_IDENTIFY_IDENTIFY     = 0x02000000,
        APP_EVENT_IDENTIFY_STOP         = 0x04000000,
        APP_EVENT_IDENTIFY_MASK         = 
            APP_EVENT_IDENTIFY_START | APP_EVENT_IDENTIFY_IDENTIFY | APP_EVENT_IDENTIFY_STOP,

        APP_EVENT_ALL_MASK              = 
            APP_EVENT_BTN_ALL_MASK | APP_EVENT_SYS_ALL_MASK | APP_EVENT_IDENTIFY_MASK,
    };

    void SetEndpointId(EndpointId endpointId)
    {
        if (mEndpointId != (EndpointId) -1)
            mEndpointId = endpointId;
    }

    EndpointId GetEndpointId(void) { return mEndpointId; }
    void PostEvent(app_event_t event);
    void ButtonEventHandler(uint8_t btnIdx, uint8_t btnAction);

    static void IdentifyStartHandler(Identify *);
    static void IdentifyStopHandler(Identify *);
    static void IdentifyHandleOp(app_event_t event);

private:
    friend AppTask & GetAppTask(void);
    friend void StartAppTask(void);
    friend PlatformManagerImpl;

    static void ChipEventHandler(const ChipDeviceEvent * event, intptr_t arg);

    static void ButtonInit(void);
    static bool ButtonPressed(void);
    static void ButtonEventHandler(void * arg);

    static void AppTaskMain(void * pvParameter);

    static CHIP_ERROR StartAppShellTask();
    static void AppShellTask(void * args);

    EndpointId mEndpointId = (EndpointId) 1;
    TaskHandle_t sAppTaskHandle;
    QueueHandle_t sAppEventQueue;

    uint64_t mButtonPressedTime;
    bool mIsFactoryResetIndicat;
    bool mIsConnected;

    static StackType_t appStack[APP_TASK_STACK_SIZE / sizeof(StackType_t)];
    static StaticTask_t appTaskStruct;
    static AppTask sAppTask;
};

inline AppTask & GetAppTask(void)
{
    return AppTask::sAppTask;
}

void StartAppTask();
