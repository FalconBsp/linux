/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

/**
 * @file   drDrmApi.h
 * @brief  Contains DCI command definitions and data structures
 *
 */

#ifndef __DRDRMAPI_H__
#define __DRDRMAPI_H__

#include "dci.h"
//#include "stdint.h"

// Driver UUID. Update accordingly after reserving UUID
#define DRV_DRM_UUID {{0xa3,0x38,0xce,0xa0,0xd4,0x78,0x11,0xe3,0x9c,0x1a,0x08,0x00,0x20,0x0c,0x9a,0x66}}

#define MAX_NUM_DISPLAYS            32  //Even with support for DP 1.2 this should be enough
#define MAX_NUM_CNTL                6   // 3 bits - 0 to 7 indexes are possible,
                                        // but sources are limited by 0 to 5
#define MAX_NUM_ENGINE              11  // 10 wired and 1 wireless
#define MAX_NUM_LINK                7

#define DRM_PSTORAGE_SIZE           (1024*64)
#define DRM_HDCP_KSV_LENGTH         5
#define DRM_MAX_NUM_RX_DEVICES      127
#define WRAPPED_AES_KEY_SIZE        64
#define HFSPSPSPSS_KEY_SIZE         16
#define DRM_DEVICE_INFO_SIZE        16
#define HFSPS_OMAC_SIZE             16

// NOTE: Add engine ID for newer ASICs, but keep backwards compatibility in order to
//       prevent changes in previous init functions for older ASICs
typedef enum
{
    DISPLAY_ENGINE_ID_DIGA = 0,
    DISPLAY_ENGINE_ID_DIGB,
    DISPLAY_ENGINE_ID_DIGC,
    DISPLAY_ENGINE_ID_DIGD,
    DISPLAY_ENGINE_ID_DIGE,
    DISPLAY_ENGINE_ID_DIGF,
    DISPLAY_ENGINE_ID_DIGG,
    DISPLAY_ENGINE_ID_DACA,
    DISPLAY_ENGINE_ID_DACB,
    DISPLAY_ENGINE_ID_DVO,
    DISPLAY_ENGINE_ID_VCE,
    DISPLAY_ENGINE_ID_COUNT // Current count is 11 = 10 wired and 1 wireless
} DISPLAY_ENGINE_ID;

typedef struct
{
    unsigned int    DispIndex;
    unsigned int    CtrlIndex;
    unsigned int    EngineID;
    unsigned int    LinkID;
    unsigned int    DispActive;
    unsigned int    IsWired;
    unsigned int    ConnectorType;
} DISPLAY_PATH;

// TODO: Have DCI handler update display path info whenever there is mode set
typedef struct
{
    unsigned int    DispPathCnt; // Number of entries in display path array
    DISPLAY_PATH    DispPath[DISPLAY_ENGINE_ID_COUNT];
} DISPLAY_PATH_INFO;

// Command ID's
#define DRM_MODE_SET                        1
#define DRM_CHECK_REVOCATION                2
#define DRM_SET_VIDEO_PROTECTED_REGION      3
#define DRM_GET_DEVICE_INFO                 4

// Mode set command structure
typedef struct
{
    dciCommandHeader_t      header;         // Command header
    DISPLAY_PATH_INFO       dispPathInfo;   // Display configuration
} DRM_CMD_MODE_SET;

// Video protected region set command structure
typedef struct
{
    dciCommandHeader_t      header;       // Command header
    uint32_t                ulRegionType;
    uint64_t                ulCurrentStartPhysicalAddr;
    uint64_t                ulCurrentSFBSize;
    uint64_t                ulNewStartPhysicalAddr;
    uint64_t                ulNewSFBSize;
} DRM_CMD_SET_VPR;

// Check repeater KSV list for revocation
typedef struct
{
    dciCommandHeader_t  header;                                             // Command header
    unsigned int        DisplayIndex;                                       // Value indicating the display index
    unsigned char       BKSV[DRM_HDCP_KSV_LENGTH];                          // Key Selection Vector
    unsigned char       KSVList[DRM_HDCP_KSV_LENGTH*DRM_MAX_NUM_RX_DEVICES];// Repeater list of KSVs
    unsigned int        KSVListLength;                                      // Length of repeater list of KSVs
} DRM_CMD_CHECK_REVOCATION;

typedef struct
{
    dciCommandHeader_t  header;                                             // Command header
} DRM_CMD_GET_DEVICE_INFO;

// FW validation result structure
typedef struct
{
    unsigned int    IsACPFWVFailed;
    unsigned int    InterruptErrorType;
} DRM_FW_VALIDATION_RESULT;

// Device Info result structure
typedef struct
{
    unsigned long    ulDeviceID;
    unsigned long    ulGPIOPAD_Y;
} DRM_DEVICE_INFO_STRUCT;

typedef struct _DRM_HFSPS_PSP_DATA
{
    DRM_DEVICE_INFO_STRUCT   stDeviceInfo;
    unsigned char            MAC[HFSPSPSPSS_KEY_SIZE];
} DRM_HFSPS_PSP_DATA;

// Note: DCI buffer consists of two parts. First 4 KB is used for DCI interface,
//       remaining part contains Persistent Storage data.
//
// DRM DCI message
typedef struct 
{
    // first 2 KB is used for communication from CPLIB to ASD
    union {
        dciCommandHeader_t          commandHeader;
        dciResponseHeader_t         responseHeader;
        DRM_CMD_MODE_SET            cmdModeSet;
        DRM_CMD_CHECK_REVOCATION    cmdCheckRevocation;
        DRM_CMD_SET_VPR             cmdSetVideoProtectedRegion;
        DRM_CMD_GET_DEVICE_INFO     cmdGetDeviceInfo;

        unsigned char               Buffer0[1024*2];     // 2 KB padding
    };
    // first 2 KB is used for communication from ASD to CPLIB
    union {
        DRM_FW_VALIDATION_RESULT    FWVResult;
        DRM_HFSPS_PSP_DATA          stEncryptedHFSPSData; 

        unsigned char               Buffer1[1024*2];     // 2 KB padding
    };
    unsigned char       PStorage[DRM_PSTORAGE_SIZE];    // Persistent Storage buffer

} dciMessage_t;

#endif // __DRDRMAPI_H__
