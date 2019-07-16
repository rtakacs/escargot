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
#include "parser/ScriptParser.h"
#include "interpreter/ByteCodeGenerator.h"
#include "interpreter/ByteCode.h"
#include "parser/ast/ProgramNode.h"
#include "parser/CodeBlock.h"
#include "util/Util.h"
#include "runtime/Value.h"
#include <limits>
#include <algorithm>

#define WRITE_INTO_FILE(data, size, file, msg) \
    fwrite((char*)data, size, 1, file); \
    if (SNAPSHOT_VERBOSE) \
        printf("%s is dumped (%zu bytes)\n", msg, (size_t) size);

#define DEFINE_VARIABLE(type, name) \
    type name = (type) currentCode

namespace Escargot {

static std::unordered_map<String*, size_t> literalMap;
static std::vector<String*> literalStorage;
static std::vector<InterpretedCodeBlock*> codeBlocksStorage;

// Save the given ByteCodeBlock recursively.
void SaveBytecode(const char*);

size_t getIndexByLiteral(String* string)
{
    auto it = literalMap.find(string);

    if (it == literalMap.end()) {
        size_t index = literalStorage.size();

        literalMap[string] = index;
        literalStorage.push_back(string);

        return index;
    }

    return it->second;
}

void Snapshot::postProcess(InterpretedCodeBlock* block)
{
    ASSERT(block != nullptr);

    InterpretedCodeBlock::IdentifierInfoVector m_identifierInfos = block->m_identifierInfos;
    // Identifier information.
    for (size_t j = 0; j < m_identifierInfos.size(); j++) {
        IdentifierInfo iinfo;
        String* identifierName = m_identifierInfos[j].m_name.string();

        getIndexByLiteral(identifierName);
    }

    InterpretedCodeBlock::FunctionParametersInfoVector m_parameterInfo = block->m_parametersInfomation;

    for (size_t j = 0; j < block->m_parameterCount; j++) {
        ParameterInfo pinfo;
        String* parameterName = m_parameterInfo[j].m_name.string();

        getIndexByLiteral(parameterName);
    }

    String* functionName = block->m_functionName.string();

    if (functionName) {
        getIndexByLiteral(functionName);
    }

    size_t codesize = block->byteCodeBlock()->m_code.size();
    char* bytecode = block->byteCodeBlock()->m_code.data();

    char* pos = bytecode;
    char* end = bytecode + codesize;

    while (pos < end) {
        ByteCode* currentCode = (ByteCode*)pos;
#if defined(COMPILER_GCC)
        Opcode opcode = (Opcode)(size_t)currentCode->m_opcodeInAddress;
#else
        Opcode opcode = currentCode->m_opcode;
#endif
        switch (opcode) {
        case GetGlobalObjectOpcode: {
            DEFINE_VARIABLE(GetGlobalObject*, getGlobalObject);

            getGlobalObject->m_propertyName.m_data = getIndexByLiteral(getGlobalObject->m_propertyName.plainString());

            break;
        }
        case SetGlobalObjectOpcode: {
            DEFINE_VARIABLE(SetGlobalObject*, setGlobalObject);

            setGlobalObject->m_propertyName.m_data = getIndexByLiteral(setGlobalObject->m_propertyName.plainString());

            break;
        }
        case LoadLiteralOpcode: {
            DEFINE_VARIABLE(LoadLiteral*, loadLiteral);

            Value value = loadLiteral->m_value;

            if (value.isString()) {
                loadLiteral->m_value = Value(Value::SnapshotIndex, getIndexByLiteral(value.asString()));
            }

            break;
        }
        case DeclareFunctionDeclarationsOpcode: {
            DEFINE_VARIABLE(DeclareFunctionDeclarations*, declareFunctionDeclarations);

            InterpretedCodeBlock* block = declareFunctionDeclarations->m_codeBlock;

            auto it = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), block);

            ASSERT(it != codeBlocksStorage.end());

            size_t index = std::distance(codeBlocksStorage.begin(), it);

            declareFunctionDeclarations->m_codeBlock = (InterpretedCodeBlock*) index;

            break;
        }
        case LoadByNameOpcode: {
            DEFINE_VARIABLE(LoadByName*, loadByName);

            loadByName->m_name.m_string = (String*) getIndexByLiteral(loadByName->m_name.string());

            break;
        }
        case StoreByNameOpcode: {
            DEFINE_VARIABLE(StoreByName*, storeByName);

            storeByName->m_name.m_string = (String*) getIndexByLiteral(storeByName->m_name.string());

            break;
        }
        case LoadRegexpOpcode : {
            DEFINE_VARIABLE(LoadRegexp*, loadRegexp);

            loadRegexp->m_body = (String*) getIndexByLiteral(loadRegexp->m_body);
            loadRegexp->m_option = (String*) getIndexByLiteral(loadRegexp->m_option);

            break;
        }
        case CreateClassOpcode: {
            DEFINE_VARIABLE(CreateClass*, createClass);

            if (createClass->m_name.string() == String::emptyString) {
                createClass->m_name.m_string = (String*) 10000;
            } else {
                createClass->m_name.m_string = (String*) getIndexByLiteral(createClass->m_name.string());
            }

            if (createClass->m_stage == 2) {
                auto codeBlockIter = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), createClass->m_codeBlock);

                ASSERT(codeBlockIter != codeBlocksStorage.end());

                size_t index = std::distance(codeBlocksStorage.begin(), codeBlockIter);

                createClass->m_codeBlock = (CodeBlock*) index;
            }

