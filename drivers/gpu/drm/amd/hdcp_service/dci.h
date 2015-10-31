//-----------------------------------------------------------------------------
//
//  Confidential and Proprietary Information
//  Copyright 2013(c), Advanced Micro Devices, Inc. (unpublished)
//
//  All rights reserved. This notice is intended as a precaution against
//  inadvertent publication and does not imply publication or any waiver
//  of confidentiality. The year included in the foregoing notice is the
//  year of creation of the work.
//
//-----------------------------------------------------------------------------

/**
 * @file   dci.h
 * @brief  Contains DCI (Driver Control
 * Interface) definitions and data structures
 *
 */

#ifndef __DCI_H__
#define __DCI_H__


typedef unsigned int dciCommandId_t;
typedef unsigned int dciResponseId_t;
typedef unsigned int dciReturnCode_t;

/**
 * Return codes of driver commands.
 * NOTE: This part is just a copy pasty from drASDError.h
 * concerning DCI error codes. If a modification is required
 * for this part, please also make the same change in drASDError.h
 */
#define RET_E_ASD_OK                        0x00000000
// ASD driver errors related to DCI range from 0x000000A0 to 0x000000AF
#define RET_E_ASD_DCI_INVALID_INPUT         0x000000A0
#define RET_E_ASD_DCI_UNKNOWN_CMD           0x000000A1

/**
 * DCI command header.
 */
typedef struct{
    dciCommandId_t commandId; /**< Command ID */
} dciCommandHeader_t;

/**
 * DCI response header.
 */
typedef struct{
    dciResponseId_t     responseId; /**< Response ID (must be command ID | RSP_ID_MASK )*/
    dciReturnCode_t     returnCode; /**< Return code of command */
} dciResponseHeader_t;

#endif // __DCI_H__
