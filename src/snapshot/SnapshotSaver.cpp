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

namespace Escargot {

static std::unordered_set<String*, std::hash<String*>, std::equal_to<String*>> userLiteralStorage;
static std::vector<InterpretedCodeBlock*> codeBlocksStorage;

// Save the given ByteCodeBlock recursively.
void SaveBytecode(const char*);

void collectUserLiterals(ByteCodeBlock* codeblock)
{
    ByteCodeLiteralData literals = codeblock->m_literalData;
    AtomicStringVector usingNames = codeblock->m_codeBlock->usingNames();

    for (size_t i = 0; i < literals.size(); i++) {
        userLiteralStorage.insert((String*)literals[i]);
    }

    for (size_t i = 0; i < usingNames.size(); i++) {
        userLiteralStorage.insert(usingNames[i].string());
    }
}

void Snapshot::createByteCodeBlock(InterpretedCodeBlock* block)
{
    ASSERT(block != nullptr);

    volatile int sp;
    ExecutionState state(block->context());
    size_t currentStackBase = (size_t)&sp;
#ifdef STACK_GROWS_DOWN
    size_t stackRemainApprox = STACK_LIMIT_FROM_BASE - (state.stackBase() - currentStackBase);
#else
    size_t stackRemainApprox = STACK_LIMIT_FROM_BASE - (currentStackBase - state.stackBase());
#endif

    auto ret = state.context()->scriptParser().parseFunction(block, stackRemainApprox, &state);
    RefPtr<Node> ast = std::get<0>(ret);

    ByteCodeGenerator g;
    block->m_byteCodeBlock = g.generateByteCode(state.context(), block, ast.get(), std::get<1>(ret), false, false, false);
}

void Snapshot::walkOnCodeBlockTree(InterpretedCodeBlock* block) {
    ASSERT(block != nullptr);
    // Save all the CodeBlock node to count
    // and process them later.
    codeBlocksStorage.push_back(block);

    // Create ByteCodeBlock if necessary.
    if (block->byteCodeBlock() == nullptr) {
        createByteCodeBlock(block);
    }

    // Get all the user defined literals that are
    // reachable in the corresponding ByteCodeBlock.
    collectUserLiterals(block->byteCodeBlock());

    // Process the child blocks recursively.
    CodeBlockVector childBlocks = block->childBlocks();

    for (size_t i = 0; i < childBlocks.size(); i++) {
        walkOnCodeBlockTree(childBlocks[i]);
    }
}

void Snapshot::generate(Context* context, String* filename, String* source) {
    // Parse the program and get the AST.
    ScriptParser parser = context->scriptParser();
    ScriptParser::ScriptParserResult parserResult = parser.parse(source, filename);

    if (UNLIKELY(!!parserResult.m_error)) {
        printf("Error: %s\n", parserResult.m_error->message->toUTF8StringData().data());
        exit(1);
    }

    InterpretedCodeBlock* globalCodeBlock = parserResult.m_script->topCodeBlock();
    ProgramNode* programNode = (ProgramNode*)globalCodeBlock->cachedASTNode();
    ASTScopeContext* scopeContext = programNode->scopeContext();

    ByteCodeGenerator g;
    globalCodeBlock->m_byteCodeBlock = g.generateByteCode(context, globalCodeBlock, programNode, scopeContext, false, true);

    // Collect all the CodeBlocks and all the user
    // defined literals recursively.
    walkOnCodeBlockTree(globalCodeBlock);

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
        .userLiteralCount = (uint32_t) userLiteralStorage.size()
    };

    WRITE_INTO_FILE(&globalInfo, sizeof(GlobalInfo), output, "Global info");
    WRITE_INTO_FILE(filename->toUTF8StringData().data(), globalInfo.filenameSize, output, "Filename info");
    WRITE_INTO_FILE(source->toUTF8StringData().data(), globalInfo.sourceCodeSize, output, "Source code info");

