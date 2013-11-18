/* The file is part of Snowman decompiler.             */
/* See doc/licenses.txt for the licensing information. */

//
// SmartDec decompiler - SmartDec is a native code to C/C++ decompiler
// Copyright (C) 2015 Alexander Chernov, Katerina Troshina, Yegor Derevenets,
// Alexander Fokin, Sergey Levin, Leonid Tsvetkov
//
// This file is part of SmartDec decompiler.
//
// SmartDec decompiler is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SmartDec decompiler is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SmartDec decompiler.  If not, see <http://www.gnu.org/licenses/>.
//

#include "IntelMasterAnalyzer.h"

#include <nc/common/Conversions.h>
#include <nc/common/Foreach.h>
#include <nc/common/make_unique.h>

#include <nc/core/Module.h>
#include <nc/core/Context.h>
#include <nc/core/ir/Program.h>
#include <nc/core/ir/Statements.h>
#include <nc/core/ir/Terms.h>
#include <nc/core/ir/calling/Conventions.h>
#include <nc/core/ir/calling/Hooks.h>
#include <nc/core/ir/dflow/Dataflow.h>

#include "IntelArchitecture.h"
#include "IntelDataflowAnalyzer.h"
#include "IntelRegisters.h"

namespace nc {
namespace arch {
namespace intel {

void IntelMasterAnalyzer::createProgram(core::Context &context) const {
    MasterAnalyzer::createProgram(context);

    /*
     * Patch the IR to implement x86-64 implicit zero extend.
     */
    if (context.module()->architecture()->bitness() == 64) {
        auto minDomain = IntelRegisters::rax()->memoryLocation().domain();
        auto maxDomain = IntelRegisters::r15()->memoryLocation().domain();

        auto program = const_cast<core::ir::Program *>(context.program());

        foreach (auto *basicBlock, program->basicBlocks()) {
            context.cancellationToken().poll();

            std::vector<std::pair<const core::ir::Statement *, std::unique_ptr<core::ir::Statement>>> patchList;

            foreach (auto statement, basicBlock->statements()) {
                if (auto assignment = statement->asAssignment()) {
                    if (auto access = assignment->left()->asMemoryLocationAccess()) {
                        if (minDomain <= access->memoryLocation().domain() &&
                            access->memoryLocation().domain() <= maxDomain &&
                            access->memoryLocation().addr() == 0 &&
                            access->memoryLocation().size() == 32)
                        {
                            patchList.push_back(std::make_pair(
                                statement,
                                std::make_unique<core::ir::Assignment>(
                                    std::make_unique<core::ir::MemoryLocationAccess>(access->memoryLocation().shifted(32)),
                                    std::make_unique<core::ir::Constant>(SizedValue(32, 0)))));
                        }
                    }
                }
            }

            basicBlock->addStatements(std::move(patchList));
        }
    }
}

void IntelMasterAnalyzer::detectCallingConvention(core::Context &context, const core::ir::calling::CalleeId &calleeId) const {
    auto architecture = context.module()->architecture();

    auto setConvention = [&](const char *name) {
        context.conventions()->setConvention(calleeId, architecture->getCallingConvention(QLatin1String(name)));
    };

    if (architecture->bitness() == 32) {
        if (auto addr = calleeId.entryAddress()) {
            const QString &symbol = context.module()->getName(*addr);
            int index = symbol.lastIndexOf(QChar('@'));
            if (index != -1) {
                ByteSize argumentsSize;
                if (stringToInt(symbol.mid(index + 1), &argumentsSize)) {
                    setConvention("stdcall32");
                    context.conventions()->setArgumentsSize(calleeId, argumentsSize);
                    return;
                }
            }
        }
    }

    switch (architecture->bitness()) {
        case 16:
            setConvention("cdecl16");
            break;
        case 32:
            setConvention("cdecl32");
            break;
        case 64:
            setConvention("amd64");
            break;
    }
}

void IntelMasterAnalyzer::analyzeDataflow(core::Context &context, const core::ir::Function *function) const {
    std::unique_ptr<core::ir::dflow::Dataflow> dataflow(new core::ir::dflow::Dataflow());

    IntelDataflowAnalyzer(*dataflow, context.module()->architecture(), function, context.hooks())
        .analyze(context.cancellationToken());

    context.setDataflow(function, std::move(dataflow));
}

} // namespace intel
} // namespace arch
} // namespace nc

/* vim:set et sts=4 sw=4: */