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
    FUNCENTER;
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
    FUNCEXIT;
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
  FUNCENTER;
  DWARNING("Only Executable memory supported requested Flags:%ld\n", Flags);
  MemoryBlock Result;
  EC = std::error_code();
  if (NumBytes != 0){
    static size_t DefaultGranularity = getAllocationGranularity();    
    
    uint32 AllocType  = MEMF_EXECUTABLE;  
    size_t Granularity = DefaultGranularity;  
    size_t NumBlocks = (NumBytes + Granularity - 1) / Granularity;


    size_t AllocSize = NumBlocks * Granularity;
    void *PA = IExec->AllocVecTags(AllocSize, AVT_Type, AllocType, TAG_DONE);

    if (PA == NULL) {
        DWARNING("Failed request AllocSize:%lu\n", AllocSize);  
        DWARNING("Failed request Granularity:%lu\n", Granularity);  
        DWARNING("Failed request NumBytes:%lu\n", NumBytes);  
        EC = std::error_code(errno, std::generic_category());
    }
    else {   
        DWARNING("Allocation request AllocSize:%lu\n", AllocSize);  
        DWARNING("Allocation request Granularity:%lu\n", Granularity);  
        DWARNING("Allocation request NumBytes:%lu\n", NumBytes);          
        Result.Address = PA;
        Result.AllocatedSize = AllocSize;
        Result.Flags = (Flags & ~MF_HUGE_HINT);

        if (Flags & MF_EXEC)
            Memory::InvalidateInstructionCache(Result.Address, AllocSize);        
    }    
  }
  FUNCEXIT;
  return Result;
}

  std::error_code Memory::releaseMappedMemory(MemoryBlock &M) {
    FUNCENTER;
    if (M.Address == 0 || M.AllocatedSize == 0)
        return std::error_code();
    IExec->FreeVec(M.Address);  
    M.Address = 0;
    M.AllocatedSize = 0;
    FUNCEXIT;
    return std::error_code();
}

  std::error_code Memory::protectMappedMemory(const MemoryBlock &M,
                                       unsigned Flags) {

  FUNCENTER;                                      
  DWARNING("LLVM Bug changing protection cant update Flags as Memory Block is Read Only\n",0);
  DWARNING("Not Supported Old Flags:%ld New Flags:%ld\n", M.Flags, Flags);
                                             
  if (M.Address == 0 || M.AllocatedSize == 0)
    FUNCEXIT;
    return std::error_code();
  
  
  if (Flags & MF_EXEC)
    Memory::InvalidateInstructionCache(M.Address, M.AllocatedSize);

  FUNCEXIT;
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
