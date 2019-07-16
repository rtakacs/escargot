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

#define DEFINE_VARIABLE(type, name) \
    type name = (type) currentCode

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
        codeBlock->m_identifierOnStackCount = codeBlockInfo->m_identifierOnStackCount;
        codeBlock->m_identifierOnHeapCount = codeBlockInfo->m_identifierOnHeapCount;
        codeBlock->m_lexicalBlockIndex = codeBlockInfo->m_lexicalBlockIndex;

        if (UNLIKELY(codeBlockInfo->parentBlock == std::numeric_limits<uint32_t>::max())) {
            // Global object doesn't have parent nor function name.
            codeBlock->m_parentCodeBlock = nullptr;
        } else {
            String* str = (codeBlockInfo->functionNameIdx == 10000) ? String::emptyString : userLiterals[codeBlockInfo->functionNameIdx];

            AtomicString functionName(context, str);
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

        for (size_t j = 0; j < codeBlockInfo->objectCodePositionsSize; j++) {
            READ_INTO_VARIABLE(size_t*, position, sizeof(size_t), "Object code position info");

            codeBlock->byteCodeBlock()->m_getObjectCodePositions.push_back(*position);
        }

        READ_INTO_VARIABLE(char*, blockSourceCode, codeBlockInfo->sourceCodeSize, "Block source code info");
        READ_INTO_VARIABLE(char*, bytecode, codeBlockInfo->byteCodeSize, "Bytecode info");

        String* src = String::fromUTF8(blockSourceCode, codeBlockInfo->sourceCodeSize); 
        codeBlock->m_src = *(new StringView(src, 0, src->length()));

        char* code = bytecode;
        size_t codeBase = (size_t) code;
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
                DEFINE_VARIABLE(GetGlobalObject*, getGlobalObject);

                size_t index = getGlobalObject->m_propertyName.m_data;
                getGlobalObject->m_propertyName.m_data = (size_t) userLiterals[index];

                break;
            }
            case SetGlobalObjectOpcode: {
                DEFINE_VARIABLE(SetGlobalObject*, setGlobalObject);

                size_t index = setGlobalObject->m_propertyName.m_data;
                setGlobalObject->m_propertyName.m_data = (size_t) userLiterals[index];

                break;
            }
            case LoadLiteralOpcode: {
                DEFINE_VARIABLE(LoadLiteral*, loadLiteral);

                Value value = loadLiteral->m_value;

                if (value.isSnapshotIndex()) {
                    loadLiteral->m_value = Value(userLiterals[value.asSnapshotIndex()]);
                }

                if (loadLiteral->m_value.isPointerValue()) {
                    codeBlock->byteCodeBlock()->m_literalData.pushBack(loadLiteral->m_value.asPointerValue());
                }

                break;
            }
            case DeclareFunctionDeclarationsOpcode: {
                DEFINE_VARIABLE(DeclareFunctionDeclarations*, declareFunctionDeclarations);

                size_t index = (size_t)(declareFunctionDeclarations->m_codeBlock);
                declareFunctionDeclarations->m_codeBlock = codeBlocks[index];

                break;
            }
            case LoadByNameOpcode: {
                DEFINE_VARIABLE(LoadByName*, loadByName);

                size_t index = (size_t)(loadByName->m_name.m_string);
                loadByName->m_name.m_string = userLiterals[index];

                break;
            }
            case StoreByNameOpcode: {
                DEFINE_VARIABLE(StoreByName*, storeByName);

                size_t index = (size_t)(storeByName->m_name.m_string);
                storeByName->m_name.m_string = userLiterals[index];

                break;
            }
            case ObjectDefineOwnPropertyWithNameOperationOpcode: {
                DEFINE_VARIABLE(ObjectDefineOwnPropertyWithNameOperation*, objectDefineOwnPropertyWithNameOperation);

                /* Todo: hiba... */
                size_t index = (size_t)(objectDefineOwnPropertyWithNameOperation->m_propertyName.m_string);
                objectDefineOwnPropertyWithNameOperation->m_propertyName.m_string = userLiterals[index];

                break;
            }
            case LoadRegexpOpcode: {
                DEFINE_VARIABLE(LoadRegexp*, loadRegexp);

                size_t bodyIndex = (size_t)(loadRegexp->m_body);
                size_t optionIndex = (size_t)(loadRegexp->m_option);

                loadRegexp->m_body = userLiterals[bodyIndex];
                loadRegexp->m_option = userLiterals[optionIndex];

                // Add extra references.
                codeBlock->byteCodeBlock()->m_literalData.push_back((void*)userLiterals[bodyIndex]);
                codeBlock->byteCodeBlock()->m_literalData.push_back((void*)userLiterals[optionIndex]);

                break;
            }
            case GetObjectPreComputedCaseOpcode: {
                DEFINE_VARIABLE(GetObjectPreComputedCase*, getObjectPreComputedCase);

                size_t index = getObjectPreComputedCase->m_propertyName.m_data;
                getObjectPreComputedCase->m_propertyName.m_data = (size_t) userLiterals[index];

                break;
            }
            case JumpOpcode: {
                DEFINE_VARIABLE(Jump*, jump);
                
                jump->m_jumpPosition = jump->m_jumpPosition + codeBase;

                break;
            }
            case JumpIfTrueOpcode: {
                DEFINE_VARIABLE(JumpIfTrue*, jumpIfTrue);

                jumpIfTrue->m_jumpPosition = jumpIfTrue->m_jumpPosition + codeBase;

                break;
            }
            case JumpIfFalseOpcode: {
                DEFINE_VARIABLE(JumpIfFalse*, jumpIfFalse);

                jumpIfFalse->m_jumpPosition = jumpIfFalse->m_jumpPosition + codeBase;

                break;
            }
            case JumpIfRelationOpcode: {
                DEFINE_VARIABLE(JumpIfRelation*, jumpIfRelation);

                jumpIfRelation->m_jumpPosition = jumpIfRelation->m_jumpPosition + codeBase;

                break;
            }
            case JumpIfEqualOpcode: {
                DEFINE_VARIABLE(JumpIfEqual*, jumpIfEqual);

                jumpIfEqual->m_jumpPosition = jumpIfEqual->m_jumpPosition + codeBase;

                break;
            }
            case SetObjectPreComputedCaseOpcode: {
                DEFINE_VARIABLE(SetObjectPreComputedCase*, setObjectPreComputedCase);

                size_t index = setObjectPreComputedCase->m_propertyName.m_data;
                setObjectPreComputedCase->m_propertyName.m_data = (size_t) userLiterals[index];
                setObjectPreComputedCase->m_inlineCache = new SetObjectInlineCache();

                // Add extra reference.
                codeBlock->byteCodeBlock()->m_literalData.pushBack((void*)(setObjectPreComputedCase->m_inlineCache));

                break;
            }
            case CreateClassOpcode: {
                DEFINE_VARIABLE(CreateClass*, createClass);

                size_t index = (size_t)(createClass->m_name.m_string);

                if (index == 10000) {
                    createClass->m_name.m_string = String::emptyString;
                } else {
                    createClass->m_name.m_string = userLiterals[index];
                }

                if (createClass->m_stage == 2) {
                    size_t index = (size_t)(createClass->m_codeBlock);
                    createClass->m_codeBlock = codeBlocks[index];
                }

                break;
            }
            case CreateFunctionOpcode: {
                DEFINE_VARIABLE(CreateFunction*, createFunction);

                size_t index = (size_t)(createFunction->m_codeBlock);
                createFunction->m_codeBlock = codeBlocks[index];

                break;
            }
            default: {
                break;
            }
            }

            code += byteCodeLengths[opcode];
            currentCode->assignOpcodeInAddress();
        }

        // Replace with memcpy.
        for (size_t j = 0; j < codeBlockInfo->byteCodeSize; j++) {
            codeBlock->m_byteCodeBlock->m_code.push_back(bytecode[j]);
        }
    }

    ExecutionState state(context);
    LexicalEnvironment* env = new LexicalEnvironment(new GlobalEnvironmentRecord(state, codeBlocks[0], context->globalObject(), false, true), nullptr);

    Value thisValue(context->globalObject());
    ExecutionState newState(context);

    newState.setLexicalEnvironment(env, codeBlocks[0]->isStrict());

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
