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

#include "Escargot.h"
#include "DebuggerDevtools.h"

#include "interpreter/ByteCode.h"

#ifdef ESCARGOT_DEBUGGER

namespace Escargot {

#ifdef WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#include <WS2tcpip.h>

/* On Windows the WSAEWOULDBLOCK value can be returned for non-blocking operations */
#define ESCARGOT_EWOULDBLOCK WSAEWOULDBLOCK

/* On Windows the invalid socket's value of INVALID_SOCKET */
#define ESCARGOT_INVALID_SOCKET INVALID_SOCKET

#else /* !WIN32 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>

/* On *nix the EWOULDBLOCK errno value can be returned for non-blocking operations */
#define ESCARGOT_EWOULDBLOCK EWOULDBLOCK

/* On *nix the invalid socket has a value of -1 */
#define ESCARGOT_INVALID_SOCKET (-1)

#endif /* WIN32 */

static inline int tcpGetErrno(void)
{
#ifdef WIN32
    return WSAGetLastError();
#else /* !WIN32 */
    return errno;
#endif /* WIN32 */
}

static void tcpLogError(int errorNumber)
{
    ASSERT(errorNumber != 0);

#ifdef WIN32
    char* errorMessage = NULL;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  errorNumber,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&errorMessage,
                  0,
                  NULL);
    ESCARGOT_LOG_ERROR("TCP Error: %s\n", errorMessage);
    LocalFree(errorMessage);
#else /* !WIN32 */
    ESCARGOT_LOG_ERROR("TCP Error: %s\n", strerror(errorNumber));
#endif /* WIN32 */
}

static inline void tcpCloseSocket(EscargotSocket socket)
{
#ifdef WIN32
    closesocket(socket);
#else /* !WIN32 */
    close(socket);
#endif /* WIN32 */
}

void DebuggerDevtools::init(const char* options, Context* context)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::init\n");
}

bool DebuggerDevtools::skipSourceCode(String* srcName) const
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::skipSourceCode\n");
    return false;
}

bool DebuggerDevtools::tcpSend(EscargotSocket socket, const uint8_t* message, size_t messageLength)
{
    ESCARGOT_LOG_INFO("Implemented: DebuggerDevtools::tcpSend\n");
    do {
#ifdef OS_POSIX
        ssize_t result = recv(socket, NULL, 0, MSG_PEEK);

        if (result == 0 && (errno != ESCARGOT_EWOULDBLOCK && errno != 0)) {
            tcpLogError(tcpGetErrno());
            return false;
        }
#endif /* OS_POSIX */

        ssize_t sentBytes = Escargot::send(socket, message, messageLength, 0);

        if (sentBytes < 0) {
            int errorNumber = tcpGetErrno();

            if (errorNumber == ESCARGOT_EWOULDBLOCK) {
                continue;
            }

            tcpLogError(tcpGetErrno());
            return false;
        }

        message += sentBytes;
        messageLength -= (size_t)sentBytes;
    } while (messageLength > 0);

    return true;
}

bool DebuggerDevtools::tcpReceive(EscargotSocket socket, uint8_t* message, size_t maxLength, size_t* receivedLength)
{
    ESCARGOT_LOG_INFO("Implemented: DebuggerDevtools::tcpReceive\n");
    *receivedLength = 0;

    ssize_t length = recv(socket, message, maxLength, 0);

    if (length > 0) {
        *receivedLength = (size_t)length;
        return true;
    }

    int errorNumber = tcpGetErrno();

    if (errorNumber != ESCARGOT_EWOULDBLOCK || length == 0) {
        tcpLogError(errorNumber);
        return false;
    }
    return true;
}

void DebuggerDevtools::parseCompleted(String* source, String* srcName, size_t originLineOffset, String* error)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::parseCompleted\n");
}

