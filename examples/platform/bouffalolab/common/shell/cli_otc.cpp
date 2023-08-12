/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include <lib/core/CHIPCore.h>

#include <ChipShellCollection.h>

#include <platform/CHIPDeviceLayer.h>
#include <lib/shell/Engine.h>

#include <openthread/cli.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/thread.h>
#include <openthread_port.h>

using namespace chip;
using namespace chip::Shell;
using namespace chip::Platform;
using namespace chip::DeviceLayer;
using namespace chip::Logging;

#ifndef SHELL_OTCLI_TX_BUFFER_SIZE
#define SHELL_OTCLI_TX_BUFFER_SIZE 1024
#endif
static char sTxBuffer[SHELL_OTCLI_TX_BUFFER_SIZE];
static constexpr uint16_t sTxLength = SHELL_OTCLI_TX_BUFFER_SIZE;
static chip::Shell::Engine sShellOtcliSubcommands;

CHIP_ERROR cli_otc_help_iterator(shell_command_t * command, void * arg)
{
    streamer_printf(streamer_get(), "  %-15s %s\n\r", command->cmd_name, command->cmd_help);
    return CHIP_NO_ERROR;
}

CHIP_ERROR cli_otc_help(int argc, char ** argv)
{
    sShellOtcliSubcommands.ForEachCommand(cli_otc_help_iterator, nullptr);
    return CHIP_NO_ERROR;
}

CHIP_ERROR cli_otc_dispatch(int argc, char ** argv)
{
    CHIP_ERROR error = CHIP_NO_ERROR;

// From OT CLI internal lib, kMaxLineLength = 128
#define kMaxLineLength 128
    char buff[kMaxLineLength] = { 0 };
    char * buff_ptr           = buff;
    int i                     = 0;

    VerifyOrExit(argc > 0, error = CHIP_ERROR_INVALID_ARGUMENT);

    for (i = 0; i < argc; i++)
    {
        size_t arg_len = strlen(argv[i]);

        /* Make sure that the next argument won't overflow the buffer */
        VerifyOrExit(buff_ptr + arg_len < buff + kMaxLineLength, error = CHIP_ERROR_BUFFER_TOO_SMALL);

        strncpy(buff_ptr, argv[i], arg_len);
        buff_ptr += arg_len;

        /* Make sure that there is enough buffer for a space char */
        if (buff_ptr + sizeof(char) < buff + kMaxLineLength)
        {
            strncpy(buff_ptr, " ", sizeof(char));
            buff_ptr++;
        }
    }
    buff_ptr = 0;

    otrLock();
    otCliInputLine(buff);
    otrUnlock();

exit:
    return error;
}

static const shell_command_t cmds_otcli_root = { &cli_otc_dispatch, "otc", "Dispatch OpenThread CLI command" };

static int OnOtCliOutput(void * aContext, const char * aFormat, va_list aArguments)
{
    int rval = vsnprintf(sTxBuffer, sTxLength, aFormat, aArguments);
    VerifyOrExit(rval >= 0 && rval < sTxLength, rval = CHIP_ERROR_BUFFER_TOO_SMALL.AsInteger());
    return streamer_write(streamer_get(), (const char *) sTxBuffer, rval);
exit:
    return rval;
}

void cli_otc_init(void)
{
    if (NULL == otrGetInstance()) {
        return;
    }

    otCliInit(otrGetInstance(), &OnOtCliOutput, NULL);

    // Register the root otcli command with the top-level shell.
    Engine::Root().RegisterCommands(&cmds_otcli_root, 1);
}
