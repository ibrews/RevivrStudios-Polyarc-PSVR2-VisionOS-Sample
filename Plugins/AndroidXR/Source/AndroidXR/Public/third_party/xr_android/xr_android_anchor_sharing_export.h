#ifndef XR_ANDROID_ANCHOR_SHARING_EXPORT_H_
#define XR_ANDROID_ANCHOR_SHARING_EXPORT_H_ 1

/*
** Copyright 2017-2024, The Khronos Group Inc.
**
** SPDX-License-Identifier: Apache-2.0 OR MIT
*/

/*
** This header is generated from the Khronos OpenXR XML API Registry.
**
*/


#ifdef __cplusplus
extern "C" {
#endif


#ifndef XR_ANDROID_anchor_sharing_export
#ifdef XR_USE_PLATFORM_ANDROID

// XR_ANDROID_anchor_sharing_export is a preprocessor guard. Do not pass it to API calls.
#define XR_ANDROID_anchor_sharing_export 1
#define XR_ANDROID_anchor_sharing_export_SPEC_VERSION 1
#define XR_ANDROID_ANCHOR_SHARING_EXPORT_EXTENSION_NAME "XR_ANDROID_anchor_sharing_export"
#define XR_TYPE_ANCHOR_SHARING_INFO_ANDROID ((XrStructureType) 1000701000U)
#define XR_TYPE_ANCHOR_SHARING_TOKEN_ANDROID ((XrStructureType) 1000701001U)
#define XR_TYPE_SYSTEM_ANCHOR_SHARING_EXPORT_PROPERTIES_ANDROID ((XrStructureType) 1000701002U)
// Operation not allowed because anchor is not owned by the XrSession in which the function is being called.
#define XR_ERROR_ANCHOR_NOT_OWNED_BY_CALLER_ANDROID ((XrResult) -1000701000U)
typedef struct XrAnchorSharingInfoANDROID {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrSpace                     anchor;
} XrAnchorSharingInfoANDROID;

typedef struct XrAnchorSharingTokenANDROID {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    struct AIBinder*      token;
} XrAnchorSharingTokenANDROID;

typedef struct XrSystemAnchorSharingExportPropertiesANDROID {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrBool32              supportsAnchorSharingExport;
} XrSystemAnchorSharingExportPropertiesANDROID;

typedef XrResult (XRAPI_PTR *PFN_xrShareAnchorANDROID)(XrSession session, const XrAnchorSharingInfoANDROID* sharingInfo, XrAnchorSharingTokenANDROID* anchorToken);
typedef XrResult (XRAPI_PTR *PFN_xrUnshareAnchorANDROID)(XrSession session, XrSpace anchor);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrShareAnchorANDROID(
    XrSession                                   session,
    const XrAnchorSharingInfoANDROID*           sharingInfo,
    XrAnchorSharingTokenANDROID*                anchorToken);

XRAPI_ATTR XrResult XRAPI_CALL xrUnshareAnchorANDROID(
    XrSession                                   session,
    XrSpace                                     anchor);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */
#endif /* XR_USE_PLATFORM_ANDROID */
#endif /* XR_ANDROID_anchor_sharing_export */

#ifdef __cplusplus
}
#endif

#endif
