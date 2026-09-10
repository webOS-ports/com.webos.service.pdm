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

#include <csignal>
#include <cstdlib>
#include <experimental/filesystem>
#include <string>
#include <vector>

#include "PdmUtils.h"

namespace fs = std::experimental::filesystem;

namespace {

/* A directory of our own so the exec tests can look for files by name. */
class ScratchDir
{
public:
    ScratchDir()
    {
        char tmpl[] = "/tmp/pdm-test-XXXXXX";
        const char *made = mkdtemp(tmpl);
        EXPECT_NE(made, nullptr);
        if (made)
            mPath = made;
    }

    ~ScratchDir()
    {
        if (!mPath.empty()) {
            std::error_code ignored;
            fs::remove_all(fs::path(mPath), ignored);
        }
    }

    std::string file(const std::string &name) const { return mPath + "/" + name; }
    const std::string &path() const { return mPath; }

private:
    std::string mPath;
};

bool exists(const std::string &path)
{
    std::error_code ignored;
    return fs::exists(fs::path(path), ignored);
}

} // namespace

// ---------------------------------------------------------------------------
// toInt: udev hands us strings from the device, and std::stoi throws on them.
// ---------------------------------------------------------------------------

TEST(PdmUtilsToInt, ParsesPlainNumbers)
{
    EXPECT_EQ(PdmUtils::toInt("0"), 0);
    EXPECT_EQ(PdmUtils::toInt("12"), 12);
    EXPECT_EQ(PdmUtils::toInt("-7"), -7);
    EXPECT_EQ(PdmUtils::toInt("480"), 480);
}

TEST(PdmUtilsToInt, StopsAtTrailingGarbageLikeStoi)
{
    // SPEED is "480" but ID_INSTANCE-style values carry suffixes.
    EXPECT_EQ(PdmUtils::toInt("12abc"), 12);
    EXPECT_EQ(PdmUtils::toInt("  34  "), 34);
}

TEST(PdmUtilsToInt, ReturnsDefaultInsteadOfThrowing)
{
    // Each of these makes std::stoi throw; before PdmUtils::toInt existed the
    // exception escaped a CommandManager worker and terminated the daemon.
    EXPECT_EQ(PdmUtils::toInt(""), 0);
    EXPECT_EQ(PdmUtils::toInt("abc"), 0);
    EXPECT_EQ(PdmUtils::toInt("-"), 0);
    EXPECT_EQ(PdmUtils::toInt("0x1f"), 0);          // parses as 0, not 31
    EXPECT_EQ(PdmUtils::toInt("99999999999999999999"), 0);  // out_of_range
    EXPECT_EQ(PdmUtils::toInt("not a number", -1), -1);
    EXPECT_EQ(PdmUtils::toInt("", 42), 42);
}

TEST(PdmUtilsToInt, NeverThrows)
{
    const char *nasty[] = {"", " ", "+", "-", "..", "1e999", "\n", "\xff\xfe"};
    for (const char *value : nasty)
        EXPECT_NO_THROW(PdmUtils::toInt(value)) << "input: " << value;
}

// ---------------------------------------------------------------------------
// splitArgs
// ---------------------------------------------------------------------------

TEST(PdmUtilsSplitArgs, SplitsOnRunsOfWhitespace)
{
    EXPECT_EQ(PdmUtils::splitArgs("mkfs.ext4 -F "),
              (std::vector<std::string>{"mkfs.ext4", "-F"}));
    EXPECT_EQ(PdmUtils::splitArgs("  timeout   20  "),
              (std::vector<std::string>{"timeout", "20"}));
    EXPECT_EQ(PdmUtils::splitArgs("/usr/bin/fusermount -u "),
              (std::vector<std::string>{"/usr/bin/fusermount", "-u"}));
}

TEST(PdmUtilsSplitArgs, EmptyInputGivesNoArgs)
{
    EXPECT_TRUE(PdmUtils::splitArgs("").empty());
    EXPECT_TRUE(PdmUtils::splitArgs("   ").empty());
}

// ---------------------------------------------------------------------------
// runCommand
// ---------------------------------------------------------------------------

