// Copyright (c) 2019-2024 LG Electronics, Inc.
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

#include <sstream>
#include <iomanip>
#include <unordered_map>

#include "Common.h"
#include "DeviceHandler.h"
#include "DeviceManager.h"
#include "PdmNotificationManager.h"
#include "PdmLogUtils.h"
#include "MTPDevice.h"
#include "StorageDevice.h"
#include "DiskPartitionInfo.h"
#include "PdmUtils.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

PdmNotificationManager::PdmNotificationManager()
     : IObserver(), m_powerState(true), m_sharedMemory(nullptr)
{
    m_pLocHandler = PdmLocaleHandler::getInstance();

    int shmFd = shm_open(PDM_SHM_NAME, O_CREAT | O_RDWR | O_CLOEXEC, 0640);
    if (shmFd == -1) {
        PDM_LOG_ERROR("shm_open() is failed, error :%s", strerror(errno));
        return;
    }

    // shm_open() applies the umask to the mode it is given, and the reader
    // needs the group bit, so set the mode explicitly
    if (fchmod(shmFd, 0640) == -1)
        PDM_LOG_ERROR("fchmod() is failed, error :%s", strerror(errno));

    if (ftruncate(shmFd, PDM_SHM_SIZE) == -1) {
        PDM_LOG_ERROR("ftruncate() is failed, error :%s", strerror(errno));
        close(shmFd);
        return;
    }

    void *mapping = mmap(nullptr, PDM_SHM_SIZE, PROT_READ | PROT_WRITE,
                         MAP_SHARED, shmFd, 0);

    // the mapping outlives the descriptor
    close(shmFd);

    if (mapping == MAP_FAILED) {
        PDM_LOG_ERROR("mmap() is failed, error :%s", strerror(errno));
        return;
    }

    m_sharedMemory = static_cast<char *>(mapping);
}

PdmNotificationManager::~PdmNotificationManager()
{
    std::list<DeviceHandler*> pDeviceHandlerList = DeviceManager::getInstance()->getDeviceHandlerList();

    for(auto handler : pDeviceHandlerList)
        handler->Unregister(this);

    if (m_sharedMemory != nullptr) {
        if (munmap(m_sharedMemory, PDM_SHM_SIZE) == -1)
            PDM_LOG_ERROR("munmap() is failed, error :%s", strerror(errno));

        m_sharedMemory = nullptr;
    }

    if (shm_unlink(PDM_SHM_NAME) == -1)
        PDM_LOG_ERROR("shm_unlink() is failed, error :%s", strerror(errno));
}

void PdmNotificationManager::attachObservers()
{
    std::list<DeviceHandler*> pDeviceHandlerList = DeviceManager::getInstance()->getDeviceHandlerList();

    for(auto handler : pDeviceHandlerList)
        handler->Register(this);
}

bool PdmNotificationManager::HandlePluginEvent(int eventType)
{
    PDM_LOG_DEBUG("PdmNotificationManager: %s line: %d Event Type %d", __FUNCTION__, __LINE__, eventType );
    switch(eventType)
    {
        case POWER_PROCESS_REQEUST_SUSPEND:
        case POWER_PROCESS_PREPARE_RESUME:
            m_powerState = false;
            break;
        case POWER_STATE_RESUME_DONE:
            m_powerState = true;
            break;
        default:
            //Nothing
            break;
    }
    return false;
}

void PdmNotificationManager::update(const int &eventDeviceType, const int &eventID, IDevice* device)
{
    PDM_LOG_INFO("PdmNotificationManager:",0,"%s line: %d update -  Event: %d eventID: %d", __FUNCTION__,__LINE__,eventDeviceType, eventID);
    if(m_powerState == false)
        return;

    if((device) && (!device->canDisplayToast()))
         return;

    switch(eventID)
    {
        case CONNECTING: showConnectingToast(eventDeviceType); break;
        case MAX_COUNT_REACHED: createAlertForMaxUsbStorageDevices();break;
        case REMOVE_BEFORE_MOUNT:
            if(eventDeviceType == PdmDevAttributes::MTP_DEVICE)
            {
                unMountMtpDeviceAlert(device);
                break;
            }else{
                createAlertForUnmountedDeviceRemoval(device);
                break;
            }
        case UNSUPPORTED_FS_FORMAT_NEEDED: createAlertForUnsupportedFileSystem(device);break;
        case FSCK_TIMED_OUT: createAlertForFsckTimeout(device);break;
        case FORMAT_STARTED: showFormatStartedToast(device);break;
        case FORMAT_SUCCESS: showFormatSuccessToast(device);break;
        case FORMAT_FAIL: showFormatFailToast(device);break;
        case REMOVE_UNSUPPORTED_FS: closeUnsupportedFsAlert(device);break;
    }
}

