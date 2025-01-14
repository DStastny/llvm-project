//====--- OMPGridValues.h - Language-specific address spaces --*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Provides definitions for Target specific Grid Values
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPGRIDVALUES_H
#define LLVM_FRONTEND_OPENMP_OMPGRIDVALUES_H

namespace llvm {

namespace omp {

/// \brief Defines various target-specific GPU grid values that must be
///        consistent between host RTL (plugin), device RTL, and clang.
///        We can change grid values for a "fat" binary so that different
///        passes get the correct values when generating code for a
///        multi-target binary. Both amdgcn and nvptx values are stored in
///        this file. In the future, should there be differences between GPUs
///        of the same architecture, then simply make a different array and
///        use the new array name.
///
/// Example usage in clang:
///   const unsigned slot_size =
///   ctx.GetTargetInfo().getGridValue().GV_Warp_Size;
///
/// Example usage in libomptarget/deviceRTLs:
///   #include "llvm/Frontend/OpenMP/OMPGridValues.h"
///   #ifdef __AMDGPU__
///     #define GRIDVAL AMDGPUGridValues
///   #else
///     #define GRIDVAL NVPTXGridValues
///   #endif
///   ... Then use this reference for GV_Warp_Size in the deviceRTL source.
///   llvm::omp::GRIDVAL().GV_Warp_Size
///
/// Example usage in libomptarget hsa plugin:
///   #include "llvm/Frontend/OpenMP/OMPGridValues.h"
///   #define GRIDVAL AMDGPUGridValues
///   ... Then use this reference to access GV_Warp_Size in the hsa plugin.
///   llvm::omp::GRIDVAL().GV_Warp_Size
///
/// Example usage in libomptarget cuda plugin:
///    #include "llvm/Frontend/OpenMP/OMPGridValues.h"
///    #define GRIDVAL NVPTXGridValues
///   ... Then use this reference to access GV_Warp_Size in the cuda plugin.
///    llvm::omp::GRIDVAL().GV_Warp_Size
///

struct GV {
  /// The maximum number of workers in a kernel.
  /// (THREAD_ABSOLUTE_LIMIT) - (GV_Warp_Size), might be issue for blockDim.z
  const unsigned GV_Threads;
  /// The size reserved for data in a shared memory slot.
  const unsigned GV_Slot_Size;
  /// The default value of maximum number of threads in a worker warp.
  const unsigned GV_Warp_Size;

  constexpr unsigned warpSlotSize() const {
    return GV_Warp_Size * GV_Slot_Size;
  }

  /// the maximum number of teams.
  const unsigned GV_Max_Teams;
  // An alternative to the heavy data sharing infrastructure that uses global
  // memory is one that uses device __shared__ memory.  The amount of such space
  // (in bytes) reserved by the OpenMP runtime is noted here.
  const unsigned GV_SimpleBufferSize;
  // The absolute maximum team size for a working group
  const unsigned GV_Max_WG_Size;
  // The default maximum team size for a working group
  const unsigned GV_Default_WG_Size;

  constexpr unsigned maxWarpNumber() const {
    return GV_Max_WG_Size / GV_Warp_Size;
  }
};

/// For AMDGPU GPUs
static constexpr GV AMDGPUGridValues = {
    448,  // GV_Threads
    256,  // GV_Slot_Size
    64,   // GV_Warp_Size
    128,  // GV_Max_Teams
    896,  // GV_SimpleBufferSize
    1024, // GV_Max_WG_Size,
    256,  // GV_Default_WG_Size
};

/// For Nvidia GPUs
static constexpr GV NVPTXGridValues = {
    992,  // GV_Threads
    256,  // GV_Slot_Size
    32,   // GV_Warp_Size
    1024, // GV_Max_Teams
    896,  // GV_SimpleBufferSize
    1024, // GV_Max_WG_Size
    128,  // GV_Default_WG_Size
};

} // namespace omp
} // namespace llvm

#endif // LLVM_FRONTEND_OPENMP_OMPGRIDVALUES_H
