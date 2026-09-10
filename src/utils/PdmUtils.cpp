// Copyright (c) 2019-2023 LG Electronics, Inc.
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

#include <array>
#include <sstream>
#include <experimental/filesystem>
#include <memory>
#include <stdexcept>
#include "Common.h"
#include "PdmUtils.h"
#include "PdmLogUtils.h"
#include <algorithm>
#include <locale>

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

namespace fs = std::experimental::filesystem;

uid_t PdmUtils::get_uid(const char *user_name)
{
    uid_t          uid = 0;
    struct passwd *pwd;

    pwd = getpwnam(user_name);
    if (pwd == NULL) {
        PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: Failed to get uid", __FUNCTION__, __LINE__);
        return 0;
    }
    uid = pwd->pw_uid;

    return uid;
}

gid_t PdmUtils::get_gid(const char *group_name)
{
    gid_t          gid = 0;
    struct group  *grp;

    grp = getgrnam(group_name);
    if (grp == NULL) {
        PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: Failed to get gid", __FUNCTION__, __LINE__);
        return 0;
    }
    gid = grp->gr_gid;

    return gid;
}

void PdmUtils::do_chown(const char *file_path, const char *user_name, const char *group_name)
{
    uid_t          uid = 0;
    gid_t          gid = 0;
    struct passwd *pwd;
    struct group  *grp;

    pwd = getpwnam(user_name);
    if (pwd == NULL) {
        PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: Failed to get uid", __FUNCTION__, __LINE__);
        return;
    }
    uid = pwd->pw_uid;

    grp = getgrnam(group_name);
    if (grp == NULL) {
        PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: Failed to get gid", __FUNCTION__, __LINE__);
        return;
    }
    gid = grp->gr_gid;

    if (chown(file_path, uid, gid) == -1) {
        PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: chown fail. errno: %d strerror: %s", __FUNCTION__, __LINE__, errno, strerror(errno));
    }
}

std::string PdmUtils::execShellCmd(const std::string &cmd)
{
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
            result += buffer.data();
    }
    return result;
}

bool PdmUtils::createDir(const std::string &dirName)
{
    bool ret = false;

    if(dirName.empty())
        return ret;

    ret = fs::is_directory(fs::path(dirName));

    if(!ret){
        try{
            ret = fs::create_directories(fs::path(dirName));
        }
        catch(const fs::filesystem_error& error){
            PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: %s ", __FUNCTION__, __LINE__, error.what());
        }
    }

    return ret;
}

bool PdmUtils::removeDirRecursive(const std::string &dirPath)
{
    bool ret = false;

    if(!dirPath.empty())
    {
        try{
            ret = fs::remove_all(fs::path(dirPath));
        }
        catch(const fs::filesystem_error& error){
            PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: %s ", __FUNCTION__, __LINE__, error.what());
        }
    }

    return ret;
}

bool PdmUtils::removeFile(const std::string &file)
{
    bool ret = false;

    if(!file.empty())
    {
        try{
            ret = fs::remove(fs::path(file));
            if(!ret)
                PDM_LOG_CRITICAL("PdmUtils:%s line: %d not remove : %s", __FUNCTION__, __LINE__, file.c_str());
        }
        catch(const fs::filesystem_error& error){
            PDM_LOG_CRITICAL("PdmUtils:%s line: %d error: %s ", __FUNCTION__, __LINE__, error.what());
        }
    }

    return ret;
}

std::string& PdmUtils::ltrimString(std::string &str)
{
    if(!str.empty()) {
        auto it2 =  std::find_if( str.begin() , str.end() , [](char ch){ return !std::isspace<char>(ch , std::locale::classic() ) ; } );
        str.erase( str.begin() , it2);
    }
    return str;
}

std::string& PdmUtils::rtrimString(std::string &str)
{
    if(!str.empty()) {
        auto it1 =  std::find_if( str.rbegin() , str.rend() , [](char ch){ return !std::isspace<char>(ch , std::locale::classic() ) ; } );
        str.erase( it1.base() , str.end() );
    }
    return str;
}

std::string& PdmUtils::trimString(std::string &str)
{
    return ltrimString(rtrimString(str));
}

std::pair<std::string, std::string> PdmUtils::splitStringInTwo(std::string stringToSplit)
{
    std::string splitString;
    if(!stringToSplit.empty()) {
        std::string::size_type pos = stringToSplit.find(':');
        if(stringToSplit.npos != pos) {
            splitString = stringToSplit.substr(pos + 1);
            stringToSplit = stringToSplit.substr(0, pos);
        }
    }
    return make_pair(trimString(stringToSplit), trimString(splitString));
}

int PdmUtils::toInt(const std::string &str, int defaultValue)
{
    if (str.empty())
        return defaultValue;

    try {
        return std::stoi(str, nullptr);
    }
    catch (const std::exception &error) {
        PDM_LOG_WARNING("PdmUtils:%s line: %d not a number: \"%s\" (%s)", __FUNCTION__, __LINE__, str.c_str(), error.what());
        return defaultValue;
    }
}

std::vector<std::string> PdmUtils::splitArgs(const std::string &str)
{
    std::vector<std::string> args;
    std::istringstream stream(str);
    std::string arg;

    while (stream >> arg)
        args.push_back(arg);

    return args;
}

int PdmUtils::runCommand(const std::vector<std::string> &argv)
{
    if (argv.empty() || argv.front().empty()) {
        PDM_LOG_ERROR("PdmUtils:%s line: %d empty command", __FUNCTION__, __LINE__);
        return -1;
    }

    /* Built before the fork: everything between fork() and execvp() has to be
     * async-signal-safe, and pdm is multi-threaded. */
    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const std::string &arg : argv)
        args.push_back(const_cast<char *>(arg.c_str()));
    args.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        PDM_LOG_ERROR("PdmUtils:%s line: %d fork failed: %s", __FUNCTION__, __LINE__, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execvp(args[0], args.data());
        _exit(127);         /* same convention as a shell that cannot exec */
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            PDM_LOG_ERROR("PdmUtils:%s line: %d waitpid failed: %s", __FUNCTION__, __LINE__, strerror(errno));
            return -1;
        }
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

unsigned int PdmUtils::getPIDbyName(const char *processName)
{
    unsigned int pidValue = 0;

    if (processName != NULL) {
        std::string sysCommand("pidof -s ");
        sysCommand.append(processName);
        std::string result = PdmUtils::execShellCmd(sysCommand);
        pidValue = strtoul(result.c_str(), NULL, 10);
    }
    return pidValue;
}
