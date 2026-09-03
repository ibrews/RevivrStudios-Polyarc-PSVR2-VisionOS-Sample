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
#include "AndroidXRPluginManifestSettings.h"
#include "Interfaces/IPluginManager.h"

TArray<FName> UAndroidXRRuntimeManifestSettings::GetRegisteredHardwareFeatures()
{
    auto ManifestSettings = GetDefault<UAndroidXRPluginManifestSettings>();
    if(!ManifestSettings)
    {
        return {};
    }
    return ManifestSettings->GetRegisteredFeatures();
}

TArray<FName> UAndroidXRPluginManifestSettings::GetRegisteredFeatures() const
{
    TSet<FName> RegisteredFeatures{};
    for(auto& PluginInfo : PluginFeatures)
    {
        auto Plugin = IPluginManager::Get().FindEnabledPlugin(PluginInfo.PluginName.ToString());
        if(!Plugin.IsValid())
        {
            continue;
        }
        RegisteredFeatures.Append(PluginInfo.HardwareFeatures);
    }
    return RegisteredFeatures.Array();
}