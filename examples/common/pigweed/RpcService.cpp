/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include "RpcService.h"

#include "pw_span/span.h"
#include <array>
#include <string_view>

#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/rpc_channel.h"
#include "pw_rpc/channel.h"
#include "pw_status/status.h"
#include "pw_stream/sys_io_stream.h"
#include "pw_sys_io/sys_io.h"

#include <lib/support/logging/TextOnlyLogging.h>

#include <array>

#include "FreeRTOS.h"
#include "task.h"

namespace chip {
namespace rpc {
namespace {

using std::byte;

constexpr size_t kMaxTransmissionUnit = 1500;

// Used to write HDLC data to pw::sys_io.
pw::stream::SysIoWriter sysIoWriter;

// May be nullptr
::chip::rpc::Mutex * uart_mutex;

template <size_t buffer_size>
class ChipRpcChannelOutput : public pw::rpc::ChannelOutput
{
public:
    constexpr ChipRpcChannelOutput(pw::stream::Writer & writer, uint8_t address, const char * channel_name) :
        pw::rpc::ChannelOutput(channel_name), mWriter(writer), mAddress(address)
    {}

    pw::Status Send(pw::span<const std::byte> buffer) override
    {
        if (buffer.empty())
        {
            return pw::OkStatus();
        }
        if (uart_mutex)
        {
            uart_mutex->Lock();
        }
        pw::Status ret = pw::hdlc::WriteUIFrame(mAddress, buffer, mWriter);
        if (uart_mutex)
        {
            uart_mutex->Unlock();
        }
        return ret;
    }

private:
    pw::stream::Writer & mWriter;
    const uint8_t mAddress;
};

// Set up the output channel for the pw_rpc server to use to use.
ChipRpcChannelOutput<kMaxTransmissionUnit> hdlc_channel_output(sysIoWriter, pw::hdlc::kDefaultRpcAddress, "HDLC channel");

pw::rpc::Channel channels[] = { pw::rpc::Channel::Create<1>(&hdlc_channel_output) };

// pw_rpc server with the HDLC channel.
pw::rpc::Server server(channels);

} // namespace


uint32_t rpc_start_task_cnt_1 = 0;
uint32_t rpc_start_task_cnt_2 = 0;
uint32_t rpc_start_task_cnt_3 = 0;
uint32_t rpc_start_task_cnt_4 = 0;
uint32_t rpc_start_task_cnt_5 = 0;

extern "C" void * pxCurrentTCB;
uint32_t rpc_start_task_dummy1 = 0, rpc_start_task_dummy6 = 0;
void Start(void (*RegisterServices)(pw::rpc::Server &), ::chip::rpc::Mutex * uart_mutex_)
{
    PW_DASSERT(uart_mutex_ != nullptr);
    PW_DASSERT(RegisterServices != nullptr);
    uart_mutex = uart_mutex_;

    // Set up the server and start processing data.
    RegisterServices(server);

    // Declare a buffer for decoding incoming HDLC frames.
    std::array<std::byte, kMaxTransmissionUnit> input_buffer;

    Logging::Log(Logging::kLogModule_NotSpecified, Logging::kLogCategory_Detail, "Starting pw_rpc server");

    pw::hdlc::Decoder decoder(input_buffer);
    while (true)
    {
        std::byte data;

        rpc_start_task_cnt_2 = rpc_start_task_cnt_3 = rpc_start_task_cnt_4 = rpc_start_task_cnt_5 = rpc_start_task_cnt_1;
        rpc_start_task_cnt_1 ++;

        vTaskEnterCritical();
        StaticTask_t * task_handle = (StaticTask_t *)pxCurrentTCB;
        rpc_start_task_dummy1 = (uint32_t) task_handle->pxDummy1;
        rpc_start_task_dummy6 = (uint32_t) task_handle->pxDummy6;

        memset(task_handle->pxDummy6, 0xa5, ((uint32_t)task_handle->pxDummy1 - (uint32_t)task_handle->pxDummy6));
        vTaskExitCritical();

        if (!pw::sys_io::ReadByte(&data).ok())
        {
            // TODO: should we log?
            rpc_start_task_cnt_2 ++;
            return;
        }
        rpc_start_task_cnt_3 ++;
        if (auto result = decoder.Process(data); result.ok())
        {
            pw::hdlc::Frame & frame = result.value();
            if (frame.address() == pw::hdlc::kDefaultRpcAddress)
            {
                server.ProcessPacket(frame.data()).IgnoreError();
            }
            rpc_start_task_cnt_4 ++;
        }
        rpc_start_task_cnt_5 ++;
    }
}

} // namespace rpc
} // namespace chip
