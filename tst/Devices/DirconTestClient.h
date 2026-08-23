#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QList>
#include <QTcpSocket>

#include <string>

/**
 * A DIRCON client for Layer C of docs/fork/VIRTUAL-BIKE.md, shared by the phase 2
 * enumeration tests and the phase 3 ride loop.
 *
 * ## Why it has its own codec
 *
 * It shares no code with `DirconPacket`. If it decoded QZ's answers by handing them back
 * to QZ's own codec, a framing error would be invisible - both sides would make it
 * together - which is the same "encoder and parser agree on the same mistake" risk the
 * frame harness has one level down. Requests are built here from the six-byte header
 * described in `dirconpacket.h`; responses are compared against complete frames written
 * out as hex, and the one structured decoder below reads by literal offset.
 *
 * The expected bytes were captured from the shipped
 * `qdomyos-zwift -no-gui -simulated-bike -ride <file>` binary with an independent python
 * client, in both the default and the `rouvy_compatibility` profiles.
 */
namespace dircontest {

/// Away from the shipped 36866 so a QZ running on the developer's machine and these tests
/// are not fighting over the same listener.
const quint16 kBasePort = 47820;
/// The offset is the machine id, not a free choice: `server_base_port +
/// DM_MACHINE_WAHOO_KICKR` and `+ DM_MACHINE_WAHOO_BLUEHR` in dirconmanager.cpp.
const quint16 kBikePort = kBasePort + 0;
const quint16 kHeartPort = kBasePort + 1;

inline std::string hexOf(const QByteArray &b) { return std::string(b.toHex().constData()); }

/**
 * @brief The 128-bit form of a 16-bit Bluetooth UUID, for building requests.
 *
 * Assertions never go through this - see the namespace comment. It exists so a request can
 * name a characteristic without ninety characters of hex at every call site.
 */
inline QByteArray wireUuid(quint16 uuid) {
    QByteArray b = QByteArray::fromHex("0000000000001000800000805f9b34fb");
    b[2] = static_cast<char>(uuid >> 8);
    b[3] = static_cast<char>(uuid & 0xFF);
    return b;
}

/**
 * @brief One Indoor Bike Data frame, read by literal offset.
 *
 * A captured frame, for checking the offsets below by eye - `steady.ride` at 200 W:
 *
 *     64 02  73 0c  b4 00  0c 00  c8 00  84 00
 *     flags  speed  cad    resist power  hr
 *     0x0264 31.87  90rpm  12     200 W  132
 *
 * Flags 0x0264 is instantaneous speed, cadence, resistance level, instantaneous power and
 * heart rate. Speed is in 0.01 km/h, cadence in 0.5 rpm, and the trailing zero byte after
 * the heart rate is the Bkool HRM offset fix - see characteristicnotifier2ad2.cpp.
 */
struct IndoorBikeData {
    quint16 flags = 0;
    double speedKmh = 0;
    double cadenceRpm = 0;
    quint16 resistance = 0;
    quint16 watts = 0;
    quint8 heart = 0;
    QByteArray body;

    bool operator==(const IndoorBikeData &o) const { return body == o.body; }
    bool operator!=(const IndoorBikeData &o) const { return body != o.body; }
};

/// @param frame A whole notification frame: six-byte header, 16-byte UUID, then the value.
inline IndoorBikeData decodeIndoorBikeData(const QByteArray &frame) {
    IndoorBikeData d;
    d.body = frame.mid(6 + 16);
    const quint8 *b = reinterpret_cast<const quint8 *>(d.body.constData());
    if (d.body.size() < 12)
        return d;
    d.flags = quint16(b[0] | (b[1] << 8));
    d.speedKmh = (b[2] | (b[3] << 8)) / 100.0;
    d.cadenceRpm = (b[4] | (b[5] << 8)) / 2.0;
    d.resistance = quint16(b[6] | (b[7] << 8));
    d.watts = quint16(b[8] | (b[9] << 8));
    d.heart = b[10];
    return d;
}

/**
 * @brief A fake training app: connect, request, subscribe, watch the stream.
 *
 * The server runs in this thread, so every wait has to turn the event loop:
 * `waitForConnected()` and `waitForReadyRead()` would block the very object that owes the
 * answer and the test would time out against itself.
 */
class FakeTrainingApp {
  public:
    ~FakeTrainingApp() { m_sock.abort(); }

