//===--- AmigaOS/Watchdog.inc - AmigaOS Watchdog Implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the generic AmigaOS implementation of the Watchdog class.
//
//===----------------------------------------------------------------------===//

// TODO: implement.
// Currently this is only used by PrettyStackTrace which is also unimplemented
// on AmigaOS.

namespace llvm {
  namespace sys {
    Watchdog::Watchdog(unsigned int seconds) {}
    Watchdog::~Watchdog() {}
  }
}