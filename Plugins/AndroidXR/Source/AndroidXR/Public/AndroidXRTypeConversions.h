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
#include "HeadMountedDisplayTypes.h"

// Functions to convert OpenXR native types to Unreal AndroidXR types
// and vice-versa
namespace AndroidXR
{
    inline EAndroidXRTrackableType Convert(XrTrackableTypeANDROID NativeType)
    {
        switch (NativeType)
        {
            case XR_TRACKABLE_TYPE_PLANE_ANDROID:
                return EAndroidXRTrackableType::Plane;
            case XR_TRACKABLE_TYPE_DEPTH_ANDROID:
                return EAndroidXRTrackableType::Depth;
            default:
                break;
        }
        return EAndroidXRTrackableType::NotValid;
    }
    inline XrTrackableTypeANDROID Convert(EAndroidXRTrackableType UnrealType)
    {
        switch (UnrealType)
        {
            case EAndroidXRTrackableType::Plane:
                return XR_TRACKABLE_TYPE_PLANE_ANDROID;
            case EAndroidXRTrackableType::Depth:
                return XR_TRACKABLE_TYPE_DEPTH_ANDROID;
            default:
                break;
        }
        return XR_TRACKABLE_TYPE_NOT_VALID_ANDROID;
    }

    inline EAndroidXRTrackingState Convert(XrTrackingStateANDROID NativeType)
    {
        switch (NativeType)
        {
            case XR_TRACKING_STATE_PAUSED_ANDROID:
                return EAndroidXRTrackingState::Paused;
            case XR_TRACKING_STATE_STOPPED_ANDROID:
                return EAndroidXRTrackingState::Stopped;
            case XR_TRACKING_STATE_TRACKING_ANDROID:
                return EAndroidXRTrackingState::Tracking;
            default:
                break;
        }
        return EAndroidXRTrackingState::Stopped;
    }
    inline XrTrackingStateANDROID Convert(EAndroidXRTrackingState UnrealType)
    {
        switch (UnrealType)
        {
            case EAndroidXRTrackingState::Paused:
                return XR_TRACKING_STATE_PAUSED_ANDROID;
            case EAndroidXRTrackingState::Stopped:
                return XR_TRACKING_STATE_STOPPED_ANDROID;
            case EAndroidXRTrackingState::Tracking:
                return XR_TRACKING_STATE_TRACKING_ANDROID;
            default:
                break;
        }
        return XR_TRACKING_STATE_MAX_ENUM_ANDROID;
    }

    inline EAndroidXRPlaneType Convert(XrPlaneTypeANDROID NativeType)
    {
        switch (NativeType)
        {
            case XR_PLANE_TYPE_HORIZONTAL_DOWNWARD_FACING_ANDROID:
                return EAndroidXRPlaneType::HorizontalDownwardFacing;
            case XR_PLANE_TYPE_HORIZONTAL_UPWARD_FACING_ANDROID:
                return EAndroidXRPlaneType::HorizontalUpwardFacing;
            case XR_PLANE_TYPE_VERTICAL_ANDROID:
                return EAndroidXRPlaneType::Vertical;
            case XR_PLANE_TYPE_ARBITRARY_ANDROID:
                return EAndroidXRPlaneType::Arbitrary;
            default:
                break;
        }
        return EAndroidXRPlaneType::Arbitrary;
    }
    inline XrPlaneTypeANDROID Convert(EAndroidXRPlaneType UnrealType)
    {
        switch (UnrealType)
        {
            case EAndroidXRPlaneType::HorizontalDownwardFacing:
                return XR_PLANE_TYPE_HORIZONTAL_DOWNWARD_FACING_ANDROID;
            case EAndroidXRPlaneType::HorizontalUpwardFacing:
                return XR_PLANE_TYPE_HORIZONTAL_UPWARD_FACING_ANDROID;
            case EAndroidXRPlaneType::Vertical:
                return XR_PLANE_TYPE_VERTICAL_ANDROID;
            case EAndroidXRPlaneType::Arbitrary:
                return XR_PLANE_TYPE_ARBITRARY_ANDROID;
            default:
                break;
        }
        return XR_PLANE_TYPE_MAX_ENUM_ANDROID;
    }

