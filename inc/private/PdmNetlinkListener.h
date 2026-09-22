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

#ifndef _PDMNETLINKLISTENER_H
#define _PDMNETLINKLISTENER_H

#include <thread>

class DeviceClass;

class PdmNetlinkListener {

private:
    std::thread m_listenerThread;
    /* Owned by the listener, not a global: the monitor thread dereferences it
     * for as long as it runs, so it may only be unreferenced after the join. */
    struct udev* m_udev;
    /* eventfd the monitor thread also waits on, so stopListener() can break it
     * out of epoll_wait() instead of waiting forever for a uevent. */
    int m_stopFd;
public:
  PdmNetlinkListener();
  virtual ~PdmNetlinkListener();
  bool startListener();
  bool stopListener();
  virtual void onEvent(DeviceClass *deviceClassEvent) = 0;

  PdmNetlinkListener(const PdmNetlinkListener&) = delete;
  PdmNetlinkListener& operator=(const PdmNetlinkListener&) = delete;
private:
  bool init();
  void runListner();
  void threadStart();
  void enumerate_devices();
};
#endif //_PDMNETLINKLISTENER_H
