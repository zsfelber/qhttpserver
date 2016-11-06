/*
 * Copyright 2011-2014 Nikhil Marathe <nsm.nikhil@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "qhttpserver.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QVariant>
#include <QDebug>
#include <QEventLoop>

#include "qhttpconnection.h"

template <typename N>
inline N max_inl(N n1, N n2) {
    return (n1 < n2) ? n2 : n1;
}


QMtTcpEntry::QMtTcpEntry(QMtTcpServer *parent, QTcpSocket * socket) :
        parent(parent), started(false), eventLoop(0) {

    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    pendingSockets.append(socket);
}
QMtTcpEntry::~QMtTcpEntry() {
}

void QMtTcpEntry::run() {
    if (QThread::currentThread() == mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " == mainThread : "<<mainThread;
        throw std::exception();
    }

    {
        QMutexLocker ___lock(&mutex);
        started = true;

        QString ls;
        for (auto socket : pendingSockets) {
            socket->moveToThread(QThread::currentThread());
            ls += QString::number(socket->peerName()+" "+socket->peerAddress().toString()+":"+socket->peerPort())+", ";
        }
    }

    if (ok) {
        if (ls.length()) ls.remove(ls.length()-2, 2);
        qDebug() << "Event loop started for client socket(s):" << ls;

        QEventLoop eventLoop;
        this->eventLoop = &eventLoop;
        eventLoop.exec();
        this->eventLoop = 0;

        qDebug() << "Event loop finished:" << ls;
    }

    parent->activeEntries.removeOne(this);
    deleteLater();
}

void QMtTcpEntry::stop() {
    if (eventLoop) {
        eventLoop->exit();
    }
}

bool QMtTcpEntry::add(QTcpSocket * socket) {
    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    if (pendingSockets.size()>=parent->m_maxConnsPerThread) {
        return false;
    }

    QMutexLocker ___lock(&mutex);
    if (started) {
        return false;
    } else {
        pendingSockets.append(socket);
        return true;
    }
}

QTcpSocket * QMtTcpEntry::pop() {
    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    auto s1 = pendingSockets.first();
    return s1.socket;
}

/// Construct a new multithreaded TCP Server.
/** @param parent Parent QObject for the server. */
QMtTcpServer::QMtTcpServer(QObject *parent, int maxThreads, int maxConnsPerThread, int maxPendingConnections) :
    QTcpServer(parent),
    m_maxThreads(maxThreads), m_maxConnsPerThread(maxConnsPerThread),
    m_prefThreads(max_inl(1,maxThreads/maxConnsPerThread))
{
    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }
    setMaxPendingConnections(maxPendingConnections);
    tpool.setMaxThreadCount(maxThreads);
}

void QMtTcpServer::incomingConnection(qintptr socketDescriptor) {

    // WARN newConnection has emitted from main thread prior to this current call.
    // socket and connection objects branch needs moveToThread(socket thread) and
    // Qt::DirectConnection-s of signals to make them all to receive in socket thread !

    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    auto socket = parent->createClientSocketPeer(socketDescriptor);

    if (socket) {

        int a = activeEntries.size();

        if (a <= m_prefThreads || m_maxThreads <= a) {

            auto e = new QMtTcpEntry(this, socket);
            activeEntries.push(e);
            tpool.start(e);

        } else {

            for (auto e : activeEntries) {
                if (e->add(socket)) {
                    return;
                }
            }

            auto e = new QMtTcpEntry(this, socket);
            activeEntries.push(e);
            tpool.start(e);
        }

    }
}

QTcpSocket * QMtTcpServer::createClientSocketPeer(qintptr socketDescriptor) {
    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    // Affinity is (will set later to) QMtTcpEntry's QThreadPool (tpool) thread,
    // so event loop is running and messaging socket (thus whole object branch)
    // from there:
    QTcpSocket * clientPeer = new QTcpSocket();

    // NOTE we ignore that pending client entries may be maxThreadConns times more, never mind
    // it's good.
    int sz = activeEntries.size();

    if (!clientPeer->setSocketDescriptor(socketDescriptor)
            //&& clientPeer->open(QTcpSocket::ReadWrite)
            ) {
        qCritical()<<"client socket failure, aborted (#"<<sz<<"/"<<maxPendingConnections()<<") : "<<socketDescriptor;
        acceptError(QAbstractSocket::SocketResourceError);
        clientPeer->abort();
        clientPeer = 0;
    } else if (maxPendingConnections()<=sz) {
        qCritical()<<"Too many pending connections (#"<<sz<<"/"<<maxPendingConnections()<<") : "<<socketDescriptor;
        acceptError(QAbstractSocket::ConnectionRefusedError);
        clientPeer->abort();
        clientPeer = 0;
    } else {
        qDebug()<<"Pending client socket accepted : (#"<<sz<<"/"<<maxPendingConnections()<<") : "<<socketDescriptor;
    }
    return clientPeer;
}

bool QMtTcpServer::hasPendingConnections() {
    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    return activeEntries.size();
}

QTcpSocket * QMtTcpServer::nextPendingConnection() {
    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    for (auto e1 : activeEntries) {
        auto s1 = e1->pop();
        if (s1) {
            return s1;
        }
    }
    return 0;
}



