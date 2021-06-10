// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>

#include "Common/Arm64Emitter.h"
#include "Common/BitSet.h"
#include "Common/CommonTypes.h"

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/PowerPC/JitArm64/Jit.h"
#include "Core/PowerPC/JitArm64/JitArm64_RegCache.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PPCTables.h"
#include "Core/PowerPC/PowerPC.h"

using namespace Arm64Gen;

void JitArm64::lfXX(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITLoadStoreFloatingOff);
  FALLBACK_IF(jo.memcheck);

  u32 a = inst.RA, b = inst.RB;

  s32 offset = inst.SIMM_16;
  u32 flags = BackPatchInfo::FLAG_LOAD;
  bool update = false;
  s32 offset_reg = -1;

  switch (inst.OPCD)
  {
  case 31:
    switch (inst.SUBOP10)
    {
    case 567:  // lfsux
      flags |= BackPatchInfo::FLAG_SIZE_F32;
      update = true;
      offset_reg = b;
      break;
    case 535:  // lfsx
      flags |= BackPatchInfo::FLAG_SIZE_F32;
      offset_reg = b;
      break;
    case 631:  // lfdux
      flags |= BackPatchInfo::FLAG_SIZE_F64;
      update = true;
      offset_reg = b;
      break;
    case 599:  // lfdx
      flags |= BackPatchInfo::FLAG_SIZE_F64;
      offset_reg = b;
      break;
    }
    break;
  case 49:  // lfsu
    flags |= BackPatchInfo::FLAG_SIZE_F32;
    update = true;
    break;
  case 48:  // lfs
    flags |= BackPatchInfo::FLAG_SIZE_F32;
    break;
  case 51:  // lfdu
    flags |= BackPatchInfo::FLAG_SIZE_F64;
    update = true;
    break;
  case 50:  // lfd
    flags |= BackPatchInfo::FLAG_SIZE_F64;
    break;
  }

  u32 imm_addr = 0;
  bool is_immediate = false;

  const RegType type =
      (flags & BackPatchInfo::FLAG_SIZE_F64) != 0 ? RegType::LowerPair : RegType::DuplicatedSingle;

  gpr.Lock(ARM64Reg::W0, ARM64Reg::W30);
  fpr.Lock(ARM64Reg::Q0);

  const ARM64Reg VD = fpr.RW(inst.FD, type);
  ARM64Reg addr_reg = ARM64Reg::W0;

  if (update)
  {
    // Always uses RA
    if (gpr.IsImm(a) && offset_reg == -1)
    {
      is_immediate = true;
      imm_addr = offset + gpr.GetImm(a);
    }
    else if (gpr.IsImm(a) && offset_reg != -1 && gpr.IsImm(offset_reg))
    {
      is_immediate = true;
      imm_addr = gpr.GetImm(a) + gpr.GetImm(offset_reg);
    }
    else
    {
      if (offset_reg == -1)
      {
        ADDI2R(addr_reg, gpr.R(a), offset, addr_reg);
      }
      else
      {
        ADD(addr_reg, gpr.R(offset_reg), gpr.R(a));
      }
    }
  }
  else
  {
    if (offset_reg == -1)
    {
      if (a && gpr.IsImm(a))
      {
        is_immediate = true;
        imm_addr = gpr.GetImm(a) + offset;
      }
      else if (a)
      {
        ADDI2R(addr_reg, gpr.R(a), offset, addr_reg);
      }
      else
      {
        is_immediate = true;
        imm_addr = offset;
      }
    }
    else
    {
      if (a && gpr.IsImm(a) && gpr.IsImm(offset_reg))
      {
        is_immediate = true;
        imm_addr = gpr.GetImm(a) + gpr.GetImm(offset_reg);
      }
      else if (!a && gpr.IsImm(offset_reg))
      {
        is_immediate = true;
        imm_addr = gpr.GetImm(offset_reg);
      }
      else if (a)
      {
        ADD(addr_reg, gpr.R(a), gpr.R(offset_reg));
      }
      else
      {
        MOV(addr_reg, gpr.R(offset_reg));
      }
    }
  }

  ARM64Reg XA = EncodeRegTo64(addr_reg);

  if (is_immediate)
    MOVI2R(XA, imm_addr);

  if (update)
  {
    gpr.BindToRegister(a, false);
    MOV(gpr.R(a), addr_reg);
  }

  BitSet32 regs_in_use = gpr.GetCallerSavedUsed();
  BitSet32 fprs_in_use = fpr.GetCallerSavedUsed();
  regs_in_use[DecodeReg(ARM64Reg::W0)] = 0;
  fprs_in_use[DecodeReg(ARM64Reg::Q0)] = 0;
  fprs_in_use[DecodeReg(VD)] = 0;

  if (jo.fastmem_arena && is_immediate && PowerPC::IsOptimizableRAMAddress(imm_addr))
  {
    EmitBackpatchRoutine(flags, true, false, VD, XA, BitSet32(0), BitSet32(0));
  }
  else
  {
    EmitBackpatchRoutine(flags, jo.fastmem, jo.fastmem, VD, XA, regs_in_use, fprs_in_use);
  }

  gpr.Unlock(ARM64Reg::W0, ARM64Reg::W30);
  fpr.Unlock(ARM64Reg::Q0);
}

