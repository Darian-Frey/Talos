// RdbClient — B1 remote-debug protocol client (the hrdb-lineage pipe).
//
// Speaks the verified wire format (protocol/b1-protocol.md):
//   - a TCP stream of messages, each terminated by 0x0;
//   - within a message, tokens are separated by 0x1;
//   - server->client messages are either NOTIFICATIONS (first token starts '!',
//     pushed unsolicited: !connected, !config, !status) or command REPLIES
//     (first token "OK" or "NG"), one reply per command, in order.
//
// This class owns the QTcpSocket, reassembles messages, and routes them: each
// sent command enqueues a callback that is invoked with the next reply; '!'
// notifications are emitted as signals and never consume a pending callback.

#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QQueue>
#include <functional>

class QTcpSocket;
class QTimer;

class RdbClient : public QObject
{
    Q_OBJECT
public:
    // Tokens of one framed message (reply: [0]=="OK"/"NG"; notification: [0]=="!name").
    using Tokens = QList<QByteArray>;
    using ReplyHandler = std::function<void(const Tokens &)>;

    explicit RdbClient(QObject *parent = nullptr);
    ~RdbClient() override;

    // Connect with a few retries, so we can launch Hatari then race its listener.
    void connectToHatari(const QString &host, quint16 port,
                         int retries = 40, int intervalMs = 250);
    void disconnectFromHatari();
    bool isConnected() const;

    // Protocol id from the !connected handshake (0 until received).
    int protocolId() const { return m_protocolId; }

    // Send a raw command line (e.g. "regs", "step", "console screenshot /tmp/f.png").
    // Every command yields exactly one OK/NG reply; the handler (may be null) is
    // enqueued so reply/command ordering stays aligned.
    void sendCommand(const QByteArray &line, ReplyHandler handler = {});

    // Convenience wrappers for the M0 verbs.
    void reqStatus(ReplyHandler h = {}) { sendCommand("status", std::move(h)); }
    void reqRegs(ReplyHandler h = {}) { sendCommand("regs", std::move(h)); }
    void step(ReplyHandler h = {}) { sendCommand("step", std::move(h)); }
    void run(ReplyHandler h = {}) { sendCommand("run", std::move(h)); }
    void breakExec(ReplyHandler h = {}) { sendCommand("break", std::move(h)); }
    void screenshot(const QString &pngPath, ReplyHandler h = {});

signals:
    void connected();                 // socket up AND !connected handshake seen
    void disconnected();
    void connectionFailed(const QString &reason);
    void protocolIdReceived(int id);
    // A '!'-prefixed notification; name has the '!' stripped.
    void notification(const QByteArray &name, const RdbClient::Tokens &args);

private slots:
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onSocketConnected();

private:
    void dispatch(const Tokens &tokens);
    void tryConnect();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_retryTimer = nullptr;
    QByteArray m_buffer;                 // unframed inbound bytes
    QQueue<ReplyHandler> m_pending;      // FIFO of awaiting-reply callbacks
    QString m_host;
    quint16 m_port = 0;
    int m_retriesLeft = 0;
    int m_retryIntervalMs = 250;
    int m_protocolId = 0;
    bool m_handshakeSeen = false;

    static constexpr char kSep = 0x01;   // token separator
    static constexpr char kTerm = 0x00;  // message terminator
};
