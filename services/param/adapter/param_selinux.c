/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "init_utils.h"
#include "param_manager.h"
#include "param_security.h"
#include "param_utils.h"
#include "param_base.h"
#ifdef PARAM_SUPPORT_SELINUX
#include "selinux_parameter.h"
#include <policycoreutils.h>
#include <selinux/selinux.h>
#endif

#ifdef __aarch64__
#define CHECKER_LIB_NAME "/system/lib64/libparaperm_checker.z.so"
#define CHECKER_UPDATER_LIB_NAME "/lib64/libparaperm_checker.z.so"
#else
#define CHECKER_LIB_NAME "/system/lib/libparaperm_checker.z.so"
#define CHECKER_UPDATER_LIB_NAME "/lib/libparaperm_checker.z.so"
#endif

static SelinuxSpace g_selinuxSpace = {0};
static int InitLocalSecurityLabel(ParamSecurityLabel *security, int isInit)
{
    UNUSED(isInit);
    PARAM_CHECK(security != NULL, return -1, "Invalid security");
    security->cred.pid = getpid();
    security->cred.uid = geteuid();
    security->cred.gid = getegid();
    security->flags[PARAM_SECURITY_SELINUX] = 0;
    PARAM_LOGV("InitLocalSecurityLabel");
#if !(defined STARTUP_INIT_TEST || defined LOCAL_TEST)
    if (g_selinuxSpace.selinuxHandle == NULL) {
        const char *libname = (InUpdaterMode() == 1) ? CHECKER_UPDATER_LIB_NAME : CHECKER_LIB_NAME;
        g_selinuxSpace.selinuxHandle = dlopen(libname, RTLD_LAZY);
        PARAM_CHECK(g_selinuxSpace.selinuxHandle != NULL,
            return 0, "Failed to dlsym selinuxHandle, %s", dlerror());
    }
    void *handle = g_selinuxSpace.selinuxHandle;
    if (g_selinuxSpace.setParamCheck == NULL) {
        g_selinuxSpace.setParamCheck = (SelinuxSetParamCheck)dlsym(handle, "SetParamCheck");
        PARAM_CHECK(g_selinuxSpace.setParamCheck != NULL, return -1, "Failed to dlsym setParamCheck %s", dlerror());
    }
    if (g_selinuxSpace.getParamList == NULL) {
        g_selinuxSpace.getParamList = (ParamContextsList *(*)()) dlsym(handle, "GetParamList");
        PARAM_CHECK(g_selinuxSpace.getParamList != NULL, return -1, "Failed to dlsym getParamList %s", dlerror());
    }
    if (g_selinuxSpace.getParamLabel == NULL) {
        g_selinuxSpace.getParamLabel = (const char * (*)(const char *))dlsym(handle, "GetParamLabel");
        PARAM_CHECK(g_selinuxSpace.getParamLabel != NULL, return -1, "Failed to dlsym getParamLabel %s", dlerror());
    }
    if (g_selinuxSpace.initParamSelinux == NULL) {
        g_selinuxSpace.initParamSelinux = (void (*)())dlsym(handle, "InitParamSelinux");
        PARAM_CHECK(g_selinuxSpace.initParamSelinux != NULL, return -1, "Failed to dlsym initParamSelinux ");
    }
    if (g_selinuxSpace.readParamCheck == NULL) {
        g_selinuxSpace.readParamCheck = (int (*)(const char *))dlsym(handle, "ReadParamCheck");
    }
    if (g_selinuxSpace.destroyParamList == NULL) {
        g_selinuxSpace.destroyParamList =
            (void (*)(ParamContextsList **))dlsym(handle, "DestroyParamList");
        PARAM_CHECK(g_selinuxSpace.destroyParamList != NULL,
            return -1, "Failed to dlsym destroyParamList %s", dlerror());
    }
    g_selinuxSpace.initParamSelinux();
#endif
    PARAM_LOGV("Load selinux lib success.");
    return 0;
}

static int FreeLocalSecurityLabel(ParamSecurityLabel *srcLabel)
{
    return 0;
}

static void SetSelinuxFileCon(const char *name, const char *context)
{
    static char buffer[FILENAME_LEN_MAX] = {0};
    int len = ParamSprintf(buffer, sizeof(buffer), "%s/%s", PARAM_STORAGE_PATH, context);
    if (len > 0) {
        buffer[len] = '\0';
        PARAM_LOGI("setfilecon name %s path: %s %s ", name, context, buffer);
        if (setfilecon(buffer, context) < 0) {
            PARAM_LOGE("Failed to setfilecon %s ", context);
        }
    }
}

