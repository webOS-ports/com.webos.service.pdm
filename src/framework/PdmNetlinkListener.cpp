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

#include <libudev.h>
#include <stdarg.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstring>
#include <thread>
#include <cerrno>

#include "PdmNetlinkListener.h"
#include "PdmLogUtils.h"
#include "PdmNetlinkClassAdapter.h"

#define memzero(x,l) (std::memset((x), 0, (l)))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define SUBSYSTEM "usb"


PdmNetlinkListener::PdmNetlinkListener() : m_udev(nullptr), m_stopFd(-1) {
}

PdmNetlinkListener::~PdmNetlinkListener(){
    stopListener();
}


bool PdmNetlinkListener::startListener(){
    if (!init())
        return false;
    runListner();
    return true;
}

/* Safe to call twice, and safe to call on a listener that never started:
 * PdmNetlinkManager calls it on shutdown and the destructor calls it again. */
bool PdmNetlinkListener::stopListener(){
    if (m_listenerThread.joinable()) {
        /* Wake the thread out of epoll_wait() first. Unreferencing m_udev
         * while it is still running would pull the monitor out from under it. */
        if (m_stopFd >= 0) {
            const uint64_t one = 1;
            if (write(m_stopFd, &one, sizeof(one)) != sizeof(one))
                PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d cannot signal listener: %s", __FUNCTION__, __LINE__, strerror(errno));
        }
        try {
            m_listenerThread.join();
        }
        catch (std::exception &e) {
            PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d caught system_error: %s", __FUNCTION__, __LINE__, e.what());
        }
    }

    if (m_stopFd >= 0) {
        close(m_stopFd);
        m_stopFd = -1;
    }
    if (m_udev) {
        udev_unref(m_udev);
        m_udev = nullptr;
    }
    return true;
}

/*  To get the new udev instances
*/
bool PdmNetlinkListener::init(){
    m_udev = udev_new();
    if (!m_udev) {
        PDM_LOG_CRITICAL("PdmNetlinkListener: %s line: %d udev_new() failed", __FUNCTION__, __LINE__);
        return false;
    }

    m_stopFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (m_stopFd < 0) {
        PDM_LOG_CRITICAL("PdmNetlinkListener: %s line: %d eventfd() failed: %s", __FUNCTION__, __LINE__, strerror(errno));
        udev_unref(m_udev);
        m_udev = nullptr;
        return false;
    }

    enumerate_devices();
    return true;
}

void PdmNetlinkListener::enumerate_devices()
{
    struct udev_enumerate* enumerate = udev_enumerate_new(m_udev);
    if (!enumerate) {
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d udev_enumerate_new() failed", __FUNCTION__, __LINE__);
        return;
    }

    udev_enumerate_add_match_subsystem(enumerate, SUBSYSTEM);
    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_add_match_subsystem(enumerate, "sound");
    udev_enumerate_add_match_subsystem(enumerate, "video4linux");
    udev_enumerate_add_match_subsystem(enumerate, "net");
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_add_match_subsystem(enumerate, "rfkill");

    udev_enumerate_scan_devices(enumerate);
    struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry* entry;

    udev_list_entry_foreach(entry, devices) {
        const char* path = udev_list_entry_get_name(entry);
        struct udev_device* device = udev_device_new_from_syspath(m_udev, path);
        if (device != NULL) {
            PdmNetlinkClassAdapter::getInstance().handleEvent(device, true);
        }
        udev_device_unref(device);
    }

    udev_enumerate_unref(enumerate);
}

void PdmNetlinkListener::threadStart(){
    struct udev_monitor* monitor = NULL;
    int fd_ep;
    int fd_udev = -1;
    struct epoll_event ep_udev;
    struct epoll_event ep_stop;

    fd_ep = epoll_create1(EPOLL_CLOEXEC);
    if (fd_ep < 0) {
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d error creating epoll fd: %d", __FUNCTION__, __LINE__,fd_ep);
        goto out;
    }
    monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (monitor == NULL) {
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d no socket\n", __FUNCTION__, __LINE__);
        goto out;
    }

    if(udev_monitor_filter_add_match_subsystem_devtype(monitor, "block", NULL) < 0 ||
        udev_monitor_filter_add_match_subsystem_devtype(monitor, "input", NULL) < 0 ||
            udev_monitor_filter_add_match_subsystem_devtype(monitor, "usb", "usb_device") < 0 ||
                udev_monitor_filter_add_match_subsystem_devtype(monitor, "sound", NULL) < 0 ||
                    udev_monitor_filter_add_match_subsystem_devtype(monitor, "video4linux", NULL) < 0 ||
                        udev_monitor_filter_add_match_subsystem_devtype(monitor, "net", NULL) < 0 ||
                            udev_monitor_filter_add_match_subsystem_devtype(monitor, "tty", NULL) < 0 ||
                                udev_monitor_filter_add_match_subsystem_devtype(monitor, "rfkill", NULL) < 0){
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d filter failed\n", __FUNCTION__, __LINE__);
        goto out;
    }

   if (udev_monitor_enable_receiving(monitor) < 0) {
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d bind failed\n", __FUNCTION__, __LINE__);
        goto out;
   }
   fd_udev = udev_monitor_get_fd(monitor);
   memzero(&ep_udev, sizeof(struct epoll_event));
   ep_udev.events = EPOLLIN;
   ep_udev.data.fd = fd_udev;
   if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_udev, &ep_udev) < 0) {
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d fail to add fd to epoll: %s", __FUNCTION__, __LINE__,strerror(errno));
        goto out;
   }

   memzero(&ep_stop, sizeof(struct epoll_event));
   ep_stop.events = EPOLLIN;
   ep_stop.data.fd = m_stopFd;
   if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, m_stopFd, &ep_stop) < 0) {
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d fail to add stop fd to epoll: %s", __FUNCTION__, __LINE__,strerror(errno));
        goto out;
   }

  for (;;) {
    int fdcount;
    struct epoll_event ev[4];

    fdcount = epoll_wait(fd_ep, ev, ARRAY_SIZE(ev), -1);
    if (fdcount < 0) {
        if (errno == EINTR)
            continue;
        PDM_LOG_ERROR("PdmNetlinkListener: %s line: %d epoll_wait failed: %s", __FUNCTION__, __LINE__, strerror(errno));
        goto out;
    }

    for (int i = 0; i < fdcount; i++) {
        if (!(ev[i].events & EPOLLIN))
            continue;

        if (ev[i].data.fd == m_stopFd) {
            PDM_LOG_DEBUG("PdmNetlinkListener: %s line: %d stop requested", __FUNCTION__, __LINE__);
            goto out;
        }

        if (ev[i].data.fd == fd_udev) {
            /* NULL on a receive error, which is not the same as "no device". */
            struct udev_device *device = udev_monitor_receive_device(monitor);
            if (!device)
                continue;
            PdmNetlinkClassAdapter::getInstance().handleEvent(device, false);
            udev_device_unref(device);
        }
    }
  }

   out:
       if (fd_ep >= 0)
        close(fd_ep);
    udev_monitor_unref(monitor);
}

void PdmNetlinkListener::runListner(){
    m_listenerThread = std::thread(&PdmNetlinkListener::threadStart,this);
}
