//===- AmigaOS/DynamicLibrary.inc - AmigaOS DL Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the AmigaOS specific implementation of DynamicLibrary.
//
//===----------------------------------------------------------------------===//


#include "../Unix/DynamicLibrary.inc"
// for clib2 we need to define error variable
#include <libraries/elf.h>

/****************************************************************************/
Elf32_Error __elf_error_code;