static int SelinuxGetAllLabel(int readOnly)
{
    PARAM_CHECK(g_selinuxSpace.getParamList != NULL, return DAC_RESULT_FORBIDED, "Invalid getParamList");
    ParamContextsList *head = g_selinuxSpace.getParamList();
    ParamContextsList *node = head;

    int count = 0;
    while (node != NULL) {
        PARAM_LOGV("GetParamSecurityLabel name %s content %s", node->info.paraName, node->info.paraContext);
        if (node->info.paraContext == NULL || node->info.paraName == NULL) {
            node = node->next;
            continue;
        }
        int ret = AddWorkSpace(node->info.paraContext, readOnly, PARAM_WORKSPACE_DEF);
        if (ret != 0) {
            PARAM_LOGE("Forbid to add selinux workspace %s %s", node->info.paraName, node->info.paraContext);
            node = node->next;
            continue;
        }
        count++;
        if (readOnly != 0) {
            node = node->next;
            continue;
        }
        // set selinx label
        SetSelinuxFileCon(node->info.paraName, node->info.paraContext);
        node = node->next;
    }

    int ret = AddWorkSpace(WORKSPACE_NAME_DEF_SELINUX, readOnly, PARAM_WORKSPACE_MAX);
    PARAM_CHECK(ret == 0, return -1,
        "Failed to add selinux workspace %s", WORKSPACE_NAME_DEF_SELINUX);
    if (readOnly == 0) {
        SetSelinuxFileCon(WORKSPACE_NAME_DEF_SELINUX, WORKSPACE_NAME_DEF_SELINUX);
    }
    PARAM_LOGI("SelinuxGetAllLabel count %d", count);
    return 0;
}

static int SelinuxGetParamSecurityLabel(const char *path)
{
    UNUSED(path);
    return SelinuxGetAllLabel(0);
}

static int CheckFilePermission(const ParamSecurityLabel *localLabel, const char *fileName, int flags)
{
    UNUSED(flags);
    PARAM_CHECK(localLabel != NULL && fileName != NULL, return -1, "Invalid param");
    return 0;
}

static int SelinuxReadParamCheck(const char *name)
{
    int ret = DAC_RESULT_FORBIDED;
    if (g_selinuxSpace.readParamCheck != NULL) {
        ret = g_selinuxSpace.readParamCheck(name);
        PARAM_LOGI("SelinuxReadParamCheck name %s ret %d", name, ret);
    }
    const char *label = GetSelinuxContent(name);
    if (label == NULL) { // open file with readonly
        ret = AddWorkSpace(WORKSPACE_NAME_DEF_SELINUX, 1, PARAM_WORKSPACE_MAX);
    } else {
        PARAM_LOGI("SelinuxReadParamCheck name %s label %s", name, label);
        ret = AddWorkSpace(label, 1, PARAM_WORKSPACE_MAX);
    }
    if (ret != 0) {
        return DAC_RESULT_FORBIDED;
    }
    return DAC_RESULT_PERMISSION;
}

static int SelinuxCheckParamPermission(const ParamSecurityLabel *srcLabel, const char *name, uint32_t mode)
{
    int ret = DAC_RESULT_FORBIDED;
    PARAM_CHECK(g_selinuxSpace.setParamCheck != NULL, return ret, "Invalid setParamCheck");
    // check
    struct ucred uc;
    uc.pid = srcLabel->cred.pid;
    uc.uid = srcLabel->cred.uid;
    uc.gid = srcLabel->cred.gid;
    if (mode == DAC_WRITE) {
        ret = g_selinuxSpace.setParamCheck(name, &uc);
    } else {
#ifndef STARTUP_INIT_TEST
        ret = SelinuxReadParamCheck(name);
#else
        ret = g_selinuxSpace.readParamCheck(name);
#endif
    }
    if (ret != 0) {
        PARAM_LOGW("Selinux check name %s pid %d uid %d %d result %d", name, uc.pid, uc.uid, uc.gid, ret);
        ret = DAC_RESULT_FORBIDED;
    } else {
        ret = DAC_RESULT_PERMISSION;
    }
    return ret;
}

static int UpdaterCheckParamPermission(const ParamSecurityLabel *srcLabel, const char *name, uint32_t mode)
{
    return DAC_RESULT_PERMISSION;
}

INIT_LOCAL_API int RegisterSecuritySelinuxOps(ParamSecurityOps *ops, int isInit)
{
    PARAM_CHECK(ops != NULL, return -1, "Invalid param");
    int ret = ParamStrCpy(ops->name, sizeof(ops->name), "selinux");
    ops->securityGetLabel = NULL;
    ops->securityInitLabel = InitLocalSecurityLabel;
    ops->securityCheckFilePermission = CheckFilePermission;
    if (InUpdaterMode() == 1) {
        ops->securityCheckParamPermission = UpdaterCheckParamPermission;
    } else {
        ops->securityCheckParamPermission = SelinuxCheckParamPermission;
    }
    ops->securityFreeLabel = FreeLocalSecurityLabel;
    if (isInit != 0) {
        ops->securityGetLabel = SelinuxGetParamSecurityLabel;
    }
    return ret;
}

INIT_INNER_API const char *GetSelinuxContent(const char *name)
{
    if (g_selinuxSpace.getParamLabel != NULL) {
        return g_selinuxSpace.getParamLabel(name);
    } else {
        PARAM_LOGE("Can not init selinux");
        return WORKSPACE_NAME_DEF_SELINUX;
    }
}

INIT_LOCAL_API void OpenPermissionWorkSpace(void)
{
    // open workspace by readonly
    SelinuxGetAllLabel(1);
}

#if defined STARTUP_INIT_TEST || defined LOCAL_TEST
void SetSelinuxOps(const SelinuxSpace *space)
{
    g_selinuxSpace.setSelinuxLogCallback = space->setSelinuxLogCallback;
    g_selinuxSpace.setParamCheck = space->setParamCheck;
    g_selinuxSpace.getParamLabel = space->getParamLabel;
    g_selinuxSpace.readParamCheck = space->readParamCheck;
    g_selinuxSpace.getParamList = space->getParamList;
    g_selinuxSpace.destroyParamList = space->destroyParamList;
}
#endif