bool PdmNotificationManager::isToastRequired(int eventDeviceType)
{
    switch(eventDeviceType)
    {
        default:
        //Except above devices,Toast is required for all other device.
            return true;
    }
}

void PdmNotificationManager::sendAlertInfo(pdmEvent pEvent, pbnjson::JValue parameters)
{
    unsigned int eventPid = 0;
    std::string payloadStr;
    pbnjson::JValue payload = pbnjson::Object();
    union sigval sv;

    payload.put("pdmEvent", pEvent);
    payload.put("parameters", parameters);
    payloadStr = payload.stringify().c_str();
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d payload for signal handler: %s", __FUNCTION__, __LINE__, payload.stringify().c_str());

    if (m_sharedMemory == nullptr) {
        PDM_LOG_ERROR("PdmNotificationManager:%s line: %d no shared memory to publish the event through", __FUNCTION__, __LINE__);
        return;
    }

    // the reader is told the length through the signal, so the payload does
    // not have to be terminated - but it does have to fit
    if (payloadStr.length() > PDM_SHM_SIZE) {
        PDM_LOG_ERROR("PdmNotificationManager:%s line: %d payload of %d bytes does not fit in %d bytes of shared memory", __FUNCTION__, __LINE__, (int)payloadStr.length(), PDM_SHM_SIZE);
        return;
    }

    memcpy(m_sharedMemory, payloadStr.c_str(), payloadStr.length());

    sv.sival_int = payloadStr.length();

    eventPid = PdmUtils::getPIDbyName("event-monitor");
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d  event-monitor process ID :%d", __FUNCTION__, __LINE__, eventPid);

    if (eventPid > 0) {
        if (-1 == sigqueue(eventPid, SIGUSR2, sv))
            PDM_LOG_ERROR("sigqueue is failed error :%s", strerror(errno));
    }
}

void PdmNotificationManager::showConnectingToast(int eventDeviceType)
{
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d device type: %d", __FUNCTION__, __LINE__, eventDeviceType);

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("deviceType", eventDeviceType);
    PdmNotificationManager::sendAlertInfo(CONNECTING_EVENT, std::move(parameters));
}

void PdmNotificationManager::showToast(const std::string& message,const std::string &iconUrl)
{
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d ", __FUNCTION__, __LINE__);

    if(!createToast(message,iconUrl))
        PDM_LOG_ERROR("Unable to create Toast");
}

void PdmNotificationManager::createAlertForMaxUsbStorageDevices()
{
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d ", __FUNCTION__, __LINE__);

    pbnjson::JValue parameters = pbnjson::Object();
    PdmNotificationManager::sendAlertInfo(MAX_COUNT_REACHED_EVENT, std::move(parameters));
}

void PdmNotificationManager::unMountMtpDeviceAlert(IDevice* device)
{
    if(!device)
    {
        cout << "Path Matched"<< endl;
        return;
    }

    MTPDevice*  pMtpDev = dynamic_cast<MTPDevice*>(device);
    if(!pMtpDev)
        return;

    std::string driveName = pMtpDev->getDriveName();
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d driveName: %s", __FUNCTION__, __LINE__, driveName.c_str());
    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("driveName", driveName);
    PdmNotificationManager::sendAlertInfo(REMOVE_BEFORE_MOUNT_MTP_EVENT, std::move(parameters));
}

void PdmNotificationManager::createAlertForUnmountedDeviceRemoval(IDevice* device)
{
    if(!device)
        return;

    StorageDevice*  pStorageDev = dynamic_cast<StorageDevice*>(device);
    if(!pStorageDev)
        return;

    std::string devNumStr = std::to_string(pStorageDev->getDeviceNum());

    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d devNumStr: %s", __FUNCTION__, __LINE__, devNumStr.c_str());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("deviceNum", devNumStr);
    PdmNotificationManager::sendAlertInfo(REMOVE_BEFORE_MOUNT_EVENT, std::move(parameters));
}