    inline EAndroidXRPlaneLabel Convert(XrPlaneLabelANDROID NativeType)
    {
        switch (NativeType)
        {
            case XR_PLANE_LABEL_UNKNOWN_ANDROID:
                return EAndroidXRPlaneLabel::Unknown;
            case XR_PLANE_LABEL_WALL_ANDROID:
                return EAndroidXRPlaneLabel::Wall;
            case XR_PLANE_LABEL_FLOOR_ANDROID:
                return EAndroidXRPlaneLabel::Floor;
            case XR_PLANE_LABEL_CEILING_ANDROID:
                return EAndroidXRPlaneLabel::Ceiling;
            case XR_PLANE_LABEL_TABLE_ANDROID:
                return EAndroidXRPlaneLabel::Table;
            default:
                break;
        }
        return EAndroidXRPlaneLabel::Unknown;
    }
    inline XrPlaneLabelANDROID Convert(EAndroidXRPlaneLabel UnrealType)
    {
        switch (UnrealType)
        {
            case EAndroidXRPlaneLabel::Unknown:
                return XR_PLANE_LABEL_UNKNOWN_ANDROID;
            case EAndroidXRPlaneLabel::Wall:
                return XR_PLANE_LABEL_WALL_ANDROID;
            case EAndroidXRPlaneLabel::Floor:
                return XR_PLANE_LABEL_FLOOR_ANDROID;
            case EAndroidXRPlaneLabel::Ceiling:
                return XR_PLANE_LABEL_CEILING_ANDROID;
            case EAndroidXRPlaneLabel::Table:
                return XR_PLANE_LABEL_TABLE_ANDROID;
            default:
                break;
        }
        return XR_PLANE_LABEL_MAX_ENUM_ANDROID;
    }

    inline EAndroidXRPassthroughCameraState Convert(XrPassthroughCameraStateANDROID NativeType)
    {
        switch (NativeType)
        {
            case XR_PASSTHROUGH_CAMERA_STATE_DISABLED_ANDROID:
                return EAndroidXRPassthroughCameraState::Disabled;
            case XR_PASSTHROUGH_CAMERA_STATE_INITIALIZING_ANDROID:
                return EAndroidXRPassthroughCameraState::Initializing;
            case XR_PASSTHROUGH_CAMERA_STATE_READY_ANDROID:
                return EAndroidXRPassthroughCameraState::Ready;
            case XR_PASSTHROUGH_CAMERA_STATE_ERROR_ANDROID:
                return EAndroidXRPassthroughCameraState::Error;
            default:
                break;
        }
        return EAndroidXRPassthroughCameraState::Error;
    }
    inline XrPassthroughCameraStateANDROID Convert(EAndroidXRPassthroughCameraState UnrealType)
    {
        switch (UnrealType)
        {
            case EAndroidXRPassthroughCameraState::Disabled:
                return XR_PASSTHROUGH_CAMERA_STATE_DISABLED_ANDROID;
            case EAndroidXRPassthroughCameraState::Initializing:
                return XR_PASSTHROUGH_CAMERA_STATE_INITIALIZING_ANDROID;
            case EAndroidXRPassthroughCameraState::Ready:
                return XR_PASSTHROUGH_CAMERA_STATE_READY_ANDROID;
            case EAndroidXRPassthroughCameraState::Error:
                return XR_PASSTHROUGH_CAMERA_STATE_ERROR_ANDROID;
            default:
                break;
        }
        return XR_PASSTHROUGH_CAMERA_STATE_ERROR_ANDROID;
    }