            break;
        }
        case CreateFunctionOpcode: {
            DEFINE_VARIABLE(CreateFunction*, createFunction);

            auto codeBlockIter = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), createFunction->m_codeBlock);

            ASSERT(codeBlockIter != codeBlocksStorage.end());

            size_t index = std::distance(codeBlocksStorage.begin(), codeBlockIter);

            createFunction->m_codeBlock = (CodeBlock*) index;

            break;
        }
        case UnaryTypeofOpcode: {
            DEFINE_VARIABLE(UnaryTypeof*, unaryTypeof);

            unaryTypeof->m_id.m_string = (String*) getIndexByLiteral(unaryTypeof->m_id.string());

            break;
        }
        case UnaryDeleteOpcode: {
            DEFINE_VARIABLE(UnaryTypeof*, unaryDelete);

            unaryDelete->m_id.m_string = (String*) getIndexByLiteral(unaryDelete->m_id.string());

            break;
        }
        case CallFunctionInWithScopeOpcode: {
            DEFINE_VARIABLE(CallFunctionInWithScope*, callFunctionInWithScope);

            callFunctionInWithScope->m_calleeName.m_string = (String*) getIndexByLiteral(callFunctionInWithScope->m_calleeName.string());

            break;
        }
        case TryOperationOpcode: {
            DEFINE_VARIABLE(TryOperation*, tryOperation);

            tryOperation->m_catchVariableName.m_string = (String*) getIndexByLiteral(tryOperation->m_catchVariableName.string());

            break;
        }
        case GetObjectPreComputedCaseOpcode: {
            DEFINE_VARIABLE(GetObjectPreComputedCase*, getObjectPreComputedCase);

            getObjectPreComputedCase->m_propertyName.m_data = getIndexByLiteral(getObjectPreComputedCase->m_propertyName.plainString());

            break;
        }
        case SetObjectPreComputedCaseOpcode: {
            DEFINE_VARIABLE(SetObjectPreComputedCase*, setObjectPreComputedCase);

            setObjectPreComputedCase->m_propertyName.m_data = getIndexByLiteral(setObjectPreComputedCase->m_propertyName.plainString());

            break;
        }
        case ObjectDefineOwnPropertyWithNameOperationOpcode: {
            DEFINE_VARIABLE(ObjectDefineOwnPropertyWithNameOperation*, objectDefineOwnPropertyWithNameOperation);

            size_t index = getIndexByLiteral(objectDefineOwnPropertyWithNameOperation->m_propertyName.string());
            objectDefineOwnPropertyWithNameOperation->m_propertyName.m_string = (String*) index;

            break;
        }
        default: {
            break;
        }
        }

        pos += byteCodeLengths[opcode];
    }
}

