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
