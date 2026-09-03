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

#include "AndroidXRTypes.h"
#include "AndroidXRBlueprintFunctionLibrary.generated.h"

class UAndroidXREventProxy;

UCLASS(ClassGroup=AndroidXR)
class ANDROIDXR_API UAndroidXRBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /**
     * Determines whether or not an FAndroidXRSpace is valid.
     * @param[in] Space Space to be verified.
     * @return True if the space is valid.
     */
    UFUNCTION(BlueprintPure, Category="AndroidXR|Function Library")
    static bool IsValidSpace(const FAndroidXRSpace& Space);

    /**
     * Locates the underlying XrSpace and provides its current transform
     * (base space is the HMD's).
     * @param[in] Space Space to be located.
     * @param[out] Current transform.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintCallable, Category="AndroidXR|Function Library")
    static bool LocateSpace(const FAndroidXRSpace& Space, FTransform& Transform);

    /**
     * Destroys the underlying XrSpace.
     * @param[in] Space Space to be destroyed.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintCallable, Category="AndroidXR|Function Library")
    static bool DestroySpace(UPARAM(ref) FAndroidXRSpace& Space);

    /**
     * Gets the AndroidXR event proxy, which allows users to bind to
     * delegates which fired in response to XrEvents.
     * @return AndroidXR event proxy static instance.
     */
    UFUNCTION(BlueprintPure, Category="AndroidXR|Function Library")
    static UAndroidXREventProxy* GetEventProxy();

    /**
     * Returns whether or not camera passthrough is supported
     * @return True if camera passthrough is supported.
     */
    UFUNCTION(BlueprintPure, Category="AndroidXR|Function Library|Passthrough")
    static bool GetCameraPassthroughSupported();

    /**
     * Gets the passthrough camera state
     * @param[out] CameraState The current passthrough camera state.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintPure, Category="AndroidXR|Function Library|Passthrough")
    static bool GetPassthroughCameraState(EAndroidXRPassthroughCameraState& CameraState);

    /**
     * Set the performance level for a settings domain.
     * @param[in] SettingsDomain Settings domain to set.
     * @param[in] SettingsLevel Level to set.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintCallable, Category="AndroidXR|Function Library|Performance Settings")
    static bool PerfSettingsSetPerformanceLevel(EAndroidXRPerfSettingsDomain SettingsDomain,
        EAndroidXRPerfSettingsLevel SettingsLevel);

    /**
     * Enumerates the available display refresh rates.
     * @param[out] DisplayRefreshRates Array to be populated with refresh rates.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintCallable, Category="AndroidXR|Function Library|Display Refresh Rate")
    static bool EnumerateDisplayRefreshRates(TArray<float>& DisplayRefreshRates);

    /**
     * Gets the current display refresh rate.
     * @param[out] DisplayRefreshRate The current display refresh rate.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintPure, Category="AndroidXR|Function Library|Display Refresh Rate")
    static bool GetDisplayRefreshRate(float& DisplayRefreshRate);

    /**
     * Requests a display refresh rate.
     * @param[in] DisplayRefreshRate Requested display refresh rate.
     * @return True if the underlying API call was successful.
     */
    UFUNCTION(BlueprintCallable, Category="AndroidXR|Function Library|Display Refresh Rate")
    static bool RequestDisplayRefreshRate(float DisplayRefreshRate);
};
