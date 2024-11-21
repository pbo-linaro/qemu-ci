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

#include <PrepareForTcgPass.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Local.h>

using namespace llvm;

static void removeFunctionsWithLoops(Module &M, ModuleAnalysisManager &MAM)
{
    // Iterate over all Strongly Connected Components (SCCs), a SCC implies
    // the existence of loops if:
    //   - it has more than one node, or;
    //   - it has a self-edge.
    SmallVector<Function *, 16> FunctionsToRemove;
    for (Function &F : M) {
        if (F.isDeclaration()) {
            continue;
        }
        for (auto It = scc_begin(&F); !It.isAtEnd(); ++It) {
#if LLVM_VERSION_MAJOR > 10
            if (It.hasCycle()) {
#else
            if (It.hasLoop()) {
#endif
                FunctionsToRemove.push_back(&F);
                break;
            }
        }
    }

    for (auto *F : FunctionsToRemove) {
        F->deleteBody();
    }
}

inline void demotePhis(Function &F)
{
    if (F.isDeclaration()) {
        return;
    }

    SmallVector<PHINode *, 10> Phis;
    for (auto &I : instructions(F)) {
        if (auto *Phi = dyn_cast<PHINode>(&I)) {
            Phis.push_back(Phi);
        }
    }

    for (auto *Phi : Phis) {
        DemotePHIToStack(Phi);
    }
}

PreservedAnalyses PrepareForTcgPass::run(Module &M, ModuleAnalysisManager &MAM)
{
    removeFunctionsWithLoops(M, MAM);
    for (Function &F : M) {
        demotePhis(F);
    }
    return PreservedAnalyses::none();
}
