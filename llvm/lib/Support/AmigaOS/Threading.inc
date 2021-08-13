//===- AmigaOS/Threading.inc - AmigaOS Threading Implementation ----- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the AmigaOS specific implementation of Threading functions.
//
//===----------------------------------------------------------------------===//


// AmigaOS uses PThreads so fall back to unix

#include "../Unix/Threading.inc"
