#include "RdbClient.h"

#include <QTcpSocket>
#include <QTimer>

RdbClient::RdbClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_retryTimer(new QTimer(this))
{
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &RdbClient::tryConnect);
    connect(m_socket, &QTcpSocket::readyRead, this, &RdbClient::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, &RdbClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RdbClient::disconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &RdbClient::onSocketError);
}

RdbClient::~RdbClient() = default;

bool RdbClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void RdbClient::connectToHatari(const QString &host, quint16 port, int retries, int intervalMs)
{
    m_host = host;
    m_port = port;
    m_retriesLeft = retries;
    m_retryIntervalMs = intervalMs;
    m_handshakeSeen = false;
    m_protocolId = 0;
    m_buffer.clear();
    m_pending.clear();
    tryConnect();
}

void RdbClient::disconnectFromHatari()
{
    m_retryTimer->stop();
    m_retriesLeft = 0;
    m_socket->abort();
}

void RdbClient::tryConnect()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void RdbClient::onSocketConnected()
{
    // Socket is up; the !connected handshake follows in onReadyRead. We emit
    // connected() only once the handshake confirms the protocol.
}

void RdbClient::onSocketError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err);
    // While still racing Hatari's listener, keep retrying quietly.
    if (m_retriesLeft > 0) {
        --m_retriesLeft;
        m_retryTimer->start(m_retryIntervalMs);
        return;
    }
    if (!m_handshakeSeen)
        emit connectionFailed(m_socket->errorString());
}

void RdbClient::sendCommand(const QByteArray &line, ReplyHandler handler)
{
    if (!isConnected())
        return;
    m_pending.enqueue(std::move(handler)); // one reply per command, even if null
    QByteArray frame = line;
    frame.append(kTerm);
    m_socket->write(frame);
}

void RdbClient::screenshot(const QString &pngPath, ReplyHandler h)
{
    // Routed through the `console` verb to Hatari's debugui screenshot command,
    // which saves its rendered surface (the taken framebuffer, D-007) as PNG.
    sendCommand("console screenshot " + pngPath.toLocal8Bit(), std::move(h));
}

void RdbClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // Carve out each 0x0-terminated message.
    int term;
    while ((term = m_buffer.indexOf(kTerm)) >= 0) {
        const QByteArray message = m_buffer.left(term);
        m_buffer.remove(0, term + 1);

        Tokens tokens;
        if (!message.isEmpty()) {
            for (const QByteArray &tok : message.split(kSep))
                tokens.append(tok);
        }
        dispatch(tokens);
    }
}

void RdbClient::dispatch(const Tokens &tokens)
{
    if (tokens.isEmpty())
        return;

    const QByteArray &head = tokens.first();
    if (head.startsWith('!')) {
        const QByteArray name = head.mid(1);
        const Tokens args = tokens.mid(1);

        if (name == "connected") {
            bool ok = false;
            m_protocolId = args.isEmpty() ? 0 : args.first().toInt(&ok, 16);
            m_handshakeSeen = true;
            emit protocolIdReceived(m_protocolId);
            emit connected();
        }
        emit notification(name, args);
        return;
    }

    // Otherwise it is a command reply (OK / NG) — match to the oldest command.
    if (!m_pending.isEmpty()) {
        ReplyHandler handler = m_pending.dequeue();
        if (handler)
            handler(tokens);
    }
}
