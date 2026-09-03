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

#include "AndroidXRHelpers.h"

bool ResolveOpenXRFunctions(XrInstance Instance,
    const TArray<TTuple<const char *, PFN_xrVoidFunction*>>& Functions,
    void (*LoggingFunction)(const char*, XrResult))
{
    auto AllFunctionsFound = true;
    for (const auto& Function : Functions)
    {
        auto Result = xrGetInstanceProcAddr(Instance,
            Function.Get<0>(), Function.Get<1>());
        if (!XR_UNQUALIFIED_SUCCESS(Result))
        {
            LoggingFunction(Function.Get<0>(), Result);
            AllFunctionsFound = false;
        }
    }
    return AllFunctionsFound;
}