void DebuggerDevtools::stopAtBreakpoint(ByteCodeBlock* byteCodeBlock, uint32_t offset, ExecutionState* state)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::stopAtBreakpoint\n");
    if (m_stopState == ESCARGOT_DEBUGGER_IN_EVAL_MODE) {
        m_delay--;
        if (m_delay == 0) {
            processEvents(state, byteCodeBlock);
        }
        return;
    }

    uint8_t* byteCodeStart = byteCodeBlock->m_code.data();

    // send(ESCARGOT_MESSAGE_BREAKPOINT_HIT, &breakpointOffset, sizeof(BreakpointOffset));

    if (!enabled()) {
        return;
    }

    ASSERT(m_activeObjects.size() == 0);
    m_stopState = ESCARGOT_DEBUGGER_IN_WAIT_MODE;

    while (processEvents(state, byteCodeBlock))
        ;

    m_activeObjects.clear();
    m_delay = ESCARGOT_DEBUGGER_MESSAGE_PROCESS_DELAY;
}

void DebuggerDevtools::byteCodeReleaseNotification(ByteCodeBlock* byteCodeBlock)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::byteCodeReleaseNotification\n");
}

void DebuggerDevtools::exceptionCaught(String* message, SavedStackTraceDataVector& exceptionTrace)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::exceptionCaught\n");
}

void DebuggerDevtools::consoleOut(String* output)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::consoleOut\n");
}

String* DebuggerDevtools::getClientSource(String** sourceName)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::getClientSource\n");
    return nullptr;
}

bool DebuggerDevtools::getWaitBeforeExitClient()
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::getWaitBeforeExitClient\n");
    while (processEvents(nullptr, nullptr))
        ;
    return false;
}

bool DebuggerDevtools::processEvents(ExecutionState* state, Optional<ByteCodeBlock*> byteCodeBlock, bool isBlockingRequest)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::processEvents\n");

    uint8_t buffer[ESCARGOT_DEVTOOLS_DEBUGGER_MAX_MESSAGE_LENGTH];
    size_t length;

    while (true) {
        if (isBlockingRequest) {
            if (!receive(buffer, length)) {
                break;
            }
        } else {
            if (isThereAnyEvent()) {
                if (!receive(buffer, length)) {
                    break;
                }
            } else {
                return false;
            }
        }
    }

    return false;
}

bool DebuggerDevtools::send(uint8_t type, const void* buffer, size_t length)
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::send\n");
    return false;
}

