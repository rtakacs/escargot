  /*
 * Copyright (c) 2019-present Samsung Electronics Co., Ltd
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#include "Snapshot.h"

#include "Escargot.h"
#include "runtime/VMInstance.h"
#include "runtime/ExecutionContext.h"
#include "util/Vector.h"
#include "runtime/Value.h"
#include "parser/ScriptParser.h"
#include "interpreter/ByteCodeGenerator.h"
#include "interpreter/ByteCodeInterpreter.h"
#include "interpreter/ByteCode.h"
#include "parser/ast/ProgramNode.h"
#include "parser/CodeBlock.h"
#include "interpreter/ByteCode.h"
#include "util/Util.h"
#include "runtime/Environment.h"
#include "runtime/EnvironmentRecord.h"

#define READ_INTO_VARIABLE(type, name, size, msg) \
    type name; \
    name = (type) snapshot; \
    snapshot += size; \
    if (SNAPSHOT_VERBOSE) \
        printf("%s has been read (%zu bytes)\n", msg, (size_t) size);

namespace Escargot {

std::vector<InterpretedCodeBlock*> codeBlocks;
std::vector<String*> userLiterals;

void Snapshot::execute(Context* context, const char* snapshot) {
    READ_INTO_VARIABLE(SnapshotInfo*, snapshotInfo, sizeof(SnapshotInfo), "Snapshot info");

    if (snapshotInfo->magic != SNAPSHOT_MAGIC) {
        fprintf(stderr, "Wrong file format.\n");
        exit(1);
    }

    if (snapshotInfo->version != SNAPSHOT_VERSION) {
        fprintf(stderr, "Wrong snapshot version.\n");
        exit(1);
    }

    READ_INTO_VARIABLE(GlobalInfo*, globalInfo, sizeof(GlobalInfo), "Global info");

    // Create all the necessary InterpretedCodeBlock objects.
    for (size_t i = 0; i < globalInfo->codeblockCount; i++) {
        codeBlocks.push_back(new InterpretedCodeBlock(context));
    }

    // Read the script infromation.
    READ_INTO_VARIABLE(char*, filename, globalInfo->filenameSize, "Filename info");
    READ_INTO_VARIABLE(char*, source, globalInfo->sourceCodeSize, "Source code info");

    Script* script = new Script(String::fromUTF8(filename, globalInfo->filenameSize),
                                String::fromUTF8(source, globalInfo->sourceCodeSize));

    // Create user defined literals.
    for (size_t i = 0; i < globalInfo->userLiteralCount; i++) {
        READ_INTO_VARIABLE(size_t*, strsize, sizeof(size_t), "User literal size");
        READ_INTO_VARIABLE(char*, strdata, *strsize, "User literal data");

        userLiterals.push_back(String::fromUTF8(strdata, *strsize));
    }

    // Initialize the created CodeBlocks.
    for (size_t i = 0; i < globalInfo->codeblockCount; i++) {
        InterpretedCodeBlock* codeBlock = codeBlocks[i];

        READ_INTO_VARIABLE(CodeBlockInfo*, codeBlockInfo, sizeof(CodeBlockInfo), "CodeBlock info");

        codeBlock->m_byteCodeBlock = new ByteCodeBlock(codeBlock);
        codeBlock->m_byteCodeBlock->m_requiredRegisterFileSizeInValueSize = codeBlockInfo->requiredRegisterCount;
        codeBlock->m_byteCodeBlock->m_isOnGlobal = true;
        codeBlock->m_script = script;
        codeBlock->m_isConstructor = codeBlockInfo->m_isConstructor;
        codeBlock->m_isStrict = codeBlockInfo->m_isStrict;
        codeBlock->m_hasCallNativeFunctionCode = codeBlockInfo->m_hasCallNativeFunctionCode;
        codeBlock->m_isFunctionNameSaveOnHeap = codeBlockInfo->m_isFunctionNameSaveOnHeap;
        codeBlock->m_isFunctionNameExplicitlyDeclared = codeBlockInfo->m_isFunctionNameExplicitlyDeclared;
        codeBlock->m_canUseIndexedVariableStorage = codeBlockInfo->m_canUseIndexedVariableStorage;
        codeBlock->m_canAllocateEnvironmentOnStack = codeBlockInfo->m_canAllocateEnvironmentOnStack;
        codeBlock->m_needsComplexParameterCopy = codeBlockInfo->m_needsComplexParameterCopy;
        codeBlock->m_hasEval = codeBlockInfo->m_hasEval;
        codeBlock->m_hasWith = codeBlockInfo->m_hasWith;
        codeBlock->m_hasSuper = codeBlockInfo->m_hasSuper;
        codeBlock->m_hasCatch = codeBlockInfo->m_hasCatch;
        codeBlock->m_hasYield = codeBlockInfo->m_hasYield;
        codeBlock->m_inCatch = codeBlockInfo->m_inCatch;
        codeBlock->m_inWith = codeBlockInfo->m_inWith;
        codeBlock->m_usesArgumentsObject = codeBlockInfo->m_usesArgumentsObject;
        codeBlock->m_isFunctionExpression = codeBlockInfo->m_isFunctionExpression;
        codeBlock->m_isFunctionDeclaration = codeBlockInfo->m_isFunctionDeclaration;
        codeBlock->m_isFunctionDeclarationWithSpecialBinding = codeBlockInfo->m_isFunctionDeclarationWithSpecialBinding;
        codeBlock->m_isArrowFunctionExpression = codeBlockInfo->m_isArrowFunctionExpression;
        codeBlock->m_isClassConstructor = codeBlockInfo->m_isClassConstructor;
        codeBlock->m_isInWithScope = codeBlockInfo->m_isInWithScope;
        codeBlock->m_isEvalCodeInFunction = codeBlockInfo->m_isEvalCodeInFunction;
        codeBlock->m_needsVirtualIDOperation = codeBlockInfo->m_needsVirtualIDOperation;
        codeBlock->m_needToLoadThisValue = codeBlockInfo->m_needToLoadThisValue;
        codeBlock->m_hasRestElement = codeBlockInfo->m_hasRestElement;

        if (UNLIKELY(codeBlockInfo->parentBlock == std::numeric_limits<uint32_t>::max())) {
            // Global object doesn't have parent nor function name.
            codeBlock->m_parentCodeBlock = nullptr;
        } else {
           AtomicString functionName(context, userLiterals[codeBlockInfo->functionNameIdx]);

           codeBlock->m_functionName = functionName;
           codeBlock->m_parentCodeBlock = codeBlocks[codeBlockInfo->parentBlock];
        }

        for (size_t j = 0; j < codeBlockInfo->childBlockCount; j++) {
            READ_INTO_VARIABLE(size_t*, index, sizeof(size_t), "Child block info");

            codeBlock->m_childBlocks.push_back(codeBlocks[*index]);
        }

        for (size_t j = 0; j < codeBlockInfo->parameterCount; j++) {
            READ_INTO_VARIABLE(ParameterInfo*, parameterInfo, sizeof(ParameterInfo), "Paramter info");

            AtomicString name(context, userLiterals[parameterInfo->parameterNameIdx]);

            InterpretedCodeBlock::FunctionParametersInfo info;
            info.m_isHeapAllocated = parameterInfo->isHeapAllocated;
            info.m_isDuplicated = parameterInfo->isDuplicated;
            info.m_index = parameterInfo->index;
            info.m_name = name;

            codeBlock->m_parametersInfomation.push_back(info);
        }

        for (size_t j = 0; j < codeBlockInfo->identifierCount; j++) {
            READ_INTO_VARIABLE(IdentifierInfo*, identifierInfo, sizeof(IdentifierInfo), "Identifier info");

            AtomicString name(context, userLiterals[identifierInfo->identifierNameIdx]);

            CodeBlock::IdentifierInfo info;
            info.m_needToAllocateOnStack = identifierInfo->needToAllocateOnStack;
            info.m_isMutable = identifierInfo->isMutable;
            info.m_isExplicitlyDeclaredOrParameterName = identifierInfo->isExplicitlyDeclaredOrParameterName;
            info.m_indexForIndexedStorage = identifierInfo->indexForIndexedStorage;
            info.m_name = name;

            codeBlock->m_identifierInfos.push_back(info);
        }

        for (size_t j = 0; j < codeBlockInfo->numeralValueCount; j++) {
            READ_INTO_VARIABLE(Value*, numeralValue, sizeof(Value), "Numeral value info");

            codeBlock->byteCodeBlock()->m_numeralLiteralData.push_back(*numeralValue);
        }

        for (size_t j = 0; j < codeBlockInfo->literalCount; j++) {
            READ_INTO_VARIABLE(size_t*, literalIdx, sizeof(size_t), "Bytecode literal info");

            String* literal = userLiterals[*literalIdx];
            codeBlock->byteCodeBlock()->m_literalData.push_back((void*)literal);
        }

        for (size_t j = 0; j < codeBlockInfo->objectCodePositionsSize; j++) {
            READ_INTO_VARIABLE(size_t*, position, sizeof(size_t), "Object code position info");

            codeBlock->byteCodeBlock()->m_getObjectCodePositions.push_back(*position);
        }

        READ_INTO_VARIABLE(char*, blockSourceCode, codeBlockInfo->sourceCodeSize, "Source code info");
        READ_INTO_VARIABLE(char*, bytecode, codeBlockInfo->byteCodeSize, "Bytecode info");

        String* src = String::fromUTF8(blockSourceCode, codeBlockInfo->sourceCodeSize); 
        codeBlock->m_src = *(new StringView(src, 0, src->length()));

        char* code = bytecode;
        char* end = bytecode + codeBlockInfo->byteCodeSize;

        while (code < end) {
            ByteCode* currentCode = (ByteCode*)code;
#if defined(COMPILER_GCC)
            Opcode opcode = (Opcode)(size_t)currentCode->m_opcodeInAddress;
#else
            Opcode opcode = currentCode->m_opcode;
#endif
            switch (opcode) {
            case GetGlobalObjectOpcode: {
                size_t index = ((GetGlobalObject*)currentCode)->m_propertyName.m_data;

                ((GetGlobalObject*)currentCode)->m_propertyName.m_data = (size_t) userLiterals[index];
                break;
            }
            case SetGlobalObjectOpcode: {
                size_t index = ((SetGlobalObject*)currentCode)->m_propertyName.m_data;

                ((SetGlobalObject*)currentCode)->m_propertyName.m_data = (size_t) userLiterals[index];
                break;
            }
            case LoadLiteralOpcode: {
                Value value = ((LoadLiteral*)currentCode)->m_value;

                if (value.isSnapshotIndex()) {
                    int32_t index = value.asSnapshotIndex();

                    ((LoadLiteral*)currentCode)->m_value = Value(userLiterals[index]);
                }
                break;
            }
            case DeclareFunctionDeclarationsOpcode: {
                size_t index = (size_t)((DeclareFunctionDeclarations*)currentCode)->m_codeBlock;

                ((DeclareFunctionDeclarations*)currentCode)->m_codeBlock = codeBlocks[index];
                break;
            }
            case LoadByNameOpcode: {
                size_t index = (size_t)(((LoadByName*)currentCode)->m_name.m_string);

                ((LoadByName*)currentCode)->m_name.m_string = userLiterals[index];
                break;
            }
            case StoreByNameOpcode: {
                size_t index = (size_t)(((StoreByName*)currentCode)->m_name.m_string);

                ((StoreByName*)currentCode)->m_name.m_string = userLiterals[index];
                break;
            }
            case ObjectDefineOwnPropertyWithNameOperationOpcode: {
                size_t index = (size_t)(((ObjectDefineOwnPropertyWithNameOperation*)currentCode)->m_propertyName.m_string);

                ((ObjectDefineOwnPropertyWithNameOperation*)currentCode)->m_propertyName.m_string = userLiterals[index];
                break;
            }
            default: {
                break;
            }
            }

            code += byteCodeLengths[opcode];
        }

        // Replace with memcpy.
        for (size_t j = 0; j < codeBlockInfo->byteCodeSize; j++) {
            codeBlock->m_byteCodeBlock->m_code.push_back(bytecode[j]);
        }
    }

    ExecutionState state(context);
    LexicalEnvironment* env = new LexicalEnvironment(new GlobalEnvironmentRecord(state, codeBlocks[0], context->globalObject(), false, true), nullptr);

    ExecutionContext ec(context, nullptr, env, codeBlocks[0]->isStrict());
    Value thisValue(context->globalObject());
    ExecutionState newState(&state, &ec);

    size_t literalStorageSize = codeBlocks[0]->byteCodeBlock()->m_numeralLiteralData.size();
    Value* registerFile = (Value*)alloca((codeBlocks[0]->byteCodeBlock()->m_requiredRegisterFileSizeInValueSize + 1 + literalStorageSize) * sizeof(Value));
    registerFile[0] = Value();
    Value* stackStorage = registerFile + codeBlocks[0]->byteCodeBlock()->m_requiredRegisterFileSizeInValueSize;
    stackStorage[0] = thisValue;
    Value* literalStorage = stackStorage + 1;
    Value* src = codeBlocks[0]->byteCodeBlock()->m_numeralLiteralData.data();
    for (size_t i = 0; i < literalStorageSize; i++) {
        literalStorage[i] = src[i];
    }

    Value resultValue = ByteCodeInterpreter::interpret(newState, codeBlocks[0]->byteCodeBlock(), 0, registerFile);
    clearStack<512>();
}

} /* Escargot */
