/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "systemgetparametercommitid_fuzzer.h"
#include "fuzz_utils.h"
#include "sys_param.h"

namespace OHOS {
    bool FuzzSystemGetParameterCommitId(const uint8_t* data, size_t size)
    {
        bool result = false;
        uint32_t commitId = 0;
        char *rest = nullptr;
        ParamHandle handle = (ParamHandle)strtoul(reinterpret_cast<char *>(const_cast<uint8_t *>(data)), &rest, BASE);
        if (!SystemGetParameterCommitId(handle, &commitId)) {
            result = true;
        }
        return result;
    }
}

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    /* Run your code on data */
    OHOS::FuzzSystemGetParameterCommitId(data, size);
    return 0;
}
