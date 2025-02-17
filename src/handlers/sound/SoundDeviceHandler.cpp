// Copyright (c) 2019 LG Electronics, Inc.
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

#include "Common.h"
#include "SoundDeviceHandler.h"
#include "PdmJson.h"

using namespace PdmDevAttributes;

bool SoundDeviceHandler::mIsObjRegistered = SoundDeviceHandler::RegisterObject();

SoundDeviceHandler::SoundDeviceHandler(PdmConfig* const pConfObj, PluginAdapter* const pluginAdapter)
    : DeviceHandler(pConfObj, pluginAdapter)
{
    lunaHandler->registerLunaCallback(std::bind(&SoundDeviceHandler::GetAttachedDeviceStatus, this, _1, _2),
                                                                          GET_DEVICESTATUS);
    lunaHandler->registerLunaCallback(std::bind(&SoundDeviceHandler::GetAttachedAudioDeviceList, this, _1, _2),
                                                                                    GET_AUDIODEVICELIST);
    lunaHandler->registerLunaCallback(std::bind(&SoundDeviceHandler::GetAttachedNonStorageDeviceList, this, _1, _2),
                                                                                   GET_NONSTORAGEDEVICELIST);
}

SoundDeviceHandler::~SoundDeviceHandler() {
    if(!sList.empty())
    {
        for(auto soundDevice : sList)
        {
            delete soundDevice;
        }
        sList.clear();
    }
}

bool SoundDeviceHandler::HandlerEvent(PdmNetlinkEvent* pNE){

    PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d ", __FUNCTION__, __LINE__);

    if(!isSoundDevice(pNE))
        return false;

    if(pNE->getDevAttribute(DEVTYPE) ==  USB_DEVICE) {
        ProcessSoundDevice(pNE);
        return false;
    }
    else if(pNE->getDevAttribute(SUBSYSTEM) ==  "sound") {
        ProcessSoundDevice(pNE);
        return true;
    }
    return false;

}

void SoundDeviceHandler::removeDevice(SoundDevice* soundDevice)
{
    if(!soundDevice)
        return;
    sList.remove(soundDevice);
    Notify(SOUND_DEVICE, REMOVE, soundDevice);
    delete soundDevice;
    soundDevice = nullptr;
}

void SoundDeviceHandler::ProcessSoundDevice(PdmNetlinkEvent* pNE){
    SoundDevice *soundDevice;
    PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d SoundDeviceHandler: DEVTYPE: %s ACTION: %s", __FUNCTION__, __LINE__,pNE->getDevAttribute(DEVTYPE).c_str(),pNE->getDevAttribute(ACTION).c_str());
    switch(sMapDeviceActions[pNE->getDevAttribute(ACTION)])
    {
        case DeviceActions::USB_DEV_ADD:
            PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d  Add Sound device",__FUNCTION__, __LINE__);
            soundDevice = getDeviceWithPath< SoundDevice >(sList,pNE->getDevAttribute(DEVPATH));
            if (!soundDevice) {
                if(pNE->getDevAttribute(DEVTYPE) == USB_DEVICE) {
                    soundDevice = new (std::nothrow) SoundDevice(m_pConfObj, m_pluginAdapter);
                    if(soundDevice) {
                        soundDevice->setDeviceInfo(pNE);
                        sList.push_back(soundDevice);
                    } else {
                        PDM_LOG_CRITICAL("SoundDeviceHandler:%s line: %d Unable to create new Sound device", __FUNCTION__, __LINE__);
                    }
                } else
                    PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d  DEVTYPE is not USB_DEVICE",__FUNCTION__, __LINE__);
            }else
                soundDevice->updateDeviceInfo(pNE);
        break;
        case DeviceActions::USB_DEV_REMOVE:
            PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d  Remove Sound device", __FUNCTION__, __LINE__);
            soundDevice = getDeviceWithPath< SoundDevice >(sList,pNE->getDevAttribute(DEVPATH));
            if(soundDevice)
                removeDevice(soundDevice);
        break;
        case DeviceActions::USB_DEV_CHANGE:
            soundDevice = getDeviceWithPath< SoundDevice >(sList,pNE->getDevAttribute(DEVPATH));
            if(soundDevice){
                PDM_LOG_DEBUG("SoundDeviceHandlerif:%s line: %d USB_DEV_CHANGE ", __FUNCTION__, __LINE__);
                soundDevice->updateDeviceInfo(pNE);
                Notify(SOUND_DEVICE,ADD);
            }
        break;
        default:
            //Do nothing
            break;
    }
}

bool SoundDeviceHandler::HandlerCommand(CommandType *cmdtypes, CommandResponse *cmdResponse) {
    PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d", __FUNCTION__, __LINE__);
    return false;
}

bool SoundDeviceHandler::HandlePluginEvent(int eventType)
{
    PDM_LOG_DEBUG("SoundDeviceHandler:%s line: %d", __FUNCTION__, __LINE__);
    return false;
}

bool SoundDeviceHandler::GetAttachedDeviceStatus(pbnjson::JValue &payload, LSMessage *message)
{
    return getAttachedDeviceStatus< SoundDevice >(sList, payload );
}

bool SoundDeviceHandler::GetAttachedAudioDeviceList(pbnjson::JValue &payload, LSMessage *message)
{
    return getAttachedAudioDeviceList< SoundDevice >( sList, payload );
}

bool SoundDeviceHandler::GetAttachedNonStorageDeviceList(pbnjson::JValue &payload, LSMessage *message)
{
    return getAttachedNonStorageDeviceList< SoundDevice >( sList, payload );
}

bool SoundDeviceHandler::isSoundDevice(PdmNetlinkEvent* pNE)
{
    if((pNE->getInterfaceClass().find(iClass) != std::string::npos) || (pNE->getDevAttribute(SUBSYSTEM) == "sound"))
        return true;

    return false;
}
