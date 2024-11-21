//
//  Copyright(c) 2024 rev.ng Labs Srl. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#pragma once

#include <stdint.h>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

// Pseudo instructions refers to extra LLVM instructions implemented as
// calls to undefined functions.  They are useful for amending LLVM IR to
// simplify mapping to TCG in the backend, e.g.
//
//   %2 = call i32 @IdentityMap.i32.i16(i16 %1)
//
// is a pseudo opcode used to communicate that %1 and %2 should be mapped
// to the same value in TCG.

enum PseudoInstArg {
    ArgInt,
    ArgVec,
    ArgPtr,
    ArgLabel,
    ArgVoid,
};

#define PSEUDO_INST_DEF(name, ret, args) name
enum PseudoInst : uint8_t {
#include "PseudoInst.inc"
};
#undef PSEUDO_INST_DEF

// Retrieve string representation and argument counts for a given
// pseudo instruction.
const char *pseudoInstName(PseudoInst Inst);
uint8_t pseudoInstArgCount(PseudoInst Inst);

// Maps PseudoInst + return/argument types to a FunctionCallee that can be
// called.
llvm::FunctionCallee pseudoInstFunction(llvm::Module &M, PseudoInst Inst,
                                        llvm::Type *RetType,
                                        llvm::ArrayRef<llvm::Type *> ArgTypes);

// Reverse mapping of above, takes a call instruction and attempts to map the
// callee to a PseudoInst.
PseudoInst getPseudoInstFromCall(const llvm::CallInst *Call);
