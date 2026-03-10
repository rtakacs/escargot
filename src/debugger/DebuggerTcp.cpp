/*
 * Copyright (c) 2016-present Samsung Electronics Co., Ltd
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
#include "DebuggerTcp.h"
#include "DebuggerDevtools.h"
#include "DebuggerHttpRouter.h"
#include "runtime/String.h" // for split function

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

static bool tcpConfigureSocket(EscargotSocket socket, uint16_t port)
{
    sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt_value = 1;

    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt_value, sizeof(int)) != 0) {
        return false;
    }

    if (bind(socket, (sockaddr*)&addr, sizeof(sockaddr_in)) != 0) {
        return false;
    }

    return listen(socket, 1) == 0;
}

static inline void tcpCloseSocket(EscargotSocket socket)
{
#ifdef WIN32
    closesocket(socket);
#else /* !WIN32 */
    close(socket);
#endif /* WIN32 */
}

bool DebuggerTcp::tcpSend(EscargotSocket socket, const uint8_t* message, size_t messageLength)
{
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

bool DebuggerTcp::tcpReceive(EscargotSocket socket, uint8_t* message, size_t maxLength, size_t* receivedLength)
{
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

void DebuggerTcp::init(const char* options, Context* context)
{
    DebuggerRemote::init(options, context);
}

DebuggerRemote* DebuggerTcp::createDebugger(const char* options, Context* context)
{
    uint16_t port = 6501;
    int timeout = -1;

    String* skipSourceName = nullptr;
    EscargotSocket clientSocket = 0;

    if (options) {
        auto v = split(options, ';');
        const char portOption[] = "--port=";
        const char acceptTimeoutOption[] = "--accept-timeout=";
        const char skipOption[] = "--skip=";
        for (size_t i = 0; i < v.size(); i++) {
            const std::string& s = v[i];
            if (s.find(portOption) == 0) {
                int i = std::atoi(s.data() + sizeof(portOption) - 1);
                if (i > 0 && i <= 65535) {
                    port = i;
                }
            } else if (s.find(acceptTimeoutOption) == 0) {
                timeout = std::atoi(s.data() + sizeof(acceptTimeoutOption) - 1);
            } else if (s.find(skipOption) == 0) {
                const char* skipStr = const_cast<const char*>(s.data() + sizeof(skipOption) - 1);
                size_t skipLen = strlen(skipStr);
                skipSourceName = String::fromASCII(skipStr, skipLen);
            }
        }
    }

#ifdef WIN32
    WSADATA wsaData;
    int wsa_init_status = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_init_status != NO_ERROR) {
        return nullptr;
    }
#endif /* WIN32*/

    EscargotSocket serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == ESCARGOT_INVALID_SOCKET) {
        return nullptr;
    }

    if (!tcpConfigureSocket(serverSocket, port)) {
        int error = tcpGetErrno();
        tcpCloseSocket(serverSocket);
        tcpLogError(error);
        return nullptr;
    }

    ESCARGOT_LOG_INFO("Waiting for client connection 0.0.0.0:%hd\n", port);

    struct pollfd fd[1];
    fd[0].fd = serverSocket;
    fd[0].events = POLLIN;
    while (true) {
        int rc = poll(fd, 1, 0);

        if (rc != 0) {
            break;
        }

        usleep(10 * 1000); // 10ms
        if (timeout == -1) {
            continue;
        }
        timeout -= 10;
        if (timeout < 0) {
            ESCARGOT_LOG_ERROR("Waiting for client connection error: timeout reached\n");
            tcpCloseSocket(serverSocket);
            return nullptr;
        }
    }

    sockaddr_in addr;
    socklen_t sinSize = sizeof(sockaddr_in);
    DebuggerHttpRouter httpRouter;

    while (true) {
        clientSocket = accept(serverSocket, (sockaddr*)&addr, &sinSize);

        if (clientSocket == ESCARGOT_INVALID_SOCKET) {
            tcpLogError(tcpGetErrno());
            return nullptr;
        }

#ifdef WIN32
        u_long nonblockingEnabled = 1;

        /* Set non-blocking mode. */
        if (ioctlsocket(clientSocket, FIONBIO, &nonblockingEnabled) != NO_ERROR) {
            tcpCloseSocket(clientSocket);
            return nullptr;
        }
#else /* !WIN32 */
        int socketFlags = fcntl(clientSocket, F_GETFL, 0);

        if (socketFlags < 0) {
            tcpCloseSocket(clientSocket);
            return nullptr;
        }

        /* Set non-blocking mode. */
        if (fcntl(clientSocket, F_SETFL, socketFlags | O_NONBLOCK) == -1) {
            tcpCloseSocket(clientSocket);
            return nullptr;
        }
#endif /* WIN32 */

        ESCARGOT_LOG_INFO("Connected from: %s\n", inet_ntoa(addr.sin_addr));

        if (!httpRouter.handleHttpRequest(clientSocket)) {
            tcpCloseSocket(clientSocket);
            return nullptr;
        }

        if (httpRouter.webSocketEstablished()) {
            break;
        }

        // Close connection, waiting for the next request
        tcpCloseSocket(clientSocket);
    }

    tcpCloseSocket(serverSocket);

    DebuggerRemote* client;

    if (httpRouter.client() == DebuggerClient::Escargot) {
        client = new DebuggerTcp(clientSocket, skipSourceName);
    } else {
        client = new DebuggerDevtools(clientSocket, skipSourceName);
    }

    client->enable(context);
    return client;
}