    inline EAndroidXRPerformanceMetricsCounterUnit Convert(XrPerformanceMetricsCounterUnitANDROID NativeType)
    {
        switch (NativeType)
        {
            case XR_PERFORMANCE_METRICS_COUNTER_UNIT_GENERIC_ANDROID:
                return EAndroidXRPerformanceMetricsCounterUnit::Generic;
            case XR_PERFORMANCE_METRICS_COUNTER_UNIT_PERCENTAGE_ANDROID:
                return EAndroidXRPerformanceMetricsCounterUnit::Percentage;
            case XR_PERFORMANCE_METRICS_COUNTER_UNIT_MILLISECONDS_ANDROID:
                return EAndroidXRPerformanceMetricsCounterUnit::Millisecond;
            case XR_PERFORMANCE_METRICS_COUNTER_UNIT_BYTES_ANDROID:
                return EAndroidXRPerformanceMetricsCounterUnit::Bytes;
            case XR_PERFORMANCE_METRICS_COUNTER_UNIT_HERTZ_ANDROID:
                return EAndroidXRPerformanceMetricsCounterUnit::Hertz;
            default:
                break;
        }
        return EAndroidXRPerformanceMetricsCounterUnit::Generic;
    }
    inline XrPerformanceMetricsCounterUnitANDROID Convert(EAndroidXRPerformanceMetricsCounterUnit UnrealType)
    {
        switch (UnrealType)
        {
            case EAndroidXRPerformanceMetricsCounterUnit::Generic:
                return XR_PERFORMANCE_METRICS_COUNTER_UNIT_GENERIC_ANDROID;
            case EAndroidXRPerformanceMetricsCounterUnit::Percentage:
                return XR_PERFORMANCE_METRICS_COUNTER_UNIT_PERCENTAGE_ANDROID;
            case EAndroidXRPerformanceMetricsCounterUnit::Millisecond:
                return XR_PERFORMANCE_METRICS_COUNTER_UNIT_MILLISECONDS_ANDROID;
            case EAndroidXRPerformanceMetricsCounterUnit::Bytes:
                return XR_PERFORMANCE_METRICS_COUNTER_UNIT_BYTES_ANDROID;
            case EAndroidXRPerformanceMetricsCounterUnit::Hertz:
                return XR_PERFORMANCE_METRICS_COUNTER_UNIT_HERTZ_ANDROID;
            default:
                break;
        }
        return XR_PERFORMANCE_METRICS_COUNTER_UNIT_MAX_ENUM_ANDROID;
    }
    inline EAndroidXRDepthCameraResolution Convert(XrDepthCameraResolutionANDROID NativeType)
    {
        switch (NativeType)
        {
        case XR_DEPTH_CAMERA_RESOLUTION_80x80_ANDROID:
            return EAndroidXRDepthCameraResolution::Resolution80;
        case XR_DEPTH_CAMERA_RESOLUTION_160x160_ANDROID:
            return EAndroidXRDepthCameraResolution::Resolution160;
        case XR_DEPTH_CAMERA_RESOLUTION_320x320_ANDROID:
            return EAndroidXRDepthCameraResolution::Resolution320;
        default:
            break;
        }
        return EAndroidXRDepthCameraResolution::Resolution80;
    }
    inline XrDepthCameraResolutionANDROID Convert(EAndroidXRDepthCameraResolution UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRDepthCameraResolution::Resolution80:
            return XR_DEPTH_CAMERA_RESOLUTION_80x80_ANDROID;
        case EAndroidXRDepthCameraResolution::Resolution160:
            return XR_DEPTH_CAMERA_RESOLUTION_160x160_ANDROID;
        case EAndroidXRDepthCameraResolution::Resolution320:
            return XR_DEPTH_CAMERA_RESOLUTION_320x320_ANDROID;
        default:
            break;
        }
        return XR_DEPTH_CAMERA_RESOLUTION_80x80_ANDROID;
    }

    inline EAndroidXRPerfSettingsDomain Convert(XrPerfSettingsDomainEXT NativeType)
    {
        switch (NativeType)
        {
        case XR_PERF_SETTINGS_DOMAIN_CPU_EXT:
            return EAndroidXRPerfSettingsDomain::CPU;
        case XR_PERF_SETTINGS_DOMAIN_GPU_EXT:
            return EAndroidXRPerfSettingsDomain::GPU;
        default:
            break;
        }
        return EAndroidXRPerfSettingsDomain::CPU;
    }
    inline XrPerfSettingsDomainEXT Convert(EAndroidXRPerfSettingsDomain UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRPerfSettingsDomain::CPU:
            return XR_PERF_SETTINGS_DOMAIN_CPU_EXT;
        case EAndroidXRPerfSettingsDomain::GPU:
            return XR_PERF_SETTINGS_DOMAIN_GPU_EXT;
        default:
            break;
        }
        return XR_PERF_SETTINGS_DOMAIN_CPU_EXT;
    }

    inline EAndroidXRPerfSettingsSubDomain Convert(XrPerfSettingsSubDomainEXT NativeType)
    {
        switch (NativeType)
        {
        case XR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING_EXT:
            return EAndroidXRPerfSettingsSubDomain::Compositing;
        case XR_PERF_SETTINGS_SUB_DOMAIN_RENDERING_EXT:
            return EAndroidXRPerfSettingsSubDomain::Rendering;
        case XR_PERF_SETTINGS_SUB_DOMAIN_THERMAL_EXT:
            return EAndroidXRPerfSettingsSubDomain::Thermal;
        default:
            break;
        }
        return EAndroidXRPerfSettingsSubDomain::Compositing;
    }
    inline XrPerfSettingsSubDomainEXT Convert(EAndroidXRPerfSettingsSubDomain UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRPerfSettingsSubDomain::Compositing:
            return XR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING_EXT;
        case EAndroidXRPerfSettingsSubDomain::Rendering:
            return XR_PERF_SETTINGS_SUB_DOMAIN_RENDERING_EXT;
        case EAndroidXRPerfSettingsSubDomain::Thermal:
            return XR_PERF_SETTINGS_SUB_DOMAIN_THERMAL_EXT;
        default:
            break;
        }
        return XR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING_EXT;
    }

    inline EAndroidXRPerfSettingsLevel Convert(XrPerfSettingsLevelEXT NativeType)
    {
        switch (NativeType)
        {
        case XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT:
            return EAndroidXRPerfSettingsLevel::PowerSavings;
        case XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT:
            return EAndroidXRPerfSettingsLevel::SustainedLow;
        case XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT:
            return EAndroidXRPerfSettingsLevel::SustainedHigh;
        case XR_PERF_SETTINGS_LEVEL_BOOST_EXT:
            return EAndroidXRPerfSettingsLevel::Boost;
        default:
            break;
        }
        return EAndroidXRPerfSettingsLevel::PowerSavings;
    }
    inline XrPerfSettingsLevelEXT Convert(EAndroidXRPerfSettingsLevel UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRPerfSettingsLevel::PowerSavings:
            return XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT;
        case EAndroidXRPerfSettingsLevel::SustainedLow:
            return XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
        case EAndroidXRPerfSettingsLevel::SustainedHigh:
            return XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
        case EAndroidXRPerfSettingsLevel::Boost:
            return XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
        default:
            break;
        }
        return XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT;
    }

    inline EAndroidXRPerfSettingsNotificationLevel Convert(XrPerfSettingsNotificationLevelEXT NativeType)
    {
        switch (NativeType)
        {
        case XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT:
            return EAndroidXRPerfSettingsNotificationLevel::Normal;
        case XR_PERF_SETTINGS_NOTIF_LEVEL_WARNING_EXT:
            return EAndroidXRPerfSettingsNotificationLevel::Warning;
        case XR_PERF_SETTINGS_NOTIF_LEVEL_IMPAIRED_EXT:
            return EAndroidXRPerfSettingsNotificationLevel::Impaired;
        default:
            break;
        }
        return EAndroidXRPerfSettingsNotificationLevel::Normal;
    }
    inline XrPerfSettingsNotificationLevelEXT Convert(EAndroidXRPerfSettingsNotificationLevel UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRPerfSettingsNotificationLevel::Normal:
            return XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT;
        case EAndroidXRPerfSettingsNotificationLevel::Warning:
            return XR_PERF_SETTINGS_NOTIF_LEVEL_WARNING_EXT;
        case EAndroidXRPerfSettingsNotificationLevel::Impaired:
            return XR_PERF_SETTINGS_NOTIF_LEVEL_IMPAIRED_EXT;
        default:
            break;
        }
        return XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT;
    }

