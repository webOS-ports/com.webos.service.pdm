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

#include <algorithm>
#include <string>
#include <vector>

#include "Common.h"
#include "DiskFormat.h"
#include "PdmFs.h"
#include "PdmFsck.h"

namespace {

/* The label a caller of setVolumeLabel/format could send. Whatever it is, it
 * has to survive as exactly one argv entry. */
const char *const kHostileLabel = "holiday; rm -rf / #";

int countOf(const std::vector<std::string> &argv, const std::string &value)
{
    return static_cast<int>(std::count(argv.begin(), argv.end(), value));
}

} // namespace

// ---------------------------------------------------------------------------
// PdmFs::volumeLabelCommand
// ---------------------------------------------------------------------------

TEST(VolumeLabelCommand, UsesTheRightToolPerFilesystem)
{
    EXPECT_EQ(PdmFs::volumeLabelCommand("ntfs", "sda1", "MYDISK"),
              (std::vector<std::string>{"ntfslabel", "-f", "/dev/sda1", "MYDISK"}));
    EXPECT_EQ(PdmFs::volumeLabelCommand("tntfs", "sda1", "MYDISK"),
              (std::vector<std::string>{"ntfslabel", "-f", "/dev/sda1", "MYDISK"}));
    EXPECT_EQ(PdmFs::volumeLabelCommand("vfat", "sdb2", "MYDISK"),
              (std::vector<std::string>{"fatlabel", "-f", "-l", "MYDISK", "/dev/sdb2"}));
    EXPECT_EQ(PdmFs::volumeLabelCommand("ext4", "sdc3", "MYDISK"),
              (std::vector<std::string>{"e2label", "/dev/sdc3", "MYDISK"}));
}

TEST(VolumeLabelCommand, UnsupportedFilesystemGivesNoCommand)
{
    EXPECT_TRUE(PdmFs::volumeLabelCommand("exfat", "sda1", "MYDISK").empty());
    EXPECT_TRUE(PdmFs::volumeLabelCommand("", "sda1", "MYDISK").empty());
    EXPECT_TRUE(PdmFs::volumeLabelCommand("fuse_mtp", "sda1", "MYDISK").empty());
}

TEST(VolumeLabelCommand, KeepsAHostileLabelInOneArgument)
{
    for (const char *fsType : {"ntfs", "vfat", "ext2", "ext3", "ext4"}) {
        const std::vector<std::string> argv =
            PdmFs::volumeLabelCommand(fsType, "sda1", kHostileLabel);
        ASSERT_FALSE(argv.empty()) << fsType;
        EXPECT_EQ(countOf(argv, kHostileLabel), 1) << fsType;
        // Nothing else in the argv picked up any of it.
        for (const std::string &arg : argv) {
            if (arg == kHostileLabel)
                continue;
            EXPECT_EQ(arg.find("rm -rf"), std::string::npos) << fsType << ": " << arg;
            EXPECT_EQ(arg.find(';'), std::string::npos) << fsType << ": " << arg;
        }
    }
}

TEST(VolumeLabelCommand, DriveNameIsAlwaysADevNode)
{
    const std::vector<std::string> argv = PdmFs::volumeLabelCommand("ext4", "sda1", "L");
    ASSERT_EQ(argv.size(), 3u);
    EXPECT_EQ(argv[1], "/dev/sda1");
}

// ---------------------------------------------------------------------------
// DiskFormat::formatCommand
// ---------------------------------------------------------------------------

TEST(FormatCommand, BuildsTheMkfsInvocation)
{
    DiskFormat formatter;
    const std::vector<std::string> argv =
        formatter.formatCommand("sda1", PdmDevAttributes::PDM_DRV_TYPE_EXT4, "");
    ASSERT_FALSE(argv.empty());
    EXPECT_EQ(argv.front(), "mkfs.ext4");
    EXPECT_EQ(argv.back(), "/dev/sda1");
}

