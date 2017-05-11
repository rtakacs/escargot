/*
 * Copyright (c) 2016-present Samsung Electronics Co., Ltd
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifndef BinaryExpressionDivisionNode_h
#define BinaryExpressionDivisionNode_h

#include "ExpressionNode.h"

namespace Escargot {

class BinaryExpressionDivisionNode : public ExpressionNode {
public:
    friend class ScriptParser;

    BinaryExpressionDivisionNode(Node* left, Node* right)
        : ExpressionNode()
    {
        m_left = (ExpressionNode*)left;
        m_right = (ExpressionNode*)right;
    }

    virtual ~BinaryExpressionDivisionNode()
    {
        delete m_left;
        delete m_right;
    }
    virtual ASTNodeType type() { return ASTNodeType::BinaryExpressionDivison; }
    virtual void generateExpressionByteCode(ByteCodeBlock* codeBlock, ByteCodeGenerateContext* context, ByteCodeRegisterIndex dstRegister)
    {
        size_t src0 = m_left->getRegister(codeBlock, context);
        size_t src1 = m_right->getRegister(codeBlock, context);
        m_left->generateExpressionByteCode(codeBlock, context, src0);
        m_right->generateExpressionByteCode(codeBlock, context, src1);

        context->giveUpRegister();
        context->giveUpRegister();

        codeBlock->pushCode(BinaryDivision(ByteCodeLOC(m_loc.index), src0, src1, dstRegister), context, this);
    }

    virtual void iterateChildrenIdentifier(const std::function<void(AtomicString name)>& fn)
    {
        m_left->iterateChildrenIdentifier(fn);
        m_right->iterateChildrenIdentifier(fn);
    }

protected:
    ExpressionNode* m_left;
    ExpressionNode* m_right;
};
}

#endif
