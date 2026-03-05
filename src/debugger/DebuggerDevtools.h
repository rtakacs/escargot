/*
 * Copyright (c) 2026-present Samsung Electronics Co., Ltd
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
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

#ifndef __DebuggerDevtools__
#define __DebuggerDevtools__

#include "Debugger.h"

#ifdef ESCARGOT_DEBUGGER
namespace Escargot {

#ifdef WIN32
#include <winsock2.h>
typedef SOCKET EscargotSocket;
#else /* !WIN32 */
typedef int EscargotSocket;
#endif /* WIN32 */

class DebuggerDevtools : public DebuggerRemote {
public:
    DebuggerDevtools(EscargotSocket socket, String* skipSource)
        : m_socket(socket)
        , m_receiveBuffer{}
        , m_receiveBufferFill(0)
        , m_messageLength(0)
        , m_skipSourceName(skipSource)
    {
    }

    virtual void init(const char* options, Context* context);
    virtual bool skipSourceCode(String* srcName) const override;
    static bool tcpReceive(EscargotSocket socket, uint8_t* message, size_t maxLength, size_t* receivedLength);
    static bool tcpSend(EscargotSocket socket, const uint8_t* message, size_t messageLength);

    virtual void parseCompleted(String* source, String* srcName, size_t originLineOffset, String* error = nullptr) override;
    virtual void stopAtBreakpoint(ByteCodeBlock* byteCodeBlock, uint32_t offset, ExecutionState* state) override;
    virtual void byteCodeReleaseNotification(ByteCodeBlock* byteCodeBlock) override;
    virtual void exceptionCaught(String* message, SavedStackTraceDataVector& exceptionTrace) override;
    virtual void consoleOut(String* output) override;
    virtual String* getClientSource(String** sourceName) override;
    virtual bool getWaitBeforeExitClient() override;

protected:
    virtual bool processEvents(ExecutionState* state, Optional<ByteCodeBlock*> byteCodeBlock, bool isBlockingRequest = true) override;

    virtual bool send(uint8_t type, const void* buffer, size_t length) override;
    virtual bool receive(uint8_t* buffer, size_t& length) override;
    virtual bool isThereAnyEvent() override;
    virtual void close(CloseReason reason) override;

private:
    void receiveData();

    EscargotSocket m_socket;
    uint8_t m_receiveBuffer[2 + sizeof(uint32_t) + ESCARGOT_DEBUGGER_MAX_MESSAGE_LENGTH];
    uint8_t m_receiveBufferFill;
    uint8_t m_messageLength;

    // skip generating debugging bytecode for source code whose name contains m_skipSourceName
    String* m_skipSourceName;
};
} // namespace Escargot
#endif /* ESCARGOT_DEBUGGER */

#endif
