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

#include "FunctionAnnotation.h"
#include "TcgGlobalMap.h"
#include <llvm/IR/PassManager.h>

//
// TcgGenPass
//
// Backend pass responsible for emitting the final TCG code.  Ideally this pass
// should be as simple as possible simply mapping one expression LLVM IR
// directly to another in TCG.
//
// However, we currently still rely on this pass to perform the mapping of
// constants. (mapping of values is handled by the TcgTempAllocationPass.)
//

class TcgGenPass : public llvm::PassInfoMixin<TcgGenPass> {
    llvm::raw_ostream &OutSource;
    llvm::raw_ostream &OutHeader;
    llvm::raw_ostream &OutEnabled;
    llvm::raw_ostream &OutLog;
    llvm::StringRef HeaderPath;
    const AnnotationMapTy &Annotations;
    const TcgGlobalMap &TcgGlobals;

public:
    TcgGenPass(llvm::raw_ostream &OutSource, llvm::raw_ostream &OutHeader,
               llvm::raw_ostream &OutEnabled, llvm::raw_ostream &OutLog,
               llvm::StringRef HeaderPath, const AnnotationMapTy &Annotations,
               const TcgGlobalMap &TcgGlobals)
        : OutSource(OutSource), OutHeader(OutHeader), OutEnabled(OutEnabled),
          OutLog(OutLog), HeaderPath(HeaderPath), Annotations(Annotations),
          TcgGlobals(TcgGlobals)
    {
    }

    llvm::PreservedAnalyses run(llvm::Module &M,
                                llvm::ModuleAnalysisManager &MAM);
};
