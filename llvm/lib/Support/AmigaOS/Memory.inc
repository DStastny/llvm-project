//===- Support/AmigaOS/Memory.inc - AmigaOS Memory Implementation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the AmigaOS specific implementation of various Memory
// management utilities
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Process.h"


// The Windows.h header must be the last one included.
#include "llvm/Support/AmigaOS/AmigaOSSupport.h"


static size_t getAllocationGranularity() {
    uint32 pageSizeBits = 0; 
    uint32 pageSize = 0;

    IExec->GetCPUInfoTags(GCIT_ExecPageSize, &pageSizeBits, TAG_END);
    for (uint32 i = 0; i < 32; i++)
    {        
        if (pageSizeBits & (1UL << i))
        {
          
            pageSize = (1UL << i);
            break;
        }
    }
    return pageSize; 
}


namespace llvm {
namespace sys {

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only AmigaOS specific code
//===          and must not be UNIX code
//===----------------------------------------------------------------------===//

MemoryBlock Memory::allocateMappedMemory(size_t NumBytes,
                                         const MemoryBlock *const NearBlock,
                                         unsigned Flags,
                                         std::error_code &EC) {
  EC = std::error_code();
  if (NumBytes == 0)
    return MemoryBlock();

  static size_t DefaultGranularity = getAllocationGranularity();
  
  DWARNING("Only Executable memory supproted requested Flags:%lld\n", Flags);
  uint32 AllocType  = MEMF_EXECUTABLE;  
  size_t Granularity = DefaultGranularity;  
  size_t NumBlocks = (NumBytes + Granularity - 1) / Granularity;


  size_t AllocSize = NumBlocks * Granularity;
  void *PA = IExec->AllocVecTags(AllocSize, AVT_Type, AllocType, TAG_DONE);

  if (PA == NULL) {
    EC = std::error_code(errno, std::generic_category());
    return MemoryBlock();
  }

  MemoryBlock Result;
  Result.Address = PA;
  Result.AllocatedSize = AllocSize;
  Result.Flags = (Flags & ~MF_HUGE_HINT);

  if (Flags & MF_EXEC)
    Memory::InvalidateInstructionCache(Result.Address, AllocSize);

  return Result;
}

  std::error_code Memory::releaseMappedMemory(MemoryBlock &M) {
  if (M.Address == 0 || M.AllocatedSize == 0)
    return std::error_code();

  IExec->FreeVec(M.Address);  
  M.Address = 0;
  M.AllocatedSize = 0;

  return std::error_code();
}

  std::error_code Memory::protectMappedMemory(const MemoryBlock &M,
                                       unsigned Flags) {
  if (M.Address == 0 || M.AllocatedSize == 0)
    return std::error_code();
  
  DWARNING("LLVM Bug changing protection cant update Flags as Memory Block is Read Only\n",0);
  DWARNING("Not Supported Old Flags:%lld New Flags:%lld\n", M.Flags, Flags);
  
  if (Flags & MF_EXEC)
    Memory::InvalidateInstructionCache(M.Address, M.AllocatedSize);

  return std::error_code();
}

/// InvalidateInstructionCache - Before the JIT can run a block of code
/// that has been emitted it must invalidate the instruction cache on some
/// platforms.
void Memory::InvalidateInstructionCache(
    const void *Addr, size_t Len) {
  IExec->CacheClearE(const_cast<APTR>(Addr), Len, CACRF_ClearI);  
}

} // namespace sys
} // namespace llvm