void Snapshot::createByteCodeBlock(InterpretedCodeBlock* block)
{
    ASSERT(block != nullptr);

    // Don't generate if it already generated.
    if (block->byteCodeBlock())
        return;

    volatile int sp;
    ExecutionState state(block->context());
    size_t currentStackBase = (size_t)&sp;
#ifdef STACK_GROWS_DOWN
    size_t stackRemainApprox = STACK_LIMIT_FROM_BASE - (state.stackBase() - currentStackBase);
#else
    size_t stackRemainApprox = STACK_LIMIT_FROM_BASE - (currentStackBase - state.stackBase());
#endif

    state.context()->scriptParser().generateFunctionByteCode(state, block, stackRemainApprox);
}

void Snapshot::walkOnCodeBlockTree(InterpretedCodeBlock* block) {
    ASSERT(block != nullptr);
    // Save all the CodeBlock node to count
    // and process them later.
    printf("save codeblock: %p\n", block);
    codeBlocksStorage.push_back(block);

    createByteCodeBlock(block);
    // Process the child blocks recursively.
    CodeBlockVector childBlocks = block->childBlocks();

    for (size_t i = 0; i < childBlocks.size(); i++) {
        walkOnCodeBlockTree(childBlocks[i]);
    }

    // Replace pointers with simple indexes in the bytecode stream.
    postProcess(block);
}

void Snapshot::generate(Context* context, String* filename, String* source) {
    ExecutionState state(context);

    Escargot::Script* script = context->scriptParser().initializeScript(state, source, filename);
    // Collect all the CodeBlocks and all the user defined literals recursively.
    walkOnCodeBlockTree(script->m_topCodeBlock);

    createSnapshot(filename, source);
}

