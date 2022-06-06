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

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include "init_cmds.h"
#include "init_service.h"
#include "init_service_manager.h"
#include "init_service_socket.h"
#include "param_stub.h"
#include "init_utils.h"
#include "securec.h"
#include "init_group_manager.h"
#include "trigger_manager.h"

using namespace testing::ext;
using namespace std;
namespace init_ut {
class ServiceUnitTest : public testing::Test {
public:
    static void SetUpTestCase(void)
    {
        string svcPath = "/data/init_ut/test_service";
        auto fp = std::unique_ptr<FILE, decltype(&fclose)>(fopen(svcPath.c_str(), "wb"), fclose);
        if (fp == nullptr) {
            cout << "ServiceUnitTest open : " << svcPath << " failed." << errno << endl;
        }
        sync();
    }

    static void TearDownTestCase(void) {};
    void SetUp() {};
    void TearDown() {};
};
HWTEST_F(ServiceUnitTest, TestDestoryHashMap, TestSize.Level1)
{
    HashMapDestory(GetInitWorkspace()->hashMap[0]);
}
HWTEST_F(ServiceUnitTest, case01, TestSize.Level1)
{
    const char *jsonStr = "{\"services\":{\"name\":\"test_service\",\"path\":[\"/data/init_ut/test_service\"],"
        "\"importance\":-20,\"uid\":\"system\",\"writepid\":[\"/dev/test_service\"],\"console\":1,"
        "\"gid\":[\"system\"], \"critical\":1}}";
    cJSON* jobItem = cJSON_Parse(jsonStr);
    ASSERT_NE(nullptr, jobItem);
    cJSON *serviceItem = cJSON_GetObjectItem(jobItem, "services");
    ASSERT_NE(nullptr, serviceItem);
    Service *service = AddService("test_service");
    int ret = ParseOneService(serviceItem, service);
    EXPECT_EQ(ret, 0);

    ret = ServiceStart(service);
    EXPECT_EQ(ret, 0);

    ret = ServiceStop(service);
    EXPECT_EQ(ret, 0);

    ReleaseService(service);
}

HWTEST_F(ServiceUnitTest, case02, TestSize.Level1)
{
    const char *jsonStr = "{\"services\":{\"name\":\"test_service8\",\"path\":[\"/data/init_ut/test_service\"],"
        "\"importance\":-20,\"uid\":\"system\",\"writepid\":[\"/dev/test_service\"],\"console\":1,"
        "\"gid\":[\"system\"],\"caps\":[10, 4294967295, 10000],\"cpucore\":[1]}}";
    cJSON* jobItem = cJSON_Parse(jsonStr);
    ASSERT_NE(nullptr, jobItem);
    cJSON *serviceItem = cJSON_GetObjectItem(jobItem, "services");
    ASSERT_NE(nullptr, serviceItem);
    Service *service = AddService("test_service8");
    int ret = ParseOneService(serviceItem, service);
    EXPECT_EQ(ret, 0);

    int *fds = (int *)malloc(sizeof(int) * 1); // ServiceStop will release fds
    UpdaterServiceFds(service, fds, 1);
    service->attribute = SERVICE_ATTR_ONDEMAND;
    ret = ServiceStart(service);
    EXPECT_EQ(ret, 0);
    CmdLines *cmdline = (CmdLines *)malloc(sizeof(CmdLines) + sizeof(CmdLine));
    cmdline->cmdNum = 1;
    cmdline->cmds[0].cmdIndex = 0;
    service->restartArg = cmdline;
    ServiceSocket tmpSock = { .next = nullptr, .sockFd = 0 };
    ServiceSocket tmpSock1 = { .next = &tmpSock, .sockFd = 0 };
    service->socketCfg = &tmpSock1;
    ServiceReap(service);
    service->socketCfg = nullptr;
    service->attribute &= SERVICE_ATTR_NEED_RESTART;
    service->firstCrashTime = 0;
    ServiceReap(service);
    DoCmdByName("reset ", "test_service8");
    // reset again
    DoCmdByName("reset ", "test_service8");
    service->pid = 0xfffffff; // 0xfffffff is not exist pid
    service->attribute = SERVICE_ATTR_TIMERSTART;
    ret = ServiceStop(service);
    EXPECT_NE(ret, 0);
    ReleaseService(service);
}

HWTEST_F(ServiceUnitTest, TestServiceStartAbnormal, TestSize.Level1)
{
    const char *jsonStr = "{\"services\":{\"name\":\"test_service1\",\"path\":[\"/data/init_ut/test_service\"],"
        "\"importance\":-20,\"uid\":\"system\",\"writepid\":[\"/dev/test_service\"],\"console\":1,"
        "\"gid\":[\"system\"]}}";
    cJSON* jobItem = cJSON_Parse(jsonStr);
    ASSERT_NE(nullptr, jobItem);
    cJSON *serviceItem = cJSON_GetObjectItem(jobItem, "services");
    ASSERT_NE(nullptr, serviceItem);
    Service *service = AddService("test_service1");
    ASSERT_NE(nullptr, service);
    int ret = ParseOneService(serviceItem, service);
    EXPECT_EQ(ret, 0);

    const char *path = "/data/init_ut/test_service_unused";
    ret = strncpy_s(service->pathArgs.argv[0], strlen(path) + 1, path, strlen(path));
    EXPECT_EQ(ret, 0);

    ret = ServiceStart(service);
    EXPECT_EQ(ret, -1);

    service->attribute &= SERVICE_ATTR_INVALID;
    ret = ServiceStart(service);
    EXPECT_EQ(ret, -1);

    service->pid = -1;
    ret = ServiceStop(service);
    EXPECT_EQ(ret, 0);
    ReleaseService(service);
}

HWTEST_F(ServiceUnitTest, TestServiceReap, TestSize.Level1)
{
    Service *service = AddService("test_service2");
    ASSERT_NE(nullptr, service);
    EXPECT_EQ(service->attribute, 0);
    service->attribute = SERVICE_ATTR_ONDEMAND;
    ServiceReap(service);
    service->attribute = 0;

    service->restartArg = (CmdLines *)calloc(1, sizeof(CmdLines));
    ASSERT_NE(nullptr, service->restartArg);
    ServiceReap(service);
    EXPECT_EQ(service->attribute, 0);

    const int crashCount = 241;
    service->crashCnt = crashCount;
    ServiceReap(service);
    EXPECT_EQ(service->attribute, 0);

    service->attribute |= SERVICE_ATTR_ONCE;
    ServiceReap(service);
    EXPECT_EQ(service->attribute, SERVICE_ATTR_ONCE);

    service->attribute = SERVICE_ATTR_CRITICAL;
    service->crashCount = 1;
    ServiceReap(service);
    ReleaseService(service);
}

HWTEST_F(ServiceUnitTest, TestServiceReapOther, TestSize.Level1)
{
    const char *serviceStr = "{\"services\":{\"name\":\"test_service4\",\"path\":[\"/data/init_ut/test_service\"],"
        "\"onrestart\":[\"sleep 1\"],\"console\":1,\"writepid\":[\"/dev/test_service\"]}}";

    cJSON* jobItem = cJSON_Parse(serviceStr);
    ASSERT_NE(nullptr, jobItem);
    cJSON *serviceItem = cJSON_GetObjectItem(jobItem, "services");
    ASSERT_NE(nullptr, serviceItem);
    Service *service = AddService("test_service4");
    ASSERT_NE(nullptr, service);
    int ret = ParseOneService(serviceItem, service);
    EXPECT_EQ(ret, 0);

    ServiceReap(service);
    EXPECT_NE(service->attribute, 0);

    service->attribute |= SERVICE_ATTR_CRITICAL;
    ServiceReap(service);
    EXPECT_NE(service->attribute, 0);

    service->attribute |= SERVICE_ATTR_NEED_STOP;
    ServiceReap(service);
    EXPECT_NE(service->attribute, 0);

    service->attribute |= SERVICE_ATTR_INVALID;
    ServiceReap(service);
    EXPECT_NE(service->attribute, 0);

    ret = ServiceStop(service);
    EXPECT_EQ(ret, 0);
    ReleaseService(service);
}

HWTEST_F(ServiceUnitTest, TestServiceManagerRelease, TestSize.Level1)
{
    Service *service = nullptr;
    ReleaseService(service);
    EXPECT_TRUE(service == nullptr);
    service = AddService("test_service5");
    ASSERT_NE(nullptr, service);
    service->pathArgs.argv = (char **)malloc(sizeof(char *));
    service->pathArgs.count = 1;
    const char *path = "/data/init_ut/test_service_release";
    service->pathArgs.argv[0] = strdup(path);

    service->writePidArgs.argv = (char **)malloc(sizeof(char *));
    service->writePidArgs.count = 1;
    service->writePidArgs.argv[0] = strdup(path);

    service->servPerm.caps = (unsigned int *)malloc(sizeof(unsigned int));
    service->servPerm.gIDArray = (gid_t *)malloc(sizeof(gid_t));
    service->socketCfg = (ServiceSocket *)malloc(sizeof(ServiceSocket));
    service->socketCfg->sockFd = 0;
    service->socketCfg->next = nullptr;
    service->fileCfg = (ServiceFile *)malloc(sizeof(ServiceFile));
    service->fileCfg->fd = 0;
    service->fileCfg->next = nullptr;
    ReleaseService(service);
    service = nullptr;
}

HWTEST_F(ServiceUnitTest, TestServiceManagerGetService, TestSize.Level1)
{
    Service *service = GetServiceByPid(1);
    StopAllServices(1, nullptr, 0, nullptr);
    EXPECT_TRUE(service == nullptr);
    const char *jsonStr = "{\"services\":{\"name\":\"test_service2\",\"path\":[\"/data/init_ut/test_service\"],"
        "\"importance\":-20,\"uid\":\"system\",\"writepid\":[\"/dev/test_service\"],\"console\":1,"
        "\"gid\":[\"system\"], \"critical\":[1,2]}}";
    cJSON* jobItem = cJSON_Parse(jsonStr);
    ASSERT_NE(nullptr, jobItem);
    cJSON *serviceItem = cJSON_GetObjectItem(jobItem, "services");
    ASSERT_NE(nullptr, serviceItem);
    service = AddService("test_service2");
    ASSERT_NE(nullptr, service);
    int ret = ParseOneService(serviceItem, service);
    EXPECT_NE(ret, 0);
    const char *jsonStr1 = "{\"services\":{\"name\":\"test_service3\",\"path\":[\"/data/init_ut/test_service\"],"
        "\"importance\":-20,\"uid\":\"system\",\"writepid\":[\"/dev/test_service\"],\"console\":1,"
        "\"gid\":[\"system\"], \"critical\":[\"on\"]}}";
    jobItem = cJSON_Parse(jsonStr1);
    ASSERT_NE(nullptr, jobItem);
    serviceItem = cJSON_GetObjectItem(jobItem, "services");
    ASSERT_NE(nullptr, serviceItem);
    service = AddService("test_service3");
    ASSERT_NE(nullptr, service);
    ret = ParseOneService(serviceItem, service);
    EXPECT_NE(ret, 0);
}

HWTEST_F(ServiceUnitTest, TestServiceExec, TestSize.Level1)
{
    Service *service = AddService("test_service7");
    ASSERT_NE(nullptr, service);

    service->pathArgs.argv = (char **)malloc(sizeof(char *));
    ASSERT_NE(service->pathArgs.argv, nullptr);
    service->pathArgs.count = 1;
    const char *path = "/data/init_ut/test_service_release";
    service->pathArgs.argv[0] = strdup(path);

    service->importance = 20;
    int ret = ServiceExec(service);
    EXPECT_EQ(ret, 0);

    const int invalidImportantValue = 20;
    ret = SetImportantValue(service, "", invalidImportantValue, 1);
    EXPECT_EQ(ret, -1);
    ReleaseService(service);
}
} // namespace init_ut