bool DebuggerDevtools::receive(uint8_t* buffer, size_t& length)
{
    ESCARGOT_LOG_INFO("DebuggerDevtools::receive:\n");

    size_t receivedLength = 0;


    if (m_messageLength == 0 || m_receiveBufferFill < 2 + sizeof(uint32_t) + m_messageLength) {
        /* Cannot extract a whole message from the buffer. */
        if (!tcpReceive(m_socket,
                        m_receiveBuffer + m_receiveBufferFill,
                        ESCARGOT_DEVTOOLS_DEBUGGER_MAX_MESSAGE_LENGTH + 2 + sizeof(uint32_t) - m_receiveBufferFill,
                        &receivedLength)) {
            close(CloseAbortConnection);
            ESCARGOT_LOG_INFO("ERROR 1: closed connection\n");
            return false;
        }

        if (receivedLength == 0 && m_receiveBufferFill < (2 + sizeof(uint32_t))) {
            ESCARGOT_LOG_INFO("ERROR 2: receivedLength: %lu, m_receiveBufferFill: %d\n", receivedLength, m_receiveBufferFill);
            return false;
        }

        m_receiveBufferFill = static_cast<uint32_t>(m_receiveBufferFill + receivedLength);
    }

    if (m_messageLength == 0) {
        if (m_receiveBufferFill < 3) {
            return false;
        }

        if ((m_receiveBuffer[0] & ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK) != ESCARGOT_DEBUGGER_WEBSOCKET_TEXT_FRAME) {
            if ((m_receiveBuffer[0] & ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK) == ESCARGOT_DEBUGGER_WEBSOCKET_CLOSE_FRAME) {
                close(CloseEndConnection);
                return false;
            }

            ESCARGOT_LOG_ERROR("Unsupported Websocket opcode: %x\n", (m_receiveBuffer[0] & ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK));
            ESCARGOT_LOG_ERROR("Unsupported Websocket opcode: %x\n", (m_receiveBuffer[0]));
            close(CloseProtocolUnsupported);
            return false;
        }

        if ((m_receiveBuffer[0] & ~ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK) != ESCARGOT_DEBUGGER_WEBSOCKET_FIN_BIT
            || !(m_receiveBuffer[1] & ESCARGOT_DEBUGGER_WEBSOCKET_MASK_BIT)) {
            ESCARGOT_LOG_ERROR("Unsupported Websocket message: %x\n", m_receiveBuffer[0]);
            close(CloseProtocolUnsupported);
            return false;
        }

        m_messageLength = static_cast<uint8_t>(m_receiveBuffer[1] & ESCARGOT_DEBUGGER_WEBSOCKET_LENGTH_MASK);
        if (m_messageLength <= ESCARGOT_DEBUGGER_MAX_MESSAGE_LENGTH) {
            // length is the 7 bit value
        } else if (m_messageLength == ESCARGOT_DEVTOOLS_DEBUGGER_MESASGE_LENGTH_16BIT) {
            m_messageLength = *(m_receiveBuffer + 2);
            m_messageLength <<= 8;
            m_messageLength |= *(m_receiveBuffer + 3);
        } else if (m_messageLength == ESCARGOT_DEVTOOLS_DEBUGGER_MESASGE_LENGTH_64BIT) {
            ESCARGOT_LOG_ERROR("Unsupported Websocket message size mode: 64 bit");
        }
        printf("len: %lu\n", m_messageLength);

        if (m_messageLength == 0 || m_messageLength > ESCARGOT_DEVTOOLS_DEBUGGER_MAX_MESSAGE_LENGTH
            || m_messageLength == ESCARGOT_DEVTOOLS_DEBUGGER_MESASGE_LENGTH_64BIT) {
            ESCARGOT_LOG_ERROR("Unsupported Websocket message size: %lu\n", m_messageLength);
            close(CloseProtocolUnsupported);
            return false;
        }
        ESCARGOT_LOG_INFO("Websocket message size: %lu\n", m_messageLength);
    }

    size_t totalSize = 2 + sizeof(uint32_t) + m_messageLength;

    if (m_receiveBufferFill < totalSize) {
        return false;
    }

    uint8_t* mask = m_receiveBuffer + 2 + (m_messageLength > ESCARGOT_DEBUGGER_MAX_MESSAGE_LENGTH ? 2 : 0);
    uint8_t* mask_end = mask + sizeof(uint32_t);
    uint8_t* source = mask_end;
    uint8_t* buffer_end = buffer + m_messageLength;

    while (buffer < buffer_end) {
        *buffer++ = *source++ ^ *mask++;

        if (mask >= mask_end) {
            mask -= 4;
        }
    }

    length = m_messageLength;
    m_messageLength = 0;

    if (m_receiveBufferFill == totalSize) {
        m_receiveBufferFill = 0;
        return true;
    }

    m_receiveBufferFill = static_cast<uint32_t>(m_receiveBufferFill - totalSize);
    memmove(m_receiveBuffer, m_receiveBuffer + totalSize, m_receiveBufferFill);

    buffer -= length;
    printf("MESSAGE: %.*s\n", static_cast<int>(length), reinterpret_cast<const char*>(buffer));

    return true;
}

bool DebuggerDevtools::isThereAnyEvent()
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::isThereAnyEvent\n");
    return false;
}

void DebuggerDevtools::close(CloseReason reason)
{
    ESCARGOT_LOG_INFO("Implemented: DebuggerDevtools::close\n");

    tcpCloseSocket(m_socket);
}

void DebuggerDevtools::receiveData()
{
    ESCARGOT_LOG_INFO("Implement this: DebuggerDevtools::receiveData\n");
}

} // namespace Escargot

#endif /* ESCARGOT_DEBUGGER */