void Snapshot::createSnapshot(String* filename, String* source)
{
    FILE *output = fopen(SNAPSHOT_FILENAME, "wb");

    if (output == nullptr) {
        fprintf(stderr, "Cannot open file.\n");
        exit(1);
    }

    SnapshotInfo snapshotInfo {
        .magic = SNAPSHOT_MAGIC,
        .version = SNAPSHOT_VERSION,
    };

    WRITE_INTO_FILE(&snapshotInfo, sizeof(SnapshotInfo), output, "Snapshot info");

    GlobalInfo globalInfo {
        .filenameSize = (uint32_t) filename->length(),
        .sourceCodeSize = (uint32_t) source->length(),
        .codeblockCount = (uint32_t) codeBlocksStorage.size(),
        .userLiteralCount = (uint32_t) literalStorage.size()
    };

    WRITE_INTO_FILE(&globalInfo, sizeof(GlobalInfo), output, "Global info");
    WRITE_INTO_FILE(filename->toUTF8StringData().data(), globalInfo.filenameSize, output, "Filename info");
    WRITE_INTO_FILE(source->toUTF8StringData().data(), globalInfo.sourceCodeSize, output, "Source code info");

    // User defined literals.
    for (auto it = literalStorage.begin(); it != literalStorage.end(); it++) {
        String* string = *it;

        if (string == nullptr)
            continue;

        size_t strsize = string->length();
        char* strdata = string->toUTF8StringData().data();

        WRITE_INTO_FILE(&strsize, sizeof(size_t), output, "User literal size");
        WRITE_INTO_FILE(strdata, strsize, output, "User literal data");
    }

    for (size_t i = 0; i < codeBlocksStorage.size(); i++) {
        InterpretedCodeBlock* cb = codeBlocksStorage[i];

        CodeBlockInfo codeBlockInfo;
        // General infromation.
        codeBlockInfo.parameterCount = cb->m_parameterCount;
        codeBlockInfo.numeralValueCount = cb->byteCodeBlock()->m_numeralLiteralData.size();
        codeBlockInfo.literalCount = cb->byteCodeBlock()->m_literalData.size();
        codeBlockInfo.requiredRegisterCount = cb->byteCodeBlock()->m_requiredRegisterFileSizeInValueSize;
        codeBlockInfo.identifierCount = cb->m_identifierInfos.size();
        codeBlockInfo.childBlockCount = cb->m_childBlocks.size();
        codeBlockInfo.byteCodeSize = cb->byteCodeBlock()->m_code.size();
        codeBlockInfo.sourceCodeSize = cb->src().length();
        codeBlockInfo.objectCodePositionsSize = cb->byteCodeBlock()->m_getObjectCodePositions.size();

        if (cb->isGlobalScopeCodeBlock()) {
            codeBlockInfo.functionNameIdx = std::numeric_limits<uint32_t>::max();
            codeBlockInfo.parentBlock = std::numeric_limits<uint32_t>::max();
        } else {
            String* functionName = cb->m_functionName.string();

            if (functionName == String::emptyString) {
                codeBlockInfo.functionNameIdx = 10000;
            } else {
                codeBlockInfo.functionNameIdx = getIndexByLiteral(functionName);
            }         

            auto codeBlockIter = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), cb->m_parentCodeBlock);

            ASSERT(codeBlockIter != codeBlocksStorage.end());

            codeBlockInfo.parentBlock = std::distance(codeBlocksStorage.begin(), codeBlockIter);
        }

        codeBlockInfo.m_identifierOnStackCount = cb->m_identifierOnStackCount;
        codeBlockInfo.m_identifierOnHeapCount = cb->m_identifierOnHeapCount;
        codeBlockInfo.m_lexicalBlockIndex = cb->m_lexicalBlockIndex;
        // Scope information
        codeBlockInfo.m_isConstructor = cb->m_isConstructor;
        codeBlockInfo.m_isStrict = cb->m_isStrict;
        codeBlockInfo.m_hasCallNativeFunctionCode = cb->m_hasCallNativeFunctionCode;
        codeBlockInfo.m_isFunctionNameSaveOnHeap = cb->m_isFunctionNameSaveOnHeap;
        codeBlockInfo.m_isFunctionNameExplicitlyDeclared = cb->m_isFunctionNameExplicitlyDeclared;
        codeBlockInfo.m_canUseIndexedVariableStorage = cb->m_canUseIndexedVariableStorage;
        codeBlockInfo.m_canAllocateEnvironmentOnStack = cb->m_canAllocateEnvironmentOnStack;
        codeBlockInfo.m_needsComplexParameterCopy = cb->m_needsComplexParameterCopy;
        codeBlockInfo.m_hasEval = cb->m_hasEval;
        codeBlockInfo.m_hasWith = cb->m_hasWith;
        codeBlockInfo.m_hasSuper = cb->m_hasSuper;
        codeBlockInfo.m_hasCatch = cb->m_hasCatch;
        codeBlockInfo.m_hasYield = cb->m_hasYield;
        codeBlockInfo.m_inCatch = cb->m_inCatch;
        codeBlockInfo.m_inWith = cb->m_inWith;
        codeBlockInfo.m_usesArgumentsObject = cb->m_usesArgumentsObject;
        codeBlockInfo.m_isFunctionExpression = cb->m_isFunctionExpression;
        codeBlockInfo.m_isFunctionDeclaration = cb->m_isFunctionDeclaration;
        codeBlockInfo.m_isFunctionDeclarationWithSpecialBinding = cb->m_isFunctionDeclarationWithSpecialBinding;
        codeBlockInfo.m_isArrowFunctionExpression = cb->m_isArrowFunctionExpression;
        codeBlockInfo.m_isClassConstructor = cb->m_isClassConstructor;
        codeBlockInfo.m_isInWithScope = cb->m_isInWithScope;
        codeBlockInfo.m_isEvalCodeInFunction = cb->m_isEvalCodeInFunction;
        codeBlockInfo.m_needsVirtualIDOperation = cb->m_needsVirtualIDOperation;
        codeBlockInfo.m_needToLoadThisValue = cb->m_needToLoadThisValue;
        codeBlockInfo.m_hasRestElement = cb->m_hasRestElement;

        WRITE_INTO_FILE(&codeBlockInfo, sizeof(CodeBlockInfo), output, "CodeBlock info");

        CodeBlockVector childs = cb->m_childBlocks;
        // ChildBlock informations.
        for (size_t j = 0; j < codeBlockInfo.childBlockCount; j++) {
            InterpretedCodeBlock* child = childs[j];

            auto it = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), child);

            ASSERT(it != codeBlocksStorage.end());

            size_t index = std::distance(codeBlocksStorage.begin(), it);

            WRITE_INTO_FILE(&index, sizeof(size_t), output, "Child block info");
        }

        InterpretedCodeBlock::FunctionParametersInfoVector m_parameterInfo = cb->m_parametersInfomation;
        // Parameter information.
        for (size_t j = 0; j < cb->m_parameterCount; j++) {
            ParameterInfo pinfo;
            String* parameterName = m_parameterInfo[j].m_name.string();

            pinfo.parameterNameIdx = getIndexByLiteral(parameterName);
            pinfo.isHeapAllocated = m_parameterInfo[j].m_isHeapAllocated;
            pinfo.isDuplicated = m_parameterInfo[j].m_isDuplicated;
            pinfo.index = m_parameterInfo[j].m_index;

            WRITE_INTO_FILE(&pinfo, sizeof(ParameterInfo), output, "Parameter info");
        }

        InterpretedCodeBlock::IdentifierInfoVector m_identifierInfos = cb->m_identifierInfos;
        // Identifier information.
        for (size_t j = 0; j < codeBlockInfo.identifierCount; j++) {
            IdentifierInfo iinfo;
            String* identifierName = m_identifierInfos[j].m_name.string();

            iinfo.identifierNameIdx = getIndexByLiteral(identifierName);
            iinfo.isExplicitlyDeclaredOrParameterName = m_identifierInfos[j].m_isExplicitlyDeclaredOrParameterName;
            iinfo.indexForIndexedStorage = m_identifierInfos[j].m_indexForIndexedStorage;
            iinfo.needToAllocateOnStack = m_identifierInfos[j].m_needToAllocateOnStack;
            iinfo.isMutable = m_identifierInfos[j].m_isMutable;

            WRITE_INTO_FILE(&iinfo, sizeof(IdentifierInfo), output, "Identifier info");
        }

        // Numeral literal information.
        for (size_t j = 0; j < codeBlockInfo.numeralValueCount; j++) {
            Value value = cb->byteCodeBlock()->m_numeralLiteralData[j];

            if (value.isString()) {
                printf("nooooo...\n");
            }

            WRITE_INTO_FILE(&value, sizeof(Value), output, "Numeral value info");
        }

        // ObjectCodePosition information.
        for (size_t j = 0; j < codeBlockInfo.objectCodePositionsSize; j++) {
            size_t position = cb->byteCodeBlock()->m_getObjectCodePositions[j];

            WRITE_INTO_FILE(&position, sizeof(size_t), output, "Object code position info");
        }

        // Bytecode and sourcecode information.
        char* sourcecode = cb->src().toUTF8StringData().data();
        char* bytecode = cb->byteCodeBlock()->m_code.data();
        size_t codesize = cb->byteCodeBlock()->m_code.size();

        WRITE_INTO_FILE(sourcecode, codeBlockInfo.sourceCodeSize, output, "Block source code data");
        WRITE_INTO_FILE(bytecode, codesize, output, "Bytecode opcode data");
    }

    fclose(output);
    return;
}

} /* Escargot */