TEST(FormatCommand, AppendsTheLabelAsItsOwnArgument)
{
    DiskFormat formatter;
    const std::vector<std::string> argv =
        formatter.formatCommand("sda1", PdmDevAttributes::PDM_DRV_TYPE_EXT4, "MYDISK");
    ASSERT_GE(argv.size(), 4u);
    EXPECT_EQ(argv.back(), "MYDISK");
    EXPECT_EQ(argv[argv.size() - 2], "-L");
    EXPECT_EQ(argv[argv.size() - 3], "/dev/sda1");
}

TEST(FormatCommand, KeepsAHostileLabelInOneArgument)
{
    DiskFormat formatter;
    for (const char *fsType : {PdmDevAttributes::PDM_DRV_TYPE_EXT4.c_str(),
                               PdmDevAttributes::PDM_DRV_TYPE_NTFS.c_str(),
                               PdmDevAttributes::PDM_DRV_TYPE_FAT.c_str()}) {
        const std::vector<std::string> argv =
            formatter.formatCommand("sda1", fsType, kHostileLabel);
        ASSERT_FALSE(argv.empty()) << fsType;
        EXPECT_EQ(argv.back(), kHostileLabel) << fsType;
        EXPECT_EQ(countOf(argv, kHostileLabel), 1) << fsType;
    }
}

TEST(FormatCommand, UnknownFilesystemGivesNoCommand)
{
    DiskFormat formatter;
    EXPECT_TRUE(formatter.formatCommand("sda1", "reiserfs", "").empty());
    EXPECT_TRUE(formatter.formatCommand("sda1", "", "").empty());
    // "nofs" is in the table but has no command; it must not produce an argv
    // whose first entry is the device node.
    EXPECT_TRUE(formatter.formatCommand("sda1", PdmDevAttributes::PDM_DRV_TYPE_NOFS, "").empty());
}

// ---------------------------------------------------------------------------
// PdmFsck::fsckCommand
// ---------------------------------------------------------------------------

TEST(FsckCommand, WrapsAutoModeInTimeout)
{
    PdmFsck fsck;
    const std::vector<std::string> argv =
        fsck.fsckCommand(PdmDevAttributes::PDM_FSCK_AUTO,
                         PdmDevAttributes::PDM_DRV_TYPE_EXT4, "sda1");
    ASSERT_GE(argv.size(), 3u);
    EXPECT_EQ(argv[0], "timeout");
    EXPECT_EQ(argv[1], "20");
    EXPECT_EQ(argv[2], "fsck.ext4");
    EXPECT_EQ(argv.back(), "/dev/sda1");
}

TEST(FsckCommand, ForceModeIsNotWrappedInTimeout)
{
    PdmFsck fsck;
    const std::vector<std::string> argv =
        fsck.fsckCommand(PdmDevAttributes::PDM_FSCK_FORCE,
                         PdmDevAttributes::PDM_DRV_TYPE_EXT4, "sda1");
    ASSERT_FALSE(argv.empty());
    EXPECT_EQ(argv.front(), "fsck.ext4");
}

TEST(FsckCommand, OptionsArriveAsSeparateArguments)
{
    PdmFsck fsck;
    const std::vector<std::string> argv =
        fsck.fsckCommand(PdmDevAttributes::PDM_FSCK_AUTO,
                         PdmDevAttributes::PDM_DRV_TYPE_JFS, "sda1");
    ASSERT_FALSE(argv.empty());
    // "-Q -z 0x00000000" is one string in the table and four argv entries here.
    for (const std::string &arg : argv)
        EXPECT_EQ(arg.find(' '), std::string::npos) << "unsplit argument: " << arg;
}

TEST(FsckCommand, FilesystemsWithoutAnFsckGiveNoCommand)
{
    PdmFsck fsck;
    EXPECT_TRUE(fsck.fsckCommand(PdmDevAttributes::PDM_FSCK_AUTO,
                                 PdmDevAttributes::PDM_DRV_TYPE_EXFAT, "sda1").empty());
    EXPECT_TRUE(fsck.fsckCommand(PdmDevAttributes::PDM_FSCK_AUTO,
                                 PdmDevAttributes::PDM_DRV_TYPE_FUSE_MTP, "sda1").empty());
    // Not in the table at all - this used to insert an empty entry.
    EXPECT_TRUE(fsck.fsckCommand(PdmDevAttributes::PDM_FSCK_AUTO, "btrfs", "sda1").empty());
}
