// Copyright (c) 2022 LG Electronics, Inc.
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

#include <functional>
#include "SoundSubsystem.h"
#include "DeviceClassFactory.h"
#include "Common.h"
#include "PdmLogUtils.h"

using namespace PdmDevAttributes;

bool SoundSubsystem::mIsObjRegistered = SoundSubsystem::RegisterSubSystem();

SoundSubsystem::SoundSubsystem(std::unordered_map<std::string, std::string>& devPropMap)
	: DeviceClass(devPropMap), mDevType("sound")
{
	for (auto &prop : devPropMap)
		mDevPropMap[prop.first] = prop.second;
}

SoundSubsystem::~SoundSubsystem() {}

SoundSubsystem* SoundSubsystem::create(std::unordered_map<std::string, std::string>& devProMap)
{
	PDM_LOG_DEBUG("SoundSubsystem:%s line: %d", __FUNCTION__, __LINE__);
	bool canProcessEve = SoundSubsystem::canProcessEvent(devProMap);
	
	if (!canProcessEve)
		return nullptr;
	
	SoundSubsystem* ptr = new (std::nothrow) SoundSubsystem(devProMap);
	PDM_LOG_DEBUG("SoundSubsystem:%s line: %d SoundSubsystem object created", __FUNCTION__, __LINE__);
	return ptr;
}

std::string SoundSubsystem::getCardId()
{
	return mDevPropMap[CARD_ID];
}

std::string SoundSubsystem::getCardName()
{
	return mDevPropMap[CARD_NAME];
}

std::string SoundSubsystem::getCardNumber()
{
	std::string cardNumber = mDevPropMap[CARD_NUMBER];
	if (!cardNumber.empty())
		return cardNumber;

	/* Only USB sound devices carry a CARD_NUMBER property. For a platform
	 * card udev exposes the number as a sysfs attribute instead, so this
	 * lookup comes back empty and SoundDevice keeps its default of 0 -
	 * every built-in card is then reported as card 0. That is harmless on a
	 * board whose only card really is 0, but where it is not, audiod asks
	 * PulseAudio to open the wrong hw: device and playback never starts.
	 *
	 * DEVPATH already ends in the card node, e.g.
	 *   /devices/platform/rk817-sound/sound/card1
	 * so recover the number from there rather than reading sysfs again.
	 */
	const std::string &devPath = mDevPropMap[DEVPATH];
	const std::size_t cardPos = devPath.rfind("/card");
	if (cardPos != std::string::npos) {
		std::size_t start = cardPos + 5;
		std::size_t end = start;
		while (end < devPath.size() && devPath[end] >= '0' && devPath[end] <= '9')
			++end;
		if (end > start)
			cardNumber = devPath.substr(start, end - start);
	}

	return cardNumber;
}