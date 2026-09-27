// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#include <gtest/gtest.h>

#include <list>
#include <string>

#include "Device.h"
#include "DeviceHandler.h"

// ---------------------------------------------------------------------------
// getDeviceAction: udev emits more actions than the four we map, and the
// lookup used to answer USB_DEV_ADD for all of them.
// ---------------------------------------------------------------------------

TEST(DeviceAction, MapsTheActionsWeHandle)
{
    EXPECT_EQ(getDeviceAction("add"), USB_DEV_ADD);
    EXPECT_EQ(getDeviceAction("remove"), USB_DEV_REMOVE);
    EXPECT_EQ(getDeviceAction("change"), USB_DEV_CHANGE);
    EXPECT_EQ(getDeviceAction("bind"), USB_DEV_BIND);
}

TEST(DeviceAction, UnknownActionsAreNotArrivals)
{
    // Real udev actions we do not handle. Any of these answering USB_DEV_ADD
    // makes StorageDevice re-read partitions and Sound/VideoDevice allocate a
    // sub-device for an event carrying no device.
    for (const char *action : {"unbind", "move", "online", "offline", "bind ", "ADD", ""})
        EXPECT_EQ(getDeviceAction(action), USB_DEV_UNKNOWN) << "action: " << action;
}

TEST(DeviceAction, LookupDoesNotGrowTheTable)
{
    const std::size_t before = sMapDeviceActions.size();
    for (int i = 0; i < 100; ++i)
        (void)getDeviceAction("unbind" + std::to_string(i));
    EXPECT_EQ(sMapDeviceActions.size(), before);
}

// ---------------------------------------------------------------------------
// getDeviceWithName: a partition name has to find its parent device, and a
// name that merely looks like one must not.
// ---------------------------------------------------------------------------

namespace {

class FakeDevice
{
public:
    explicit FakeDevice(std::string name) : mName(std::move(name)) {}
    std::string getDeviceName() const { return mName; }

private:
    std::string mName;
};

} // namespace

TEST(DeviceLookup, FindsTheParentDeviceOfAPartition)
{
    FakeDevice sda("sda");
    FakeDevice sdb("sdb");
    std::list<FakeDevice *> devices{&sda, &sdb};

    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, "sda"), &sda);
    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, "sda1"), &sda);
    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, "sdb2"), &sdb);
}

TEST(DeviceLookup, EmptyListAndUnknownNamesGiveNothing)
{
    std::list<FakeDevice *> empty;
    EXPECT_EQ(getDeviceWithName<FakeDevice>(empty, "sda1"), nullptr);

    FakeDevice sda("sda");
    std::list<FakeDevice *> devices{&sda};
    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, "sdz1"), nullptr);
    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, ""), nullptr);
    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, "sd"), nullptr);
}

// The prefix match is deliberate - "sda1" has to reach the device named "sda" -
// so a hostile drive name reaches a device object. It must then fail to match
// any partition, which is what keeps it out of the command lines.
TEST(DeviceLookup, PrefixMatchIsNotAPartitionMatch)
{
    FakeDevice sda("sda");
    std::list<FakeDevice *> devices{&sda};
    EXPECT_EQ(getDeviceWithName<FakeDevice>(devices, "sda1; reboot"), &sda);
}
