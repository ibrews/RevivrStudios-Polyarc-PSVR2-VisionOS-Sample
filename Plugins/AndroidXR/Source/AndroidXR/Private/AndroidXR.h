/* Copyright 2026 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "IAndroidXRModule.h"
#include "IOpenXRExtensionPlugin.h"

#include <AndroidXRHelpers.h>

DECLARE_LOG_CATEGORY_EXTERN(LogAndroidXR, Log, All);

class FAndroidXR : public IAndroidXRModule, public IOpenXRExtensionPlugin
{
public:
    FAndroidXR();

    /** IModuleInterface */
    void StartupModule() override;
    void ShutdownModule() override;

    /** IOpenXRExtensionPlugin */
    FString GetDisplayName() override
    {
        return FString(TEXT("AndroidXR"));
    }

    bool GetCustomLoader(PFN_xrGetInstanceProcAddr* OutGetProcAddr) override;
    void PostCreateInstance(XrInstance InInstance) override;
    void PostCreateSession(XrSession InSession) override;
    bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
    bool GetCameraPassthroughSupported() override;
    bool GetPassthroughCameraState(EAndroidXRPassthroughCameraState& CameraState) override;
    void OnEvent(XrSession InSession, const XrEventDataBaseHeader* InHeader) override;

    bool PerfSettingsSetPerformanceLevel(EAndroidXRPerfSettingsDomain SettingsDomain,
        EAndroidXRPerfSettingsLevel SettingsLevel) override;

    bool EnumerateDisplayRefreshRates(TArray<float>& DisplayRefreshRates) override;
    bool GetDisplayRefreshRate(float& DisplayRefreshRate) override;
    bool RequestDisplayRefreshRate(float DisplayRefreshRate) override;

    bool SetAndroidApplicationThread(EAndroidXRAndroidThreadType ThreadType, uint32 ThreadID) override;

    bool SetDebugUtilsObjectName(XrObjectType ObjectType,
        uint64 ObjectHandle,
        FString ObjectName) override;
    bool CreateDebugUtilsMessenger(const XrDebugUtilsMessengerCreateInfoEXT& CreateInfo,
        XrDebugUtilsMessengerEXT& Messenger) override;
    bool DestroyDebugUtilsMessenger(XrDebugUtilsMessengerEXT& Messenger) override;
    bool SubmitDebugUtilsMessage(XrDebugUtilsMessageSeverityFlagsEXT SeverityFlags,
        XrDebugUtilsMessageTypeFlagsEXT TypeFlags,
        const XrDebugUtilsMessengerCallbackDataEXT& CallbackData) override;
    bool BeginDebugUtilsLabelRegion(const XrDebugUtilsLabelEXT& LabelInfo) override;
    bool EndDebugUtilsLabelRegion() override;
    bool InsertDebugUtilsLabel(const XrDebugUtilsLabelEXT& LabelInfo) override;
private:
    XrInstance Instance{};
    XrSession Session{};
    void* LoaderHandle{};

    #define PASSTHROUGHCAMERA_FUNCTIONS(HelperMacro) \
        HelperMacro(xrGetPassthroughCameraStateANDROID)

    PASSTHROUGHCAMERA_FUNCTIONS(DECLARE_OPENXR_FUNC);

    #define PERFORMANCESETTINGS_FUNCTIONS(HelperMacro) \
        HelperMacro(xrPerfSettingsSetPerformanceLevelEXT)

    PERFORMANCESETTINGS_FUNCTIONS(DECLARE_OPENXR_FUNC);

    #define DISPLAYREFRESHRATE_FUNCTIONS(HelperMacro) \
        HelperMacro(xrEnumerateDisplayRefreshRatesFB) \
        HelperMacro(xrGetDisplayRefreshRateFB) \
        HelperMacro(xrRequestDisplayRefreshRateFB)

    DISPLAYREFRESHRATE_FUNCTIONS(DECLARE_OPENXR_FUNC);

#if PLATFORM_ANDROID
    #define APPLICATIONTHREAD_FUNCTIONS(HelperMacro) \
        HelperMacro(xrSetAndroidApplicationThreadKHR)

    APPLICATIONTHREAD_FUNCTIONS(DECLARE_OPENXR_FUNC);
#endif // PLATFORM_ANDROID

    #define DEBUGUTILS_FUNCTIONS(HelperMacro) \
        HelperMacro(xrSetDebugUtilsObjectNameEXT) \
        HelperMacro(xrCreateDebugUtilsMessengerEXT) \
        HelperMacro(xrDestroyDebugUtilsMessengerEXT) \
        HelperMacro(xrSubmitDebugUtilsMessageEXT) \
        HelperMacro(xrSessionBeginDebugUtilsLabelRegionEXT) \
        HelperMacro(xrSessionEndDebugUtilsLabelRegionEXT) \
        HelperMacro(xrSessionInsertDebugUtilsLabelEXT)

    DEBUGUTILS_FUNCTIONS(DECLARE_OPENXR_FUNC);
};
