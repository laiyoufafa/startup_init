/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ueventd_read_cfg.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "init_utils.h"
#include "list.h"
#include "ueventd_utils.h"
#include "securec.h"
#define INIT_LOG_TAG "ueventd"
#include "init_log.h"

// default item count in config files
#define DEFAULTITEMCOUNT (50)

#define SYS_CONFIG_PATH_NUM 0
#define SYS_CONFIG_ATTR_NUM 1
#define SYS_CONFIG_MODE_NUM 2
#define SYS_CONFIG_UID_NUM 3
#define SYS_CONFIG_GID_NUM 4

#define DEVICE_CONFIG_NAME_NUM 0
#define DEVICE_CONFIG_MODE_NUM 1
#define DEVICE_CONFIG_UID_NUM 2
#define DEVICE_CONFIG_GID_NUM 3

typedef enum SECTION {
    SECTION_INVALID = -1,
    SECTION_DEVICE = 0,
    SECTION_SYSFS,
    SECTION_FIRMWARE
} SECTION;

typedef int (*ParseConfigFunc)(char *);
typedef struct FunctionMapper {
    char *name;
    ParseConfigFunc func;
} FUNCTIONMAPPER;

struct ListNode g_devices = {
    .next = &g_devices,
    .prev = &g_devices,
};

struct ListNode g_sysDevices = {
    .next = &g_sysDevices,
    .prev = &g_sysDevices,
};

struct ListNode g_firmwares = {
    .next = &g_firmwares,
    .prev = &g_firmwares,
};

static int ParseDeviceConfig(char *p)
{
    INIT_LOGD("Parse device config info: %s", p);
    char **items = NULL;
    int count = -1;
    // format: <device node> <mode> <uid> <gid>
    const int expectedCount = 4;

    if (INVALIDSTRING(p)) {
        INIT_LOGE("Invalid argument");
    }
    items = SplitStringExt(p, " ", &count, expectedCount);
    if (count != expectedCount) {
        INIT_LOGE("Ignore invalid item: %s", p);
        FreeStringVector(items, count);
        return 0;
    }

    struct DeviceUdevConf *config = calloc(1, sizeof(struct DeviceUdevConf));
    if (config == NULL) {
        errno = ENOMEM;
        FreeStringVector(items, count);
        return -1;
    }
    config->name = strdup(items[DEVICE_CONFIG_NAME_NUM]); // device node
    errno = 0;
    config->mode = strtoul(items[DEVICE_CONFIG_MODE_NUM], NULL, OCTAL_BASE);
    if (errno != 0) {
        INIT_LOGE("Invalid mode in config file for device node %s. use default mode", config->name);
        config->mode = DEVMODE;
    }
    config->uid = (uid_t)StringToInt(items[DEVICE_CONFIG_UID_NUM], 0);
    config->gid = (gid_t)StringToInt(items[DEVICE_CONFIG_GID_NUM], 0);
    ListAddTail(&g_devices, &config->list);
    FreeStringVector(items, count);
    return 0;
}

static int ParseSysfsConfig(char *p)
{
    INIT_LOGD("Parse sysfs config info: %s", p);
    char **items = NULL;
    int count = -1;
    // format: <syspath> <attribute> <mode> <uid> <gid>
    const int expectedCount = 5;

    if (INVALIDSTRING(p)) {
        INIT_LOGE("Invalid argument");
    }
    items = SplitStringExt(p, " ", &count, expectedCount);
    if (count != expectedCount) {
        INIT_LOGE("Ignore invalid item: %s", p);
        FreeStringVector(items, count);
        return 0;
    }
    struct SysUdevConf *config = calloc(1, sizeof(struct SysUdevConf));
    if (config == NULL) {
        errno = ENOMEM;
        FreeStringVector(items, count);
        return -1;
    }
    config->sysPath = strdup(items[SYS_CONFIG_PATH_NUM]); // sys path
    config->attr = strdup(items[SYS_CONFIG_ATTR_NUM]);  // attribute
    errno = 0;
    config->mode = strtoul(items[SYS_CONFIG_MODE_NUM], NULL, OCTAL_BASE);
    if (errno != 0) {
        INIT_LOGE("Invalid mode in config file for sys path %s. use default mode", config->sysPath);
        config->mode = DEVMODE;
    }
    config->uid = (uid_t)StringToInt(items[SYS_CONFIG_UID_NUM], 0);
    config->gid = (gid_t)StringToInt(items[SYS_CONFIG_GID_NUM], 0);
    ListAddTail(&g_sysDevices, &config->list);
    FreeStringVector(items, count);
    return 0;
}

static int ParseFirmwareConfig(char *p)
{
    INIT_LOGD("Parse firmware config info: %s", p);
    if (INVALIDSTRING(p)) {
        INIT_LOGE("Invalid argument");
    }

    // Sanity checks
    struct stat st = {};
    if (stat(p, &st) != 0) {
        INIT_LOGE("Invalid firware file: %s, err = %d", p, errno);
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        INIT_LOGE("Expect directory in firmware config");
        return -1;
    }

    struct FirmwareUdevConf *config = calloc(1, sizeof(struct FirmwareUdevConf));
    if (config == NULL) {
        errno = ENOMEM;
        return -1;
    }

    config->fmPath = strdup(p);
    ListAddTail(&g_firmwares, &config->list);
    return 0;
}

