//===- AmigaOSSupport.h - Common Windows Include File -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines things specific to AmigaOS implementations.  In addition to
// providing some helpers for working with AmigaOS APIs, this header wraps
// <windows.h> with some portability macros.  Always include AmigaOSSupport.h
// instead of including Amiga Headers directly.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_SUPPORT_AMIGAOSSUPPORT_H
#define LLVM_SUPPORT_AMIGAOSSUPPORT_H



#include <proto/exec.h>
#include <proto/dos.h>



#define FUNCENTER DPRINTF("Method %s() entered\n", __func__)
#define FUNCEXIT  DPRINTF("Method %s() exited\n", __func__)

#ifdef DEBUG
#define TASKID (APTR)  ::IExec->FindTask(0)
#define DPRINTF(errMsg, ...) ::IExec->DebugPrintF("Task : %p ", TASKID); ::IExec->DebugPrintF(errMsg, ##__VA_ARGS__); 
#else
#define TASKID
#define DPRINTF(errMsg, ...)
#endif

#define NOTIMPLEMENTED ::IDOS->Printf("Method %s() not Implemented\n", __func__)
#define DWARNING(errMsg, ...) ::IDOS->Printf("Method %s ", __func__); ::IDOS->Printf(errMsg, ##__VA_ARGS__); 

#endif