QHash<int, QString> STATUS_CODES;

QHttpServer::QHttpServer(QObject *parent, int maxThreads, int maxConnsPerThread, int maxPendingConnections) :
    QObject(parent), m_tcpServer(0), m_maxThreads(maxThreads), m_maxConnsPerThread(maxConnsPerThread),
    m_maxPendingConnections(maxPendingConnections)
{
#define STATUS_CODE(num, reason) STATUS_CODES.insert(num, reason);
    // {{{
    STATUS_CODE(100, "Continue")
    STATUS_CODE(101, "Switching Protocols")
    STATUS_CODE(102, "Processing") // RFC 2518) obsoleted by RFC 4918
    STATUS_CODE(200, "OK")
    STATUS_CODE(201, "Created")
    STATUS_CODE(202, "Accepted")
    STATUS_CODE(203, "Non-Authoritative Information")
    STATUS_CODE(204, "No Content")
    STATUS_CODE(205, "Reset Content")
    STATUS_CODE(206, "Partial Content")
    STATUS_CODE(207, "Multi-Status") // RFC 4918
    STATUS_CODE(300, "Multiple Choices")
    STATUS_CODE(301, "Moved Permanently")
    STATUS_CODE(302, "Moved Temporarily")
    STATUS_CODE(303, "See Other")
    STATUS_CODE(304, "Not Modified")
    STATUS_CODE(305, "Use Proxy")
    STATUS_CODE(307, "Temporary Redirect")
    STATUS_CODE(400, "Bad Request")
    STATUS_CODE(401, "Unauthorized")
    STATUS_CODE(402, "Payment Required")
    STATUS_CODE(403, "Forbidden")
    STATUS_CODE(404, "Not Found")
    STATUS_CODE(405, "Method Not Allowed")
    STATUS_CODE(406, "Not Acceptable")
    STATUS_CODE(407, "Proxy Authentication Required")
    STATUS_CODE(408, "Request Time-out")
    STATUS_CODE(409, "Conflict")
    STATUS_CODE(410, "Gone")
    STATUS_CODE(411, "Length Required")
    STATUS_CODE(412, "Precondition Failed")
    STATUS_CODE(413, "Request Entity Too Large")
    STATUS_CODE(414, "Request-URI Too Large")
    STATUS_CODE(415, "Unsupported Media Type")
    STATUS_CODE(416, "Requested Range Not Satisfiable")
    STATUS_CODE(417, "Expectation Failed")
    STATUS_CODE(418, "I\"m a teapot")        // RFC 2324
    STATUS_CODE(422, "Unprocessable Entity") // RFC 4918
    STATUS_CODE(423, "Locked")               // RFC 4918
    STATUS_CODE(424, "Failed Dependency")    // RFC 4918
    STATUS_CODE(425, "Unordered Collection") // RFC 4918
    STATUS_CODE(426, "Upgrade Required")     // RFC 2817
    STATUS_CODE(500, "Internal Server Error")
    STATUS_CODE(501, "Not Implemented")
    STATUS_CODE(502, "Bad Gateway")
    STATUS_CODE(503, "Service Unavailable")
    STATUS_CODE(504, "Gateway Time-out")
    STATUS_CODE(505, "HTTP Version not supported")
    STATUS_CODE(506, "Variant Also Negotiates") // RFC 2295
    STATUS_CODE(507, "Insufficient Storage")    // RFC 4918
    STATUS_CODE(509, "Bandwidth Limit Exceeded")
    STATUS_CODE(510, "Not Extended") // RFC 2774
    // }}}
}

QHttpServer::~QHttpServer()
{
}

void QHttpServer::_newConnection()
{
    Q_ASSERT(m_tcpServer);

    // NOTE signals posted from main thread :

    if (QThread::currentThread() != mainThread) {
        qCritical()<<"current thread : "<<QThread::currentThread()<<":"<<QThread::currentThreadId()<<
            " != mainThread : "<<mainThread;
        throw std::exception();
    }

    while (m_tcpServer->hasPendingConnections()) {
        QHttpConnection *connection =
            new QHttpConnection(m_tcpServer->nextPendingConnection());
        connect(connection, SIGNAL(newRequest(QHttpRequest *, QHttpResponse *)), this,
                SIGNAL(newRequest(QHttpRequest *, QHttpResponse *)));
        emit newConnection(connection);
    }
}

bool QHttpServer::listen(const QHostAddress &address, quint16 port)
{
    Q_ASSERT(!m_tcpServer);
    m_tcpServer = new QMtTcpServer(this, m_maxThreads, m_maxConnsPerThread, m_maxPendingConnections);

    bool couldBindToPort = m_tcpServer->listen(address, port);
    if (couldBindToPort) {
        connect(m_tcpServer, SIGNAL(newConnection()), this, SLOT(_newConnection()));
    } else {
        delete m_tcpServer;
        m_tcpServer = NULL;
    }
    return couldBindToPort;
}

bool QHttpServer::listen(quint16 port)
{
    return listen(QHostAddress::Any, port);
}

void QHttpServer::close()
{
    if (m_tcpServer)
        m_tcpServer->close();
}