static SECTION GetSection(const char *section)
{
    if (INVALIDSTRING(section)) {
        return SECTION_INVALID;
    }

    if (STRINGEQUAL(section, "device")) {
        return SECTION_DEVICE;
    } else if (STRINGEQUAL(section, "sysfs")) {
        return SECTION_SYSFS;
    } else if (STRINGEQUAL(section, "firmware")) {
        return SECTION_FIRMWARE;
    } else {
        return SECTION_INVALID;
    }
}

static FUNCTIONMAPPER funcMapper[3] = {
    {"device", ParseDeviceConfig},
    {"sysfs", ParseSysfsConfig},
    {"firmware", ParseFirmwareConfig}
};

ParseConfigFunc callback;
int ParseUeventConfig(char *buffer)
{
    char *section = NULL;
    char *right = NULL;
    char *p = buffer;
    SECTION type;

    if (INVALIDSTRING(buffer)) {
        return -1;
    }
    if (*p == '[') {
        p++;
        if ((right = strchr(p, ']')) == NULL) {
            INIT_LOGE("Invalid line \"%s\", miss ']'", buffer);
            return -1;
        }
        *right = '\0'; // Replace ']' with '\0';
        section = p;
        INIT_LOGD("section is [%s]", section);
        if ((type = GetSection(section)) == SECTION_INVALID) {
            INIT_LOGE("Invalid section \" %s \"", section);
            callback = NULL; // reset callback
            return -1;
        }
        callback = funcMapper[type].func;
        return 0;
    }
    return callback != NULL ? callback(p) : -1;
}

static void DoUeventConfigParse(char *buffer, size_t length)
{
    if (length < 0) {
        return;
    }
    char **items = NULL;
    int count = -1;
    const int maxItemCount = DEFAULTITEMCOUNT;

    items = SplitStringExt(buffer, "\n", &count, maxItemCount);
    INIT_LOGD("Dump items count = %d", count);
    for (int i = 0; i < count; i++) {
        char *p = items[i];
        // Skip lead white space
        while (isspace(*p)) {
            p++;
        }

        // Skip comment or empty line
        if (*p == '\0' || *p == '#') {
            continue;
        }
        int rc = ParseUeventConfig(p);
        if (rc < 0) {
            INIT_LOGE("Parse uevent config from %s failed", p);
        }
    }
    // release memory
    FreeStringVector(items, count);
}

void ParseUeventdConfigFile(const char *file)
{
    if (INVALIDSTRING(file)) {
        return;
    }
    char *config = GetRealPath(file);
    if (config == NULL) {
        return;
    }
    int fd = open(config, O_RDONLY | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    free(config);
    if (fd < 0) {
        INIT_LOGE("Read from %s failed", file);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        INIT_LOGE("Failed to get file stat. err = %d", errno);
        close(fd);
        fd = -1;
        return;
    }

    // st_size should never be less than 0
    size_t size = (size_t)st.st_size;
    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        INIT_LOGE("Failed to malloc memory. err = %d", errno);
        close(fd);
        return;
    }

    if (read(fd, buffer, size) != (ssize_t)size) {
        INIT_LOGE("Read from file %s failed. err = %d", file, errno);
        free(buffer);
        buffer = NULL;
        close(fd);
        fd = -1;
        return;
    }

    buffer[size] = '\0';
    DoUeventConfigParse(buffer, size);
    free(buffer);
    buffer = NULL;
    close(fd);
    fd = -1;
}

void GetDeviceNodePermissions(const char *devNode, uid_t *uid, gid_t *gid, mode_t *mode)
{
    if (INVALIDSTRING(devNode)) {
        return;
    }

    struct ListNode *node = NULL;
    if (!ListEmpty(g_devices)) {
        ForEachListEntry(&g_devices, node) {
            struct DeviceUdevConf *config = ListEntry(node, struct DeviceUdevConf, list);
            if (STRINGEQUAL(config->name, devNode)) {
                *uid = config->uid;
                *gid = config->gid;
                *mode = config->mode;
                break;
            }
        }
    }
    return;
}

void ChangeSysAttributePermissions(const char *sysPath)
{
    if (INVALIDSTRING(sysPath)) {
        return;
    }

    struct ListNode *node = NULL;
    struct SysUdevConf *config = NULL;
    int matched = 0;
    if (!ListEmpty(g_sysDevices)) {
        ForEachListEntry(&g_sysDevices, node) {
            config = ListEntry(node, struct SysUdevConf, list);
            if (STRINGEQUAL(config->sysPath, sysPath)) {
                matched = 1;
                break;
            }
        }
    }

    if (matched == 0) {
        return;
    }
    char sysAttr[SYSPATH_SIZE] = {};
    if (snprintf_s(sysAttr, SYSPATH_SIZE, SYSPATH_SIZE - 1, "/sys%s/%s", config->sysPath, config->attr) == -1) {
        INIT_LOGE("Failed to build sys attribute for sys path %s, attr: %s", config->sysPath, config->attr);
        return;
    }
    if (chown(sysAttr, config->uid, config->gid) < 0) {
        INIT_LOGE("chown for file %s failed, err = %d", sysAttr, errno);
    }

    if (chmod(sysAttr, config->mode) < 0) {
        INIT_LOGE("[uevent][error] chmod for file %s failed, err = %d", sysAttr, errno);
    }
}