    bool connectTo(quint16 port, int ms = 5000) {
        m_sock.connectToHost(QHostAddress::LocalHost, port);
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (m_sock.state() != QAbstractSocket::ConnectedState &&
               m_sock.state() != QAbstractSocket::UnconnectedState &&
               QDateTime::currentMSecsSinceEpoch() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return m_sock.state() == QAbstractSocket::ConnectedState;
    }

    /**
     * @brief Send a request, return the whole frame that answers it.
     *
     * Unsolicited notifications (identifier 0x06) are set aside rather than returned: QZ
     * pushes those on its own clock from the moment a device is bound, and with
     * `wahoo_rgt_dircon` off it pushes them whether or not anyone subscribed, so any
     * client that cannot cope with one landing mid-exchange is a client that would fall
     * over against the real thing.
     *
     * @return The response frame, or an empty array if nothing answered in time.
     */
    QByteArray exchange(quint8 identifier, const QByteArray &payload = QByteArray(), int ms = 5000) {
        send(identifier, payload);
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            pump();
            for (QByteArray frame = takeFrame(); !frame.isEmpty(); frame = takeFrame()) {
                if (isNotification(frame))
                    m_notifications.append(frame);
                else
                    return frame;
            }
        }
        return QByteArray();
    }

    void send(quint8 identifier, const QByteArray &payload = QByteArray()) {
        QByteArray request;
        request.append(static_cast<char>(0x01));       // message version
        request.append(static_cast<char>(identifier)); // message identifier
        request.append(static_cast<char>(m_seq++));    // sequence number, echoed back
        request.append(static_cast<char>(0x00));       // response code: zero marks a request
        request.append(static_cast<char>(payload.size() >> 8));
        request.append(static_cast<char>(payload.size() & 0xFF));
        request.append(payload);
        m_sock.write(request);
        m_sock.flush();
    }

    /// ENABLE_CHARACTERISTIC_NOTIFICATIONS with the on byte set.
    QByteArray subscribe(quint16 uuid) {
        return exchange(0x05, wireUuid(uuid) + QByteArray(1, 0x01));
    }

    /// WRITE_CHARACTERISTIC.
    QByteArray write(quint16 uuid, const QByteArray &value, int ms = 5000) {
        return exchange(0x04, wireUuid(uuid) + value, ms);
    }

    /**
     * @brief Turn the event loop for @p ms, returning every notification for @p uuid that
     * arrived in that window - decoded, in order.
     *
     * The backlog is thrown away first, and that is not a tidiness measure. QZ notifies on
     * its own timer from the moment a device is bound, and Qt buffers what arrives whenever
     * the event loop turns - which it does constantly while a test waits for the ride to
     * reach some point. Without the discard, a test that waits twenty seconds and then looks
     * at the stream is handed the twenty seconds it waited through, and asserts on the wrong
     * part of the ride while looking entirely correct.
     */
    QList<IndoorBikeData> watch(quint16 uuid, int ms) {
        discardPending();
        QList<IndoorBikeData> out;
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            drainInto(uuid, out);
        drainInto(uuid, out);
        return out;
    }

    /// Drop every frame already queued, in the socket and in our own buffer.
    void discardPending() {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 150;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            pump();
            while (!takeFrame().isEmpty()) {
            }
        }
        m_notifications.clear();
    }

    /// Notification frames seen so far, whatever their UUID.
    const QList<QByteArray> &notifications() const { return m_notifications; }
    void forgetNotifications() { m_notifications.clear(); }

  private:
    void pump() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_buffer.append(m_sock.readAll());
    }

    void drainInto(quint16 uuid, QList<IndoorBikeData> &out) {
        pump();
        for (QByteArray frame = takeFrame(); !frame.isEmpty(); frame = takeFrame()) {
            if (!isNotification(frame))
                continue;
            m_notifications.append(frame);
            if (notificationUuid(frame) == uuid)
                out.append(decodeIndoorBikeData(frame));
        }
    }

    static bool isNotification(const QByteArray &frame) {
        return static_cast<quint8>(frame.at(1)) == 0x06;
    }

    /// The 16-bit UUID sits in the same two bytes of the 128-bit form the requests use.
    static quint16 notificationUuid(const QByteArray &frame) {
        return quint16((static_cast<quint8>(frame.at(6 + 2)) << 8) |
                       static_cast<quint8>(frame.at(6 + 3)));
    }

    /// One frame off the front of the buffer, split by the header's own length field:
    /// six bytes of header, then `Length` bytes of body.
    QByteArray takeFrame() {
        if (m_buffer.size() < 6)
            return QByteArray();
        const int length =
            (static_cast<quint8>(m_buffer.at(4)) << 8) | static_cast<quint8>(m_buffer.at(5));
        if (m_buffer.size() < 6 + length)
            return QByteArray();
        const QByteArray frame = m_buffer.left(6 + length);
        m_buffer.remove(0, 6 + length);
        return frame;
    }

    QTcpSocket m_sock;
    QByteArray m_buffer;
    QList<QByteArray> m_notifications;
    /// Starts at 1 and must move every request: the server treats a packet whose sequence
    /// number repeats the last one as something other than a request and never answers it.
    quint8 m_seq = 1;
};

} // namespace dircontest
