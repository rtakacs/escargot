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

#ifndef __EscargotSnapshot__
#define __EscargotSnapshot__

#include "Escargot.h"
#include "parser/ast/Node.h"
#include "runtime/AtomicString.h"
#include "runtime/ExecutionState.h"
#include "runtime/String.h"

namespace Escargot {

#define SNAPSHOT_VERBOSE 1
#define SNAPSHOT_MAGIC (0x54435345u)
#define SNAPSHOT_VERSION (1u)
#define SNAPSHOT_FILENAME "snapshot.bin"

struct SnapshotHeader {
    uint32_t magic;
    uint32_t version;
};

struct SnapshotInfo {
    uint32_t magic;
    uint32_t version;
};

struct GlobalInfo {
    uint32_t filenameSize;
    uint32_t sourceCodeSize;
    uint32_t codeblockCount;
    uint32_t userLiteralCount;
};

struct CodeBlockInfo {
    uint32_t functionNameIdx;
    uint32_t parameterCount;
    uint32_t numeralValueCount;
    uint32_t literalCount;
    uint32_t identifierCount;
    uint32_t parentBlock;
    uint32_t childBlockCount;
    uint32_t byteCodeSize;
    uint32_t sourceCodeSize;
    uint32_t objectCodePositionsSize;
    uint32_t m_lexicalBlockIndex;
    uint16_t requiredRegisterCount;
    uint16_t m_identifierOnStackCount;
    uint16_t m_identifierOnHeapCount;
    bool m_isConstructor;
    bool m_isStrict;
    bool m_hasCallNativeFunctionCode;
    bool m_isFunctionNameSaveOnHeap;
    bool m_isFunctionNameExplicitlyDeclared;
    bool m_canUseIndexedVariableStorage;
    bool m_canAllocateEnvironmentOnStack;
    bool m_needsComplexParameterCopy;
    bool m_hasEval;
    bool m_hasWith;
    bool m_hasSuper;
    bool m_hasCatch;
    bool m_hasYield;
    bool m_inCatch;
    bool m_inWith;
    bool m_usesArgumentsObject;
    bool m_isFunctionExpression;
    bool m_isFunctionDeclaration;
    bool m_isFunctionDeclarationWithSpecialBinding;
    bool m_isArrowFunctionExpression;
    bool m_isClassConstructor;
    bool m_isInWithScope;
    bool m_isEvalCodeInFunction;
    bool m_needsVirtualIDOperation;
    bool m_needToLoadThisValue;
    bool m_hasRestElement;
};

struct IdentifierInfo {
    uint32_t identifierNameIdx;
    bool needToAllocateOnStack;
    bool isMutable;
    bool isExplicitlyDeclaredOrParameterName;
    bool indexForIndexedStorage;
};

struct ParameterInfo {
    uint32_t parameterNameIdx;
    bool isHeapAllocated;
    bool isDuplicated;
    bool index;
};

class Snapshot {
public:
    static void generate(Context* context, String* filename, String* source);
    static void execute(Context* context, const char* snapshot);

private:
    static void createSnapshot(String*, String*);
    static void walkOnCodeBlockTree(InterpretedCodeBlock*);
    static void createByteCodeBlock(InterpretedCodeBlock*);
    static void postProcess(InterpretedCodeBlock*);
};

} /* !Escargot */

#endif /* __EscargotSnapshot__ */