void JitArm64::stfXX(UGeckoInstruction inst)
{
  INSTRUCTION_START
  JITDISABLE(bJITLoadStoreFloatingOff);
  FALLBACK_IF(jo.memcheck);

  u32 a = inst.RA, b = inst.RB;

  bool want_single = false;
  s32 offset = inst.SIMM_16;
  u32 flags = BackPatchInfo::FLAG_STORE;
  bool update = false;
  s32 offset_reg = -1;

  switch (inst.OPCD)
  {
  case 31:
    switch (inst.SUBOP10)
    {
    case 663:  // stfsx
      want_single = true;
      flags |= BackPatchInfo::FLAG_SIZE_F32;
      offset_reg = b;
      break;
    case 695:  // stfsux
      want_single = true;
      flags |= BackPatchInfo::FLAG_SIZE_F32;
      update = true;
      offset_reg = b;
      break;
    case 727:  // stfdx
      flags |= BackPatchInfo::FLAG_SIZE_F64;
      offset_reg = b;
      break;
    case 759:  // stfdux
      flags |= BackPatchInfo::FLAG_SIZE_F64;
      update = true;
      offset_reg = b;
      break;
    case 983:  // stfiwx
      // This instruction writes the lower 32 bits of a double. want_single must be false
      flags |= BackPatchInfo::FLAG_SIZE_F32;
      offset_reg = b;
      break;
    }
    break;
  case 53:  // stfsu
    want_single = true;
    flags |= BackPatchInfo::FLAG_SIZE_F32;
    update = true;
    break;
  case 52:  // stfs
    want_single = true;
    flags |= BackPatchInfo::FLAG_SIZE_F32;
    break;
  case 55:  // stfdu
    flags |= BackPatchInfo::FLAG_SIZE_F64;
    update = true;
    break;
  case 54:  // stfd
    flags |= BackPatchInfo::FLAG_SIZE_F64;
    break;
  }

  u32 imm_addr = 0;
  bool is_immediate = false;

  fpr.Lock(ARM64Reg::Q0);

  const bool have_single = fpr.IsSingle(inst.FS, true);

  ARM64Reg V0 =
      fpr.R(inst.FS, want_single && have_single ? RegType::LowerPairSingle : RegType::LowerPair);

  if (want_single && !have_single)
  {
    const ARM64Reg single_reg = fpr.GetReg();
    ConvertDoubleToSingleLower(inst.FS, single_reg, V0);
    V0 = single_reg;
  }

  gpr.Lock(ARM64Reg::W0, ARM64Reg::W1, ARM64Reg::W30);

  ARM64Reg addr_reg = ARM64Reg::W1;

  if (update)
  {
    // Always uses RA
    if (gpr.IsImm(a) && offset_reg == -1)
    {
      is_immediate = true;
      imm_addr = offset + gpr.GetImm(a);
    }
    else if (gpr.IsImm(a) && offset_reg != -1 && gpr.IsImm(offset_reg))
    {
      is_immediate = true;
      imm_addr = gpr.GetImm(a) + gpr.GetImm(offset_reg);
    }
    else
    {
      if (offset_reg == -1)
      {
        ADDI2R(addr_reg, gpr.R(a), offset, addr_reg);
      }
      else
      {
        ADD(addr_reg, gpr.R(offset_reg), gpr.R(a));
      }
    }
  }
  else
  {
    if (offset_reg == -1)
    {
      if (a && gpr.IsImm(a))
      {
        is_immediate = true;
        imm_addr = gpr.GetImm(a) + offset;
      }
      else if (a)
      {
        ADDI2R(addr_reg, gpr.R(a), offset, addr_reg);
      }
      else
      {
        is_immediate = true;
        imm_addr = offset;
      }
    }
    else
    {
      if (a && gpr.IsImm(a) && gpr.IsImm(offset_reg))
      {
        is_immediate = true;
        imm_addr = gpr.GetImm(a) + gpr.GetImm(offset_reg);
      }
      else if (!a && gpr.IsImm(offset_reg))
      {
        is_immediate = true;
        imm_addr = gpr.GetImm(offset_reg);
      }
      else if (a)
      {
        ADD(addr_reg, gpr.R(a), gpr.R(offset_reg));
      }
      else
      {
        MOV(addr_reg, gpr.R(offset_reg));
      }
    }
  }

  ARM64Reg XA = EncodeRegTo64(addr_reg);

  if (is_immediate && !(jo.optimizeGatherPipe && PowerPC::IsOptimizableGatherPipeWrite(imm_addr)))
  {
    MOVI2R(XA, imm_addr);

    if (update)
    {
      gpr.BindToRegister(a, false);
      MOV(gpr.R(a), addr_reg);
    }
  }
  else if (!is_immediate && update)
  {
    gpr.BindToRegister(a, false);
    MOV(gpr.R(a), addr_reg);
  }

  BitSet32 regs_in_use = gpr.GetCallerSavedUsed();
  BitSet32 fprs_in_use = fpr.GetCallerSavedUsed();
  regs_in_use[DecodeReg(ARM64Reg::W0)] = 0;
  regs_in_use[DecodeReg(ARM64Reg::W1)] = 0;
  fprs_in_use[DecodeReg(ARM64Reg::Q0)] = 0;

  if (is_immediate)
  {
    if (jo.optimizeGatherPipe && PowerPC::IsOptimizableGatherPipeWrite(imm_addr))
    {
      int accessSize;
      if (flags & BackPatchInfo::FLAG_SIZE_F64)
        accessSize = 64;
      else
        accessSize = 32;

      LDR(IndexType::Unsigned, ARM64Reg::X0, PPC_REG, PPCSTATE_OFF(gather_pipe_ptr));

      if (flags & BackPatchInfo::FLAG_SIZE_F64)
        m_float_emit.REV64(8, ARM64Reg::Q0, V0);
      else if (flags & BackPatchInfo::FLAG_SIZE_F32)
        m_float_emit.REV32(8, ARM64Reg::D0, V0);

      m_float_emit.STR(accessSize, IndexType::Post, accessSize == 64 ? ARM64Reg::Q0 : ARM64Reg::D0,
                       ARM64Reg::X0, accessSize >> 3);

      STR(IndexType::Unsigned, ARM64Reg::X0, PPC_REG, PPCSTATE_OFF(gather_pipe_ptr));
      js.fifoBytesSinceCheck += accessSize >> 3;

      if (update)
      {
        // Chance of this happening is fairly low, but support it
        gpr.BindToRegister(a, false);
        MOVI2R(gpr.R(a), imm_addr);
      }
    }
    else if (jo.fastmem_arena && PowerPC::IsOptimizableRAMAddress(imm_addr))
    {
      EmitBackpatchRoutine(flags, true, false, V0, XA, BitSet32(0), BitSet32(0));
    }
    else
    {
      EmitBackpatchRoutine(flags, false, false, V0, XA, regs_in_use, fprs_in_use);
    }
  }
  else
  {
    EmitBackpatchRoutine(flags, jo.fastmem, jo.fastmem, V0, XA, regs_in_use, fprs_in_use);
  }

  if (want_single && !have_single)
    fpr.Unlock(V0);

  gpr.Unlock(ARM64Reg::W0, ARM64Reg::W1, ARM64Reg::W30);
  fpr.Unlock(ARM64Reg::Q0);
}