/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.power-service.moto-common-libperfmgr"

#include <thread>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <sys/stat.h>

#include "Power.h"
#include "PowerExt.h"

using aidl::google::hardware::power::impl::pixel::Power;
using aidl::google::hardware::power::impl::pixel::PowerExt;
using ::android::perfmgr::HintManager;

constexpr char kPowerHalConfigPathStem[] = "/vendor/etc/powerhint_";
constexpr char kPowerHalConfigPathSuffix[] = ".json";
constexpr char kPowerHalConfigPathDefault[] = "/vendor/etc/powerhint.json";
constexpr char kPowerHalHardwareProp[] = "ro.vendor.qti.soc_name";
constexpr char kPowerHalInitProp[] = "vendor.powerhal.init";

int main() {
    struct stat st;
    std::string path = kPowerHalConfigPathDefault;
    LOG(INFO) << "Power HAL AIDL Service with Extension is starting.";

    std::string hardware = ::android::base::GetProperty(kPowerHalHardwareProp, "");

    if (hardware != "") {
        std::string newPath = kPowerHalConfigPathStem + hardware + kPowerHalConfigPathSuffix;
        if (stat(newPath.c_str(), &st) == 0) {
            path = newPath;
        }
    }

    // Parse config but do not start the looper
    std::shared_ptr<HintManager> hm = HintManager::GetFromJSON(path, false);
    if (!hm) {
        LOG(FATAL) << "Invalid config: " << path;
    }

    // single thread
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    // core service
    std::shared_ptr<Power> pw = ndk::SharedRefBase::make<Power>(hm);
    ndk::SpAIBinder pwBinder = pw->asBinder();

    // extension service
    std::shared_ptr<PowerExt> pwExt = ndk::SharedRefBase::make<PowerExt>(hm);

    // attach the extension to the same binder we will be registering
    CHECK(STATUS_OK == AIBinder_setExtension(pwBinder.get(), pwExt->asBinder().get()));

    const std::string instance = std::string() + Power::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(pw->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);
    LOG(INFO) << "Power HAL AIDL Service started.";

    std::thread initThread([&]() {
        ::android::base::WaitForProperty(kPowerHalInitProp, "1");
        hm->Start();
    });
    initThread.detach();

    ABinderProcess_joinThreadPool();

    // should not reach
    LOG(ERROR) << "Power HAL AIDL Service with Extension just died.";
    return EXIT_FAILURE;
}
