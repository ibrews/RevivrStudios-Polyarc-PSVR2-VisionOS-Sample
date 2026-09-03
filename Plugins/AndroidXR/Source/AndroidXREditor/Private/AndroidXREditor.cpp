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

#include "AndroidXREditor.h"
#include "ISettingsCategory.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "AndroidXRSettings.h"
#include "IAndroidXRModule.h"
#include "AndroidXRPluginManifestSettings.h"

#define LOCTEXT_NAMESPACE "FAndroidXREditor"

FAndroidXREditor::FAndroidXREditor()
{
}

void FAndroidXREditor::StartupModule()
{
    IAndroidXREditorModule::StartupModule();
    ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
    if(SettingsModule)
    {
        SettingsModule->RegisterSettings("Project", "Plugins", "AndroidXR",
                                         LOCTEXT("AndroidXRSettingsName", "AndroidXR"),
                                         LOCTEXT("AndroidXRSettingsDescription", "Project settings for AndroidXR plugins"),
                                         GetMutableDefault<UAndroidXRSettings>());
        SettingsModule->RegisterSettings("Project", "Plugins","AndroidXRManifest",
                                         LOCTEXT("AndroidXRManifestSettingsName", "AndroidXRManifestSettings"),
                                         LOCTEXT("AndroidXRManifestSettingsDescription", "Android Manifest settings for AndroidXR"),
                                         GetMutableDefault<UAndroidXRRuntimeManifestSettings>());
    }
}

void FAndroidXREditor::ShutdownModule()
{
    IAndroidXREditorModule::ShutdownModule();
    ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
    if(SettingsModule)
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "AndroidXR");
    }
}

IMPLEMENT_MODULE(FAndroidXREditor, AndroidXREditor);
