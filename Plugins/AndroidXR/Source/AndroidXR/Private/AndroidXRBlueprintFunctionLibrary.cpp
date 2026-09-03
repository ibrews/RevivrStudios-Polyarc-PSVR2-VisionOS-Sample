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

#include "AndroidXRBlueprintFunctionLibrary.h"
#include "IAndroidXRModule.h"
#include "IOpenXRHMD.h"
#include "IXRTrackingSystem.h"
#include "AndroidXR.h"
#include "AndroidXREventProxy.h"

bool UAndroidXRBlueprintFunctionLibrary::IsValidSpace(const FAndroidXRSpace &Space)
{
    return Space.Space != XR_NULL_HANDLE;
}

bool UAndroidXRBlueprintFunctionLibrary::LocateSpace(const FAndroidXRSpace &Space, FTransform &Transform)
{
    if (xrLocateSpace != nullptr)
    {
        if (auto HMD = GEngine->XRSystem.Get()->GetIOpenXRHMD())
        {
            XrSpaceLocation NativeLocation
            {
                .type = XR_TYPE_SPACE_LOCATION
            };

            auto Result = xrLocateSpace(Space.Space,
                HMD->GetTrackingSpace(), HMD->GetDisplayTime(), &NativeLocation);
            if (XR_UNQUALIFIED_SUCCESS(Result))
            {
                auto Flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                            XR_SPACE_LOCATION_POSITION_VALID_BIT;
                if ((NativeLocation.locationFlags & Flags) == Flags)
                {
                    Transform = ToFTransform(NativeLocation.pose,
                        GEngine->XRSystem->GetWorldToMetersScale());
                    return true;
                }
                UE_LOG(LogAndroidXR, Error,
                    TEXT("LocateSpace: xrLocateSpace returned invalid position/orientation"));
                return false;
            }
            UE_LOG(LogAndroidXR, Error,
                TEXT("LocateSpace: xrLocateSpace failed with error %s"),
                OpenXRResultToString(Result));
        }
    }
    return false;
}

bool UAndroidXRBlueprintFunctionLibrary::DestroySpace(FAndroidXRSpace &Space)
{
    if (xrDestroySpace != nullptr)
    {
        auto Result = xrDestroySpace(Space.Space);
        if (XR_UNQUALIFIED_SUCCESS(Result))
        {
            Space.Space = nullptr;
            return true;
        }

        UE_LOG(LogAndroidXR, Error,
            TEXT("DestroySpace: xrDestroySpace failed with error %s"),
            OpenXRResultToString(Result));
    }
    return false;
}

UAndroidXREventProxy* UAndroidXRBlueprintFunctionLibrary::GetEventProxy()
{
    return UAndroidXREventProxy::GetInstance();
}

bool UAndroidXRBlueprintFunctionLibrary::GetCameraPassthroughSupported()
{
    if (IAndroidXRModule::IsAvailable())
    {
        return IAndroidXRModule::Get().GetCameraPassthroughSupported();
    }
    return false;
}

bool UAndroidXRBlueprintFunctionLibrary::
    GetPassthroughCameraState(EAndroidXRPassthroughCameraState &CameraState)
{
    if (IAndroidXRModule::IsAvailable())
    {
        return IAndroidXRModule::Get().GetPassthroughCameraState(CameraState);
    }
    return false;
}

bool UAndroidXRBlueprintFunctionLibrary::PerfSettingsSetPerformanceLevel(EAndroidXRPerfSettingsDomain SettingsDomain, EAndroidXRPerfSettingsLevel SettingsLevel)
{
    if (IAndroidXRModule::IsAvailable())
    {
        return IAndroidXRModule::Get().PerfSettingsSetPerformanceLevel(SettingsDomain, SettingsLevel);
    }
    return false;
}

bool UAndroidXRBlueprintFunctionLibrary::EnumerateDisplayRefreshRates(TArray<float>& DisplayRefreshRates)
{
    if (IAndroidXRModule::IsAvailable())
    {
        return IAndroidXRModule::Get().EnumerateDisplayRefreshRates(DisplayRefreshRates);
    }
    return false;
}
bool UAndroidXRBlueprintFunctionLibrary::GetDisplayRefreshRate(float& DisplayRefreshRate)
{
    if (IAndroidXRModule::IsAvailable())
    {
        return IAndroidXRModule::Get().GetDisplayRefreshRate(DisplayRefreshRate);
    }
    return false;
}
bool UAndroidXRBlueprintFunctionLibrary::RequestDisplayRefreshRate(float DisplayRefreshRate)
{
    if (IAndroidXRModule::IsAvailable())
    {
        return IAndroidXRModule::Get().RequestDisplayRefreshRate(DisplayRefreshRate);
    }
    return false;
}