bool DebuggerTcp::skipSourceCode(String* srcName) const
{
    ASSERT(!!srcName);
    if (!m_skipSourceName || m_skipSourceName->length() == 0 || srcName->length() == 0) {
        return false;
    }

    return srcName->contains(m_skipSourceName);
}

bool DebuggerTcp::send(uint8_t type, const void* buffer, size_t length)
{
    ASSERT(enabled());
    ASSERT(length < ESCARGOT_DEBUGGER_MAX_MESSAGE_LENGTH);

    uint8_t message[ESCARGOT_DEBUGGER_MAX_MESSAGE_LENGTH + 2];

    message[0] = ESCARGOT_DEBUGGER_WEBSOCKET_FIN_BIT | ESCARGOT_DEBUGGER_WEBSOCKET_BINARY_FRAME;
    message[1] = (uint8_t)(length + 1);
    message[2] = type;
    memcpy(message + 3, buffer, length);

    if (tcpSend(m_socket, message, length + 3)) {
        return true;
    }

    close(CloseAbortConnection);
    return false;
}

bool DebuggerTcp::isThereAnyEvent()
{
    // if there is remained receive buffer data,
    // user should call receive function again
    if (m_receiveBufferFill) {
        return true;
    }

    struct pollfd fd[1];
    fd[0].fd = m_socket;
    fd[0].events = POLLIN;
    int rc = poll(fd, 1, 0);

    if (rc == 0) {
        return false;
    }

    return true;
}

bool DebuggerTcp::receive(uint8_t* buffer, size_t& length)
{
    size_t receivedLength;

    if (m_messageLength == 0 || m_receiveBufferFill < 2 + sizeof(uint32_t) + m_messageLength) {
        /* Cannot extract a whole message from the buffer. */
        if (!tcpReceive(m_socket,
                        m_receiveBuffer + m_receiveBufferFill,
                        ESCARGOT_DEBUGGER_MAX_MESSAGE_LENGTH + 2 + sizeof(uint32_t) - m_receiveBufferFill,
                        &receivedLength)) {
            close(CloseAbortConnection);
            return false;
        }

        if (receivedLength == 0 && m_receiveBufferFill < (2 + sizeof(uint32_t))) {
            return false;
        }

        m_receiveBufferFill = (uint8_t)(m_receiveBufferFill + receivedLength);
    }

    if (m_messageLength == 0) {
        if (m_receiveBufferFill < 3) {
            return false;
        }

        if ((m_receiveBuffer[0] & ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK) != ESCARGOT_DEBUGGER_WEBSOCKET_BINARY_FRAME) {
            if ((m_receiveBuffer[0] & ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK) == ESCARGOT_DEBUGGER_WEBSOCKET_CLOSE_FRAME) {
                close(CloseEndConnection);
                return false;
            }

            ESCARGOT_LOG_ERROR("Unsupported Websocket opcode.\n");
            close(CloseProtocolUnsupported);
            return false;
        }

        if ((m_receiveBuffer[0] & ~ESCARGOT_DEBUGGER_WEBSOCKET_OPCODE_MASK) != ESCARGOT_DEBUGGER_WEBSOCKET_FIN_BIT
            || !(m_receiveBuffer[1] & ESCARGOT_DEBUGGER_WEBSOCKET_MASK_BIT)) {
            ESCARGOT_LOG_ERROR("Unsupported Websocket message.\n");
            close(CloseProtocolUnsupported);
            return false;
        }

        m_messageLength = (uint8_t)(m_receiveBuffer[1] & ESCARGOT_DEBUGGER_WEBSOCKET_LENGTH_MASK);

        if (m_messageLength == 0 || m_messageLength > ESCARGOT_DEBUGGER_WEBSOCKET_ONE_BYTE_LEN_MAX) {
            ESCARGOT_LOG_ERROR("Unsupported Websocket message size.\n");
            close(CloseProtocolUnsupported);
            return false;
        }
    }

    size_t totalSize = 2 + sizeof(uint32_t) + m_messageLength;

    if (m_receiveBufferFill < totalSize) {
        return false;
    }

    uint8_t* mask = m_receiveBuffer + 2;
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

    m_receiveBufferFill = (uint8_t)(m_receiveBufferFill - totalSize);
    memmove(m_receiveBuffer, m_receiveBuffer + totalSize, m_receiveBufferFill);
    return true;
}

void DebuggerTcp::close(CloseReason reason)
{
    if (!enabled()) {
        return;
    }

    if (reason != CloseAbortConnection) {
        uint8_t message[4];
        uint16_t reasonID = 1000;

        if (reason == CloseProtocolUnsupported) {
            reasonID = 1003;
        } else if (reason == CloseProtocolError) {
            reasonID = 1002;
        }

        message[0] = ESCARGOT_DEBUGGER_WEBSOCKET_FIN_BIT | ESCARGOT_DEBUGGER_WEBSOCKET_CLOSE_FRAME;
        message[1] = 2;
        message[2] = static_cast<uint8_t>(reasonID >> 8);
        message[3] = static_cast<uint8_t>(reasonID);

        tcpSend(m_socket, message, sizeof(message));
    }

    tcpCloseSocket(m_socket);
    disable();
}
} // namespace Escargot
#endif /* ESCARGOT_DEBUGGER */
