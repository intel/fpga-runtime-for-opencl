// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_ICD_DISPATCH_H
#define ACL_ICD_DISPATCH_H

#ifdef _MSC_VER
#pragma warning(push)
// C:\Program Files (x86)\Windows Kits\10\include\10.0.17763.0\um\d3d11.h(1349):
// warning C4365: '=': conversion from 'LONG' to 'UINT', signed/unsigned
// mismatch
#pragma warning(disable : 4365)
// C:\Program Files (x86)\Windows Kits\10\include\10.0.17763.0\um\d3d11.h(3477):
// warning C4061: enumerator 'D3D_SRV_DIMENSION_UNKNOWN' in switch of enum
// 'D3D_SRV_DIMENSION' is not explicitly handled by a case label
#pragma warning(disable : 4061)
#endif
#include <CL/cl_icd.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef __GNUC__
#pragma GCC visibility push(protected)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

extern cl_icd_dispatch acl_icd_dispatch;

#if defined(__cplusplus)
} /* extern "C" */
#endif

#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#endif
