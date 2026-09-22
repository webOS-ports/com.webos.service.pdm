// Copyright (c) 2019-2022 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <glib.h>
#include <memory>
#include <stdexcept>
#include "CommandManager.h"
#include "DeviceNotification.h"
#include "LunaIPC.h"
#include "PdmConfig.h"
#include "PdmLocaleHandler.h"
#include "PdmLogUtils.h"
#include "PdmLunaService.h"
#include "PdmNetlinkManager.h"
#include "PdmNotificationManager.h"
#include "DeviceManager.h"
#include "DeviceTracker.h"
#include "PluginAdapter.h"
#include "PdmNetlinkClassAdapter.h"

#define PLUGIN_PATH "/usr/lib/libpdm-plugin.so.1"

int main(int argc, char *argv[])
{

    int exitStatus = EXIT_SUCCESS;
    GMainLoop *mainLoop = nullptr;
    PluginAdapter *pluginAdapter = nullptr;
    PdmConfig* pConfObj = nullptr;
    DeviceTracker* pDevTracker = nullptr;
    CommandManager *pCommandManager = nullptr;
    PdmNotificationManager *pNotificationMgr = nullptr;

    try {
        mainLoop = g_main_loop_new(NULL, FALSE);

        if(mainLoop == nullptr)
           throw std::bad_alloc();

        pluginAdapter = new PluginAdapter();

        pConfObj = new PdmConfig();

        if(PdmConfigStatus::PDM_CONFIG_ERROR_NONE != pConfObj->readFile())
            throw std::runtime_error("Config file read error");

        if(!DeviceManager::getInstance()->init(pConfObj,pluginAdapter))
            throw std::runtime_error("DeviceManager init fail");

        pDevTracker = new DeviceTracker(pluginAdapter);

        pNotificationMgr  = new PdmNotificationManager();

        pCommandManager = new CommandManager();
        pDevTracker->attachObservers();
        pNotificationMgr->attachObservers();

        if(LunaIPC::getInstance()->init(mainLoop,pCommandManager) == false)
            throw std::runtime_error("LunaIPC init fail");

        pluginAdapter->pluginLoader(LunaIPC::getInstance()->getLSHandle(), PLUGIN_PATH);

        pluginAdapter->init();

        if (!DeviceNotification::getInstance()->init())
            throw std::runtime_error("DeviceNotification init fail");

        if(!PdmLocaleHandler::getInstance()->init())
            throw std::runtime_error("PdmLocaleHandler init fail");

        PdmNetlinkClassAdapter::getInstance().setCommandManager(pCommandManager);
        
        PdmNetlinkManager::getInstance()->start(pCommandManager);
        g_main_loop_run(mainLoop);

        PDM_LOG_INFO("PdmMain:",0,"main loop stopped, shutting down");
        DeviceNotification::getInstance()->deInit();
        LunaIPC::getInstance()->deInit();
        PdmNetlinkManager::getInstance()->stop();
        pluginAdapter->unloadPlugin();
    } catch(std::exception& e) {

        PDM_LOG_ERROR("Exception occurred : %s", e.what());
        /* Anything thrown above is a start-up failure. Reporting success made
         * systemd's Restart=on-failure a no-op, so a pdm that could not read
         * its config or register on the bus stayed down until reboot. */
        exitStatus = EXIT_FAILURE;
    }

    /* Reached on both the clean and the failed path, so anything constructed
     * before the failure is still released. */
    delete pCommandManager;
    delete pDevTracker;
    delete pConfObj;
    delete pluginAdapter;
    delete pNotificationMgr;
    if(mainLoop)
        g_main_loop_unref(mainLoop);

    pCommandManager = nullptr;
    pDevTracker = nullptr;
    pConfObj = nullptr;
    pluginAdapter = nullptr;
    pNotificationMgr = nullptr;
    mainLoop = nullptr;

    return exitStatus;
} // main