    // User defined literals.
    for (auto it = userLiteralStorage.begin(); it != userLiteralStorage.end(); it++) {
        String* string = *it;

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
            auto literalIter = userLiteralStorage.find(cb->m_functionName.string());
            auto codeBlockIter = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), cb->m_parentCodeBlock);

            ASSERT(literalIter != userLiteralStorage.end());
            ASSERT(codeBlockIter != codeBlocksStorage.end());

            codeBlockInfo.functionNameIdx = std::distance(userLiteralStorage.begin(), literalIter);
            codeBlockInfo.parentBlock = std::distance(codeBlocksStorage.begin(), codeBlockIter);
        }

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

            auto it = std::find(userLiteralStorage.begin(), userLiteralStorage.end(), m_parameterInfo[j].m_name.string());
            ASSERT(it != userLiteralStorage.end());

            pinfo.parameterNameIdx = std::distance(userLiteralStorage.begin(), it);
            pinfo.isHeapAllocated = m_parameterInfo[j].m_isHeapAllocated;
            pinfo.isDuplicated = m_parameterInfo[j].m_isDuplicated;
            pinfo.index = m_parameterInfo[j].m_index;

            WRITE_INTO_FILE(&pinfo, sizeof(ParameterInfo), output, "Parameter info");
        }

        InterpretedCodeBlock::IdentifierInfoVector m_identifierInfos = cb->m_identifierInfos;
        // Identifier information.
        for (size_t j = 0; j < codeBlockInfo.identifierCount; j++) {
            IdentifierInfo iinfo;

            auto it = std::find(userLiteralStorage.begin(), userLiteralStorage.end(), m_identifierInfos[j].m_name.string());

            ASSERT(it != userLiteralStorage.end());

            iinfo.identifierNameIdx = std::distance(userLiteralStorage.begin(), it);
            iinfo.isExplicitlyDeclaredOrParameterName = m_identifierInfos[j].m_isExplicitlyDeclaredOrParameterName;
            iinfo.indexForIndexedStorage = m_identifierInfos[j].m_indexForIndexedStorage;
            iinfo.needToAllocateOnStack = m_identifierInfos[j].m_needToAllocateOnStack;
            iinfo.isMutable = m_identifierInfos[j].m_isMutable;

            WRITE_INTO_FILE(&iinfo, sizeof(IdentifierInfo), output, "Identifier info");
        }

        // Numeral literal information.
        for (size_t j = 0; j < codeBlockInfo.numeralValueCount; j++) {
            Value value = cb->byteCodeBlock()->m_numeralLiteralData[j];

            WRITE_INTO_FILE(&value, sizeof(Value), output, "Numeral value info");
        }

        // ByteCodeBlock literla info.0
        for (size_t j = 0; j < codeBlockInfo.literalCount; j++) {
            auto it = userLiteralStorage.find((String*)cb->byteCodeBlock()->m_literalData[j]);
            size_t index = std::distance(userLiteralStorage.begin(), it);

            WRITE_INTO_FILE(&index, sizeof(size_t), output, "ByteCodeBlock literal info");
        }

        // ObjectCodePosition information.
        for (size_t j = 0; j < codeBlockInfo.objectCodePositionsSize; j++) {
            size_t position = cb->byteCodeBlock()->m_getObjectCodePositions[j];

            WRITE_INTO_FILE(&position, sizeof(size_t), output, "Object code position info");
        }

        // Bytecode and sourcecode information.
        char* sourcecode = cb->src().toUTF8StringData().data();
        char* bytecode = cb->byteCodeBlock()->m_code.data();

        WRITE_INTO_FILE(sourcecode, codeBlockInfo.sourceCodeSize, output, "Block source code data");

        // Take care on ByteCode pointers.
        char* code = bytecode;
        char* end = bytecode + codeBlockInfo.byteCodeSize;

        while (code < end) {
            ByteCode* currentCode = (ByteCode*)code;
#if defined(COMPILER_GCC)
            Opcode opcode = (Opcode)(size_t)currentCode->m_opcodeInAddress;
#else
            Opcode opcode = currentCode->m_opcode;
#endif
            switch (opcode) {
            case GetGlobalObjectOpcode: {
                String* propertyName = ((GetGlobalObject*)currentCode)->m_propertyName.plainString();

                auto it = userLiteralStorage.find(propertyName);
                size_t index = std::distance(userLiteralStorage.begin(), it);
                
                ((GetGlobalObject*)currentCode)->m_propertyName.m_data = index;
                break;
            }
            case SetGlobalObjectOpcode: {
                String* propertyName = ((SetGlobalObject*)currentCode)->m_propertyName.plainString();

                auto it = userLiteralStorage.find(propertyName);
                size_t index = std::distance(userLiteralStorage.begin(), it);

                ((SetGlobalObject*)currentCode)->m_propertyName.m_data = index;
                break;
            }
            case LoadLiteralOpcode: {
                Value value = ((LoadLiteral*)currentCode)->m_value;

                if (value.isString()) {
                    String* string = value.asString();

                    auto it = userLiteralStorage.find(string);
                    int32_t index = (int32_t) std::distance(userLiteralStorage.begin(), it);

                    ((LoadLiteral*)currentCode)->m_value = Value(Value::SnapshotIndex, index);;
                }
                break;
            }
            case DeclareFunctionDeclarationsOpcode: {
                InterpretedCodeBlock* ib = ((DeclareFunctionDeclarations*)currentCode)->m_codeBlock;

                auto it = std::find(codeBlocksStorage.begin(), codeBlocksStorage.end(), ib);

                ASSERT(it != codeBlocksStorage.end());

                size_t index = std::distance(codeBlocksStorage.begin(), it);

                ((DeclareFunctionDeclarations*)currentCode)->m_codeBlock = (InterpretedCodeBlock*) index;
                break;
            }
            case LoadByNameOpcode: {
                String* string = ((LoadByName*)currentCode)->m_name.string();

                auto it = userLiteralStorage.find(string);
                size_t index = std::distance(userLiteralStorage.begin(), it);

                ((LoadByName*)currentCode)->m_name.m_string = (String*) index;
                break;
            }
            case StoreByNameOpcode: {
                String* string = ((StoreByName*)currentCode)->m_name.string();

                auto it = userLiteralStorage.find(string);
                size_t index = std::distance(userLiteralStorage.begin(), it);

                ((StoreByName*)currentCode)->m_name.m_string = (String*) index;
                break;
            }
            case ObjectDefineOwnPropertyWithNameOperationOpcode: {
                String* string = ((ObjectDefineOwnPropertyWithNameOperation*)currentCode)->m_propertyName.string();

                auto it = userLiteralStorage.find(string);
                size_t index = std::distance(userLiteralStorage.begin(), it);

                ((ObjectDefineOwnPropertyWithNameOperation*)currentCode)->m_propertyName.m_string = (String*) index;
                break;
            }
            default: {
                break;
            }
            }

            WRITE_INTO_FILE(code, byteCodeLengths[opcode], output, "Bytecode opcode data");
            code += byteCodeLengths[opcode];
        }
    }

    fclose(output);
    return;
}

} /* Escargot */