void PdmNotificationManager::createAlertForUnsupportedFileSystem(IDevice* device)
{
    if(!device)
        return;

    StorageDevice*  pStorageDev = dynamic_cast<StorageDevice*>(device);
    if(!pStorageDev)
        return;

    std::string devNumStr = std::to_string(pStorageDev->getDeviceNum());

    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d devNumStr: %s", __FUNCTION__, __LINE__, devNumStr.c_str());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("deviceNum", devNumStr);
    PdmNotificationManager::sendAlertInfo(UNSUPPORTED_FS_FORMAT_NEEDED_EVENT, std::move(parameters));
}

void PdmNotificationManager::createAlertForFsckTimeout(IDevice* device)
{
    PDM_LOG_WARNING("PdmNotificationManager:%s line: %d Creating alert for fsck timeout", __FUNCTION__, __LINE__);

    if(!device)
        return;

    StorageDevice*  pStorageDev = dynamic_cast<StorageDevice*>(device);
    if(!pStorageDev)
        return;

    std::string devNumStr = std::to_string(pStorageDev->getDeviceNum());
    std::string mountName = pStorageDev->getDeviceName();

    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d devNumStr: %s mountName: %s", __FUNCTION__, __LINE__, devNumStr.c_str(), mountName.c_str());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("deviceNum", devNumStr);
    parameters.put("mountName", mountName);
    PdmNotificationManager::sendAlertInfo(FSCK_TIMED_OUT_EVENT, std::move(parameters));
}

void PdmNotificationManager::showFormatStartedToast(IDevice* device)
{
    if(!device)
        return;

    DiskPartitionInfo*  pPartition = dynamic_cast<DiskPartitionInfo*>(device);
    if(!pPartition)
        return;

    std::stringstream driveSizeInGb;
    driveSizeInGb << std::fixed << std::setprecision(2) << pPartition->getDriveSize()/(float)(1024 * 1024);
    std::string driveInfo = "[" + driveSizeInGb.str() + "GB] " + pPartition->getProductName();
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d driveInfo: %s", __FUNCTION__, __LINE__, driveInfo.c_str());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("driveInfo", driveInfo);
    PdmNotificationManager::sendAlertInfo(FORMAT_STARTED_EVENT, std::move(parameters));
}

void PdmNotificationManager::showFormatSuccessToast(IDevice* device)
{
    if(!device)
        return;

    DiskPartitionInfo*  pPartition = dynamic_cast<DiskPartitionInfo*>(device);
    if(!pPartition)
        return;

    std::stringstream driveSizeInGb;
    driveSizeInGb << std::fixed << std::setprecision(2) << pPartition->getDriveSize()/(float)(1024 * 1024);
    std::string driveInfo = "[" + driveSizeInGb.str() + "GB] " + pPartition->getProductName();
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d driveInfo: %s", __FUNCTION__, __LINE__, driveInfo.c_str());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("driveInfo", driveInfo);
    PdmNotificationManager::sendAlertInfo(FORMAT_SUCCESS_EVENT, std::move(parameters));
}

void PdmNotificationManager::showFormatFailToast(IDevice* device)
{
    if(!device)
        return;

    DiskPartitionInfo*  pPartition = dynamic_cast<DiskPartitionInfo*>(device);
    if(!pPartition)
        return;

    std::stringstream driveSizeInGb;
    driveSizeInGb << std::fixed << std::setprecision(2) << pPartition->getDriveSize()/(float)(1024 * 1024);
    std::string driveInfo = "[" + driveSizeInGb.str() + "GB] " + pPartition->getProductName();
    PDM_LOG_DEBUG("PdmNotificationManager:%s line: %d driveInfo: %s", __FUNCTION__, __LINE__, driveInfo.c_str());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("driveInfo", driveInfo);
    PdmNotificationManager::sendAlertInfo(FORMAT_FAIL_EVENT, std::move(parameters));
}

void PdmNotificationManager::closeUnsupportedFsAlert(IDevice* device)
{
    if(!device)
        return;

    StorageDevice*  pStorageDev = dynamic_cast<StorageDevice*>(device);
    if(!pStorageDev)
        return;

    std::string devNumStr = std::to_string(pStorageDev->getDeviceNum());

    pbnjson::JValue parameters = pbnjson::Object();
    parameters.put("deviceNum", devNumStr);
    PdmNotificationManager::sendAlertInfo(REMOVE_UNSUPPORTED_FS_EVENT, std::move(parameters));
}
