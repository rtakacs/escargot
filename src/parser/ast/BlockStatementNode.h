/*
 * Copyright (c) 2016-present Samsung Electronics Co., Ltd
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

#ifndef BlockStatementNode_h
#define BlockStatementNode_h

#include "StatementNode.h"

namespace Escargot {

// A block statement, i.e., a sequence of statements surrounded by braces.
class BlockStatementNode : public StatementNode {
public:
    friend class ScriptParser;
    explicit BlockStatementNode(StatementContainer* body, StatementContainer* argumentInitializers = nullptr)
        : StatementNode()
        , m_container(body)
        , m_argumentInitializers(argumentInitializers)
    {
    }

    virtual ~BlockStatementNode()
    {
    }
    virtual ASTNodeType type() { return ASTNodeType::BlockStatement; }
    virtual void generateStatementByteCode(ByteCodeBlock* codeBlock, ByteCodeGenerateContext* context)
    {
        if (m_argumentInitializers != nullptr) {
            m_argumentInitializers->generateStatementByteCode(codeBlock, context);
        }
        m_container->generateStatementByteCode(codeBlock, context);
    }

    StatementNode* firstChild()
    {
        return m_container->firstChild();
    }

private:
    RefPtr<StatementContainer> m_container;
    RefPtr<StatementContainer> m_argumentInitializers;
};
}

#endif