#if PLATFORM_ANDROID
    inline EAndroidXRAndroidThreadType Convert(XrAndroidThreadTypeKHR NativeType)
    {
        switch (NativeType)
        {
        case XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR:
            return EAndroidXRAndroidThreadType::ApplicationMain;
        case XR_ANDROID_THREAD_TYPE_APPLICATION_WORKER_KHR:
            return EAndroidXRAndroidThreadType::ApplicationWorker;
        case XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR:
            return EAndroidXRAndroidThreadType::RendererMain;
        case XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR:
            return EAndroidXRAndroidThreadType::RendererWorker;
        default:
            break;
        }
        return EAndroidXRAndroidThreadType::ApplicationMain;
    }
    inline XrAndroidThreadTypeKHR Convert(EAndroidXRAndroidThreadType UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRAndroidThreadType::ApplicationMain:
            return XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR;
        case EAndroidXRAndroidThreadType::ApplicationWorker:
            return XR_ANDROID_THREAD_TYPE_APPLICATION_WORKER_KHR;
        case EAndroidXRAndroidThreadType::RendererMain:
            return XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR;
        case EAndroidXRAndroidThreadType::RendererWorker:
            return XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR;
        default:
            break;
        }
        return XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR;
    }
#endif // PLATFORM_ANDROID

    inline XrHandJointEXT Convert(EHandKeypoint UnrealType)
    {
        switch (UnrealType)
        {
            case EHandKeypoint::Palm:
                return XR_HAND_JOINT_PALM_EXT;
            case EHandKeypoint::Wrist:
                return XR_HAND_JOINT_WRIST_EXT;
            case EHandKeypoint::ThumbMetacarpal:
                return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
            case EHandKeypoint::ThumbProximal:
                return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
            case EHandKeypoint::ThumbDistal:
                return XR_HAND_JOINT_THUMB_DISTAL_EXT;
            case EHandKeypoint::ThumbTip:
                return XR_HAND_JOINT_THUMB_TIP_EXT;
            case EHandKeypoint::IndexMetacarpal:
                return XR_HAND_JOINT_INDEX_METACARPAL_EXT;
            case EHandKeypoint::IndexProximal:
                return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
            case EHandKeypoint::IndexIntermediate:
                return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
            case EHandKeypoint::IndexDistal:
                return XR_HAND_JOINT_INDEX_DISTAL_EXT;
            case EHandKeypoint::IndexTip:
                return XR_HAND_JOINT_INDEX_TIP_EXT;
            case EHandKeypoint::MiddleMetacarpal:
                return XR_HAND_JOINT_MIDDLE_METACARPAL_EXT;
            case EHandKeypoint::MiddleProximal:
                return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
            case EHandKeypoint::MiddleIntermediate:
                return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
            case EHandKeypoint::MiddleDistal:
                return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
            case EHandKeypoint::MiddleTip:
                return XR_HAND_JOINT_MIDDLE_TIP_EXT;
            case EHandKeypoint::RingMetacarpal:
                return XR_HAND_JOINT_RING_METACARPAL_EXT;
            case EHandKeypoint::RingProximal:
                return XR_HAND_JOINT_RING_PROXIMAL_EXT;
            case EHandKeypoint::RingIntermediate:
                return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
            case EHandKeypoint::RingDistal:
                return XR_HAND_JOINT_RING_DISTAL_EXT;
            case EHandKeypoint::RingTip:
                return XR_HAND_JOINT_RING_TIP_EXT;
            case EHandKeypoint::LittleMetacarpal:
                return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
            case EHandKeypoint::LittleProximal:
                return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
            case EHandKeypoint::LittleIntermediate:
                return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
            case EHandKeypoint::LittleDistal:
                return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
            case EHandKeypoint::LittleTip:
                return XR_HAND_JOINT_LITTLE_TIP_EXT;
            default:
                break;
        }
        return XR_HAND_JOINT_PALM_EXT;
    }

    inline EHandKeypoint Convert(XrHandJointEXT NativeType)
    {
        switch (NativeType)
        {
            case XR_HAND_JOINT_PALM_EXT:
                return EHandKeypoint::Palm;
            case XR_HAND_JOINT_WRIST_EXT:
                return EHandKeypoint::Wrist;
            case XR_HAND_JOINT_THUMB_METACARPAL_EXT:
                return EHandKeypoint::ThumbMetacarpal;
            case XR_HAND_JOINT_THUMB_PROXIMAL_EXT:
                return EHandKeypoint::ThumbProximal;
            case XR_HAND_JOINT_THUMB_DISTAL_EXT:
                return EHandKeypoint::ThumbDistal;
            case XR_HAND_JOINT_THUMB_TIP_EXT:
                return EHandKeypoint::ThumbTip;
            case XR_HAND_JOINT_INDEX_METACARPAL_EXT:
                return EHandKeypoint::IndexMetacarpal;
            case XR_HAND_JOINT_INDEX_PROXIMAL_EXT:
                return EHandKeypoint::IndexProximal;
            case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT:
                return EHandKeypoint::IndexIntermediate;
            case XR_HAND_JOINT_INDEX_DISTAL_EXT:
                return EHandKeypoint::IndexDistal;
            case XR_HAND_JOINT_INDEX_TIP_EXT:
                return EHandKeypoint::IndexTip;
            case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT:
                return EHandKeypoint::MiddleMetacarpal;
            case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT:
                return EHandKeypoint::MiddleProximal;
            case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT:
                return EHandKeypoint::MiddleIntermediate;
            case XR_HAND_JOINT_MIDDLE_DISTAL_EXT:
                return EHandKeypoint::MiddleDistal;
            case XR_HAND_JOINT_MIDDLE_TIP_EXT:
                return EHandKeypoint::MiddleTip;
            case XR_HAND_JOINT_RING_METACARPAL_EXT:
                return EHandKeypoint::RingMetacarpal;
            case XR_HAND_JOINT_RING_PROXIMAL_EXT:
                return EHandKeypoint::RingProximal;
            case XR_HAND_JOINT_RING_INTERMEDIATE_EXT:
                return EHandKeypoint::RingIntermediate;
            case XR_HAND_JOINT_RING_DISTAL_EXT:
                return EHandKeypoint::RingDistal;
            case XR_HAND_JOINT_RING_TIP_EXT:
                return EHandKeypoint::RingTip;
            case XR_HAND_JOINT_LITTLE_METACARPAL_EXT:
                return EHandKeypoint::LittleMetacarpal;
            case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT:
                return EHandKeypoint::LittleProximal;
            case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT:
                return EHandKeypoint::LittleIntermediate;
            case XR_HAND_JOINT_LITTLE_DISTAL_EXT:
                return EHandKeypoint::LittleDistal;
            case XR_HAND_JOINT_LITTLE_TIP_EXT:
                return EHandKeypoint::LittleTip;
            default:
                break;
        }
        return EHandKeypoint::Palm;
    }

