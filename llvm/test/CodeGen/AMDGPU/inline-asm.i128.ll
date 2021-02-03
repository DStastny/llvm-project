; NOTE: Assertions have been autogenerated by utils/update_mir_test_checks.py
; RUN: llc -mtriple=amdgcn-amd-amdhsa -mcpu=gfx908 -stop-after=finalize-isel -o - %s | FileCheck %s

; Make sure we only use one 128-bit register instead of 2 for i128 asm
; constraints

define amdgpu_kernel void @s_input_output_i128() {
  ; CHECK-LABEL: name: s_input_output_i128
  ; CHECK: bb.0 (%ir-block.0):
  ; CHECK:   INLINEASM &"; def $0", 1 /* sideeffect attdialect */, 3997706 /* regdef:SGPR_128 */, def %4
  ; CHECK:   [[COPY:%[0-9]+]]:sgpr_128 = COPY %4
  ; CHECK:   INLINEASM &"; use $0", 1 /* sideeffect attdialect */, 3997705 /* reguse:SGPR_128 */, [[COPY]]
  ; CHECK:   S_ENDPGM 0
  %val = tail call i128 asm sideeffect "; def $0", "=s"()
  call void asm sideeffect "; use $0", "s"(i128 %val)
  ret void
}

define amdgpu_kernel void @v_input_output_i128() {
  ; CHECK-LABEL: name: v_input_output_i128
  ; CHECK: bb.0 (%ir-block.0):
  ; CHECK:   INLINEASM &"; def $0", 1 /* sideeffect attdialect */, 3866634 /* regdef:VReg_128 */, def %4
  ; CHECK:   [[COPY:%[0-9]+]]:vreg_128 = COPY %4
  ; CHECK:   INLINEASM &"; use $0", 1 /* sideeffect attdialect */, 3866633 /* reguse:VReg_128 */, [[COPY]]
  ; CHECK:   S_ENDPGM 0
  %val = tail call i128 asm sideeffect "; def $0", "=v"()
  call void asm sideeffect "; use $0", "v"(i128 %val)
  ret void
}

define amdgpu_kernel void @a_input_output_i128() {
  ; CHECK-LABEL: name: a_input_output_i128
  ; CHECK: bb.0 (%ir-block.0):
  ; CHECK:   INLINEASM &"; def $0", 1 /* sideeffect attdialect */, 3801098 /* regdef:AReg_128 */, def %4
  ; CHECK:   [[COPY:%[0-9]+]]:areg_128 = COPY %4
  ; CHECK:   INLINEASM &"; use $0", 1 /* sideeffect attdialect */, 3801097 /* reguse:AReg_128 */, [[COPY]]
  ; CHECK:   S_ENDPGM 0
  %val = call i128 asm sideeffect "; def $0", "=a"()
  call void asm sideeffect "; use $0", "a"(i128 %val)
  ret void
}