TEST(PdmUtilsRunCommand, ReturnsTheExitStatusNotAWaitStatus)
{
    // system() would return 7 << 8 here. Every caller compares against small
    // numbers - PdmFsck against 1, 2 and 124 - so this has to be the real
    // exit code.
    EXPECT_EQ(PdmUtils::runCommand({"/bin/sh", "-c", "exit 0"}), 0);
    EXPECT_EQ(PdmUtils::runCommand({"/bin/sh", "-c", "exit 1"}), 1);
    EXPECT_EQ(PdmUtils::runCommand({"/bin/sh", "-c", "exit 2"}), 2);
    EXPECT_EQ(PdmUtils::runCommand({"/bin/sh", "-c", "exit 7"}), 7);
    EXPECT_EQ(PdmUtils::runCommand({"/bin/sh", "-c", "exit 124"}), 124);
}

TEST(PdmUtilsRunCommand, MissingBinaryIs127)
{
    EXPECT_EQ(PdmUtils::runCommand({"/nonexistent/pdm-test-binary"}), 127);
}

TEST(PdmUtilsRunCommand, KilledChildReports128PlusSignal)
{
    EXPECT_EQ(PdmUtils::runCommand({"/bin/sh", "-c", "kill -TERM $$"}), 128 + SIGTERM);
}

TEST(PdmUtilsRunCommand, RejectsAnEmptyCommand)
{
    EXPECT_EQ(PdmUtils::runCommand({}), -1);
    EXPECT_EQ(PdmUtils::runCommand({""}), -1);
}

TEST(PdmUtilsRunCommand, PassesArgumentsThroughVerbatim)
{
    ScratchDir dir;
    // Every one of these is a legal filename and a shell nightmare. No slashes,
    // so touch does not need any of it to be a directory.
    for (const std::string &awkward : {std::string("a b\tc'd\"e$f"),
                                       std::string("label; touch PWNED"),
                                       std::string("label && touch PWNED"),
                                       std::string("label | touch PWNED"),
                                       std::string("label`touch PWNED`"),
                                       std::string("label$(touch PWNED)"),
                                       std::string("*"),
                                       std::string("$HOME")}) {
        ASSERT_EQ(PdmUtils::runCommand({"touch", dir.file(awkward)}), 0)
            << "argument: " << awkward;
        // Arrived as one argument, uninterpreted and unexpanded.
        EXPECT_TRUE(exists(dir.file(awkward))) << "argument: " << awkward;
    }
    // Nothing in there ran as a command.
    EXPECT_FALSE(exists(dir.file("PWNED")));
}

// The regression test for the setVolumeLabel/format shell injection: each of
// these was two commands when the argv was pasted into a system() string.
// runCommand never builds a command line, so the marker must not appear.
TEST(PdmUtilsRunCommand, DoesNotInterpretShellMetacharacters)
{
    ScratchDir dir;
    const std::string injected = dir.file("PWNED");

    const std::vector<std::string> attempts = {
        "label; touch " + injected,
        "label && touch " + injected,
        "label | touch " + injected,
        "label\ntouch " + injected,
        "label`touch " + injected + "`",
        "label$(touch " + injected + ")",
        "label & touch " + injected,
        "label\n/bin/touch " + injected + "\n",
    };

    for (const std::string &attempt : attempts) {
        // true(1) ignores its arguments, so the only way the marker can appear
        // is if something interpreted the string as shell.
        ASSERT_EQ(PdmUtils::runCommand({"true", attempt}), 0) << "attempt: " << attempt;
        EXPECT_FALSE(exists(injected)) << "a shell ran for: " << attempt;
    }
}

// ---------------------------------------------------------------------------
// string helpers
// ---------------------------------------------------------------------------

TEST(PdmUtilsStrings, TrimsBothEnds)
{
    std::string value = "  spaced out \t\n";
    EXPECT_EQ(PdmUtils::trimString(value), "spaced out");

    std::string empty;
    EXPECT_EQ(PdmUtils::trimString(empty), "");
}

TEST(PdmUtilsStrings, SplitsOnTheFirstColon)
{
    EXPECT_EQ(PdmUtils::splitStringInTwo("Model Family: SanDisk"),
              std::make_pair(std::string("Model Family"), std::string("SanDisk")));
    // No colon: everything is the first half.
    EXPECT_EQ(PdmUtils::splitStringInTwo("no colon here"),
              std::make_pair(std::string("no colon here"), std::string("")));
}