#pragma region AndroidXRFaceTracking
    inline EAndroidXRFaceTrackingState Convert(XrFaceTrackingStateANDROID NativeType)
    {
        switch(NativeType)
        {
            case XR_FACE_TRACKING_STATE_PAUSED_ANDROID:
                return EAndroidXRFaceTrackingState::Paused;
            case XR_FACE_TRACKING_STATE_STOPPED_ANDROID:
                return EAndroidXRFaceTrackingState::Stopped;
            case XR_FACE_TRACKING_STATE_TRACKING_ANDROID:
                return EAndroidXRFaceTrackingState::Tracked;
        }
        return EAndroidXRFaceTrackingState::Stopped;
    }
#pragma endregion

    inline EAndroidXRSceneMeshSemanticLabelSet Convert(XrSceneMeshSemanticLabelSetANDROID NativeType)
    {
        switch (NativeType)
        {
        case XR_SCENE_MESH_SEMANTIC_LABEL_SET_NONE_ANDROID:
            return EAndroidXRSceneMeshSemanticLabelSet::None;
        case XR_SCENE_MESH_SEMANTIC_LABEL_SET_DEFAULT_ANDROID:
            return EAndroidXRSceneMeshSemanticLabelSet::Default;
        default:
            break;
        }
        return EAndroidXRSceneMeshSemanticLabelSet::None;
    }
    inline XrSceneMeshSemanticLabelSetANDROID Convert(EAndroidXRSceneMeshSemanticLabelSet UnrealType)
    {
        switch (UnrealType)
        {
        case EAndroidXRSceneMeshSemanticLabelSet::None:
            return XR_SCENE_MESH_SEMANTIC_LABEL_SET_NONE_ANDROID;
        case EAndroidXRSceneMeshSemanticLabelSet::Default:
            return XR_SCENE_MESH_SEMANTIC_LABEL_SET_DEFAULT_ANDROID;
        default:
            break;
        }
        return XR_SCENE_MESH_SEMANTIC_LABEL_SET_NONE_ANDROID;
    }

    inline ESceneMeshSemanticLabel Convert(XrSceneMeshSemanticLabelANDROID NativeType)
    {
        switch (NativeType)
        {
        case XR_SCENE_MESH_SEMANTIC_LABEL_OTHER_ANDROID:
            return ESceneMeshSemanticLabel::Other;
        case XR_SCENE_MESH_SEMANTIC_LABEL_FLOOR_ANDROID:
            return ESceneMeshSemanticLabel::Floor;
        case XR_SCENE_MESH_SEMANTIC_LABEL_CEILING_ANDROID:
            return ESceneMeshSemanticLabel::Ceiling;
        case XR_SCENE_MESH_SEMANTIC_LABEL_WALL_ANDROID:
            return ESceneMeshSemanticLabel::Wall;
        case XR_SCENE_MESH_SEMANTIC_LABEL_TABLE_ANDROID:
            return ESceneMeshSemanticLabel::Table;
        default:
            break;
        }
        return ESceneMeshSemanticLabel::Other;
    }
    inline XrSceneMeshSemanticLabelANDROID Convert(ESceneMeshSemanticLabel UnrealType)
    {
        switch (UnrealType)
        {
        case ESceneMeshSemanticLabel::Other:
            return XR_SCENE_MESH_SEMANTIC_LABEL_OTHER_ANDROID;
        case ESceneMeshSemanticLabel::Floor:
            return XR_SCENE_MESH_SEMANTIC_LABEL_FLOOR_ANDROID;
        case ESceneMeshSemanticLabel::Ceiling:
            return XR_SCENE_MESH_SEMANTIC_LABEL_CEILING_ANDROID;
        case ESceneMeshSemanticLabel::Wall:
            return XR_SCENE_MESH_SEMANTIC_LABEL_WALL_ANDROID;
        case ESceneMeshSemanticLabel::Table:
            return XR_SCENE_MESH_SEMANTIC_LABEL_TABLE_ANDROID;
        default:
            break;
        }
        return XR_SCENE_MESH_SEMANTIC_LABEL_OTHER_ANDROID;
    }

    inline ESceneMeshTrackingState Convert(XrSceneMeshTrackingStateANDROID NativeType)
    {
        switch (NativeType)
        {
        case XR_SCENE_MESH_TRACKING_STATE_INITIALIZING_ANDROID:
            return ESceneMeshTrackingState::Initializing;
        case XR_SCENE_MESH_TRACKING_STATE_TRACKING_ANDROID:
            return ESceneMeshTrackingState::Tracking;
        case XR_SCENE_MESH_TRACKING_STATE_WAITING_ANDROID:
            return ESceneMeshTrackingState::Waiting;
        case XR_SCENE_MESH_TRACKING_STATE_ERROR_ANDROID:
            return ESceneMeshTrackingState::Error;
        default:
            break;
        }
        return ESceneMeshTrackingState::Error;
    }
    inline XrSceneMeshTrackingStateANDROID Convert(ESceneMeshTrackingState UnrealType)
    {
        switch (UnrealType)
        {
        case ESceneMeshTrackingState::Initializing:
            return XR_SCENE_MESH_TRACKING_STATE_INITIALIZING_ANDROID;
        case ESceneMeshTrackingState::Tracking:
            return XR_SCENE_MESH_TRACKING_STATE_TRACKING_ANDROID;
        case ESceneMeshTrackingState::Waiting:
            return XR_SCENE_MESH_TRACKING_STATE_WAITING_ANDROID;
        case ESceneMeshTrackingState::Error:
            return XR_SCENE_MESH_TRACKING_STATE_ERROR_ANDROID;
        default:
            break;
        }
        return XR_SCENE_MESH_TRACKING_STATE_ERROR_ANDROID;
    }
}
