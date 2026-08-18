#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QUdpSocket>

/**
 * A DNS codec and mDNS client for Layer C phase 4, written from RFC 1035 and RFC 6762 and
 * sharing nothing with `qmdnsengine`.
 *
 * That independence is the whole point. QZ announces with `qmdnsengine`, so a test that
 * browsed with `qmdnsengine` would be asking the same code whether it agrees with itself -
 * a name it encodes wrongly it would also parse wrongly, and every assertion would pass.
 * Everything below - label encoding, pointer decompression, the SRV and TXT layouts - is a
 * second opinion, and the wire format is the only thing the two share.
 *
 * ## Two ways to be answered, and they test different things
 *
 * A query sent from an **ephemeral source port** is answered *unicast*, straight back to
 * that port: `Message::reply()` only sends to the multicast group when the query came from
 * port 5353. That path needs no multicast anywhere and is what most tests here use.
 *
 * A query sent **from port 5353** - which is what a real browser does - is answered on the
 * multicast group, and so are announcements and goodbyes. Receiving those needs a socket
 * sharing 5353 and joined to 224.0.0.251, which not every environment allows. See
 * MulticastListener::usable().
 */
namespace mdnstest {

const quint16 kMdnsPort = 5353;
inline QHostAddress mdnsGroup() { return QHostAddress(QStringLiteral("224.0.0.251")); }

enum DnsType { DNS_A = 1, DNS_PTR = 12, DNS_TXT = 16, DNS_SRV = 33 };

struct DnsRecord {
    QByteArray name;
    quint16 type = 0;
    quint32 ttl = 0;
    /// PTR target, or SRV target - both are names.
    QByteArray target;
    /// SRV only.
    quint16 port = 0;
    /// TXT only, one entry per "key=value" string.
    QList<QByteArray> txt;
    /// A only.
    QHostAddress address;

    QByteArray txtValue(const QByteArray &key) const {
        for (const QByteArray &entry : txt) {
            const int eq = entry.indexOf('=');
            if (eq > 0 && entry.left(eq) == key)
                return entry.mid(eq + 1);
        }
        return QByteArray();
    }
};

struct DnsMessage {
    bool response = false;
    QList<DnsRecord> records;

    QList<DnsRecord> ofType(quint16 type) const {
        QList<DnsRecord> out;
        for (const DnsRecord &r : records)
            if (r.type == type)
                out.append(r);
        return out;
    }
};

/// "_wahoo-fitness-tnp._tcp.local." -> length-prefixed labels, terminated by a zero byte.
inline QByteArray encodeName(const QByteArray &name) {
    QByteArray out;
    for (const QByteArray &label : name.split('.')) {
        if (label.isEmpty())
            continue;
        out.append(static_cast<char>(label.size()));
        out.append(label);
    }
    out.append('\0');
    return out;
}

/**
 * @brief Read a name at @p offset, following compression pointers.
 *
 * @param offset Advanced past the name as it appears here - for a pointer that is two
 *        bytes, however long the name it expands to.
 * @return The name with a trailing dot, matching how qmdnsengine spells its record names.
 */
inline QByteArray decodeName(const QByteArray &packet, int &offset) {
    QByteArray out;
    int here = offset;
    bool jumped = false;
    // A malformed packet can point in a circle; every label consumes at least one byte of
    // a bounded packet, so this cannot spin.
    for (int guard = 0; guard < 128; ++guard) {
        if (here < 0 || here >= packet.size())
            break;
        const quint8 len = static_cast<quint8>(packet.at(here));
        if ((len & 0xC0) == 0xC0) {
            if (here + 1 >= packet.size())
                break;
            const int pointer =
                ((len & 0x3F) << 8) | static_cast<quint8>(packet.at(here + 1));
            if (!jumped)
                offset = here + 2;
            jumped = true;
            here = pointer;
            continue;
        }
        if (len == 0) {
            if (!jumped)
                offset = here + 1;
            break;
        }
        if (here + 1 + len > packet.size())
            break;
        out.append(packet.mid(here + 1, len));
        out.append('.');
        here += 1 + len;
    }
    return out;
}

/// A standard query: one question, no answers, recursion off.
inline QByteArray buildQuery(const QByteArray &name, quint16 type) {
    QByteArray out;
    const quint16 header[6] = {0x1234, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000};
    for (quint16 field : header) {
        out.append(static_cast<char>(field >> 8));
        out.append(static_cast<char>(field & 0xFF));
    }
    out.append(encodeName(name));
    out.append(static_cast<char>(type >> 8));
    out.append(static_cast<char>(type & 0xFF));
    out.append(static_cast<char>(0x00));
    out.append(static_cast<char>(0x01)); // class IN
    return out;
}

/**
 * @brief A response carrying one SRV record - used to hand a responder its own claim back.
 *
 * `Record::operator==` compares name, type, target, priority, weight and port among other
 * things, but not TTL, so a record built here with priority and weight left at zero is
 * byte-for-byte the claim `qmdnsengine` proposes for a service. That is what makes it
 * possible to test the rule that a responder must not treat its own record as a competitor.
 */
inline QByteArray buildSrvResponse(const QByteArray &name, const QByteArray &target,
                                   quint16 port) {
    QByteArray rdata;
    const quint16 fields[3] = {0, 0, port}; // priority, weight, port
    for (quint16 field : fields) {
        rdata.append(static_cast<char>(field >> 8));
        rdata.append(static_cast<char>(field & 0xFF));
    }
    rdata.append(encodeName(target));

    QByteArray out;
    // Response, authoritative; no questions, one answer.
    const quint16 header[6] = {0x0000, 0x8400, 0x0000, 0x0001, 0x0000, 0x0000};
    for (quint16 field : header) {
        out.append(static_cast<char>(field >> 8));
        out.append(static_cast<char>(field & 0xFF));
    }
    out.append(encodeName(name));
    out.append(static_cast<char>(0x00));
    out.append(static_cast<char>(DNS_SRV));
    out.append(static_cast<char>(0x00));
    out.append(static_cast<char>(0x01)); // class IN
    const quint32 ttl = 4500;
    for (int shift = 24; shift >= 0; shift -= 8)
        out.append(static_cast<char>((ttl >> shift) & 0xFF));
    out.append(static_cast<char>(rdata.size() >> 8));
    out.append(static_cast<char>(rdata.size() & 0xFF));
    out.append(rdata);
    return out;
}

inline quint16 readU16(const QByteArray &p, int at) {
    return quint16((static_cast<quint8>(p.at(at)) << 8) | static_cast<quint8>(p.at(at + 1)));
}

/// Parse a response: header, then the questions skipped, then every resource record in the
/// answer, authority and additional sections alike - which section a record arrived in is
/// not something any assertion here depends on.
inline bool parseMessage(const QByteArray &packet, DnsMessage &out) {
    if (packet.size() < 12)
        return false;
    out.response = (readU16(packet, 2) & 0x8000) != 0;
    const int counts[4] = {readU16(packet, 4), readU16(packet, 6), readU16(packet, 8),
                           readU16(packet, 10)};
    int offset = 12;
    for (int i = 0; i < counts[0]; ++i) {
        decodeName(packet, offset);
        offset += 4; // qtype, qclass
    }
    const int total = counts[1] + counts[2] + counts[3];
    for (int i = 0; i < total; ++i) {
        if (offset + 10 > packet.size())
            return false;
        DnsRecord record;
        record.name = decodeName(packet, offset);
        if (offset + 10 > packet.size())
            return false;
        record.type = readU16(packet, offset);
        offset += 4; // type, class
        record.ttl = (quint32(readU16(packet, offset)) << 16) | readU16(packet, offset + 2);
        offset += 4;
        const int rdlength = readU16(packet, offset);
        offset += 2;
        if (offset + rdlength > packet.size())
            return false;
        const int rdataStart = offset;

        switch (record.type) {
        case DNS_PTR: {
            int at = rdataStart;
            record.target = decodeName(packet, at);
            break;
        }
        case DNS_SRV: {
            record.port = readU16(packet, rdataStart + 4);
            int at = rdataStart + 6;
            record.target = decodeName(packet, at);
            break;
        }
        case DNS_TXT: {
            int at = rdataStart;
            while (at < rdataStart + rdlength) {
                const int len = static_cast<quint8>(packet.at(at));
                if (len == 0 || at + 1 + len > rdataStart + rdlength)
                    break;
                record.txt.append(packet.mid(at + 1, len));
                at += 1 + len;
            }
            break;
        }
        case DNS_A:
            if (rdlength == 4) {
                record.address = QHostAddress(
                    (static_cast<quint32>(static_cast<quint8>(packet.at(rdataStart))) << 24) |
                    (static_cast<quint32>(static_cast<quint8>(packet.at(rdataStart + 1))) << 16) |
                    (static_cast<quint32>(static_cast<quint8>(packet.at(rdataStart + 2))) << 8) |
                    static_cast<quint32>(static_cast<quint8>(packet.at(rdataStart + 3))));
            }
            break;
        default:
            break;
        }
        offset = rdataStart + rdlength;
        out.records.append(record);
    }
    return true;
}

inline void turnEventLoop(int ms) {
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/**
 * @brief Asks on the multicast group from an ephemeral port, and is answered unicast.
 *
 * The asymmetry is deliberate and it is what makes this reliable.
 *
 * **Asking** has to go to the group. A unicast datagram to 127.0.0.1:5353 is delivered to
 * exactly one of the sockets sharing that port, and on any real desktop QZ is not alone
 * there - Bonjour's mDNSResponder, Windows' Dnscache and adb were all holding 5353 on the
 * machine this was written on. Multicast is delivered to every socket joined to the group,
 * so it is the only way to be sure the question reaches QZ.
 *
 * **Being answered** comes back to this socket alone, because `Message::reply()` only
 * answers on the group when the query arrived from port 5353. So receiving needs no join,
 * no shared bind, and no competition with the other responders on the host.
 */
class Browser {
  public:
    bool open() { return m_sock.bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0)); }

    /**
     * @brief Ask for @p name / @p type and collect every reply that arrives.
     *
     * The question goes out every multicast-capable interface, for the same reason QZ
     * answers on every one: a socket bound to the any-address sends to whichever single
     * interface the OS picks for the group, and on a host with a virtual adapter that is
     * regularly not where the other end is listening.
     *
     * More than one reply is normal rather than exceptional - the default profile runs two
     * DIRCON endpoints, each with its own provider, and both answer a query for the type.
     */
    /// Put a datagram on the group, out of every interface that can carry it.
    void sendRaw(const QByteArray &packet) {
        const QNetworkInterface previous = m_sock.multicastInterface();
        const auto interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &iface : interfaces) {
            if (!(iface.flags() & QNetworkInterface::CanMulticast) ||
                !(iface.flags() & QNetworkInterface::IsUp)) {
                continue;
            }
            m_sock.setMulticastInterface(iface);
            m_sock.writeDatagram(packet, mdnsGroup(), kMdnsPort);
        }
        m_sock.setMulticastInterface(previous);
        m_sock.writeDatagram(packet, mdnsGroup(), kMdnsPort);
    }

    QList<DnsMessage> ask(const QByteArray &name, quint16 type, int ms = 2000) {
        sendRaw(buildQuery(name, type));
        QList<DnsMessage> out;
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (m_sock.hasPendingDatagrams()) {
                QByteArray packet;
                packet.resize(int(m_sock.pendingDatagramSize()));
                m_sock.readDatagram(packet.data(), packet.size());
                DnsMessage message;
                if (parseMessage(packet, message) && message.response)
                    out.append(message);
            }
        }
        return out;
    }

    /**
     * @brief Ask repeatedly until something answers.
     *
     * The provider stays silent until a probe has confirmed its name is unique, which takes
     * a couple of seconds after the endpoint comes up. Retrying is the honest way to wait
     * for that: it is also exactly what a browser does.
     */
    QList<DnsMessage> askUntilAnswered(const QByteArray &name, quint16 type, int ms = 12000) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            const QList<DnsMessage> replies = ask(name, type, 700);
            if (!replies.isEmpty())
                return replies;
        }
        return QList<DnsMessage>();
    }

  private:
    QUdpSocket m_sock;
};

/**
 * @brief Listens on the multicast group, for the things only multicast carries:
 * announcements, goodbyes, and replies to a query that came from port 5353.
 *
 * Sharing UDP 5353 with QZ's own responder is deliberate and supported - `qmdnsengine`
 * binds the same way, and on a Windows desktop `Dnscache` is usually there too.
 */
class MulticastListener {
  public:
    /// @return false if this environment will not let a second socket share 5353 or join
    ///         the group - in which case nothing multicast can be asserted here at all.
    bool open() {
        if (!bindShared())
            return false;
        const auto interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &iface : interfaces) {
            if (!(iface.flags() & QNetworkInterface::CanMulticast) ||
                !(iface.flags() & QNetworkInterface::IsUp)) {
                continue;
            }
            if (m_sock.joinMulticastGroup(mdnsGroup(), iface))
                ++m_joined;
        }
        // A join on the default interface as well, for the case where the enumeration above
        // found nothing usable but the stack still has a route for the group.
        if (m_sock.joinMulticastGroup(mdnsGroup()))
            ++m_joined;
        return m_joined > 0;
    }

    /**
     * @brief Join the group on exactly one interface, and hear only what arrives there.
     *
     * This is what makes "the answer reached loopback" a real assertion rather than "the
     * answer reached somewhere": a listener joined everywhere is satisfied by a reply that
     * went out the Wi-Fi adapter, which is precisely the failure being tested for.
     */
    bool openOn(const QNetworkInterface &iface) {
        if (!bindShared())
            return false;
        if (m_sock.joinMulticastGroup(mdnsGroup(), iface))
            ++m_joined;
        return m_joined > 0;
    }

    /// How many interfaces the group was joined on.
    int joined() const { return m_joined; }

    /// Send a query the way a real browser does - from port 5353 - so the answer comes back
    /// on the group rather than to us alone.
    void askOnTheGroup(const QByteArray &name, quint16 type) {
        m_sock.writeDatagram(buildQuery(name, type), mdnsGroup(), kMdnsPort);
    }

    /// Everything heard in the next @p ms, parsed. Includes our own queries coming back.
    QList<DnsMessage> listen(int ms) {
        QList<DnsMessage> out;
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (m_sock.hasPendingDatagrams()) {
                QByteArray packet;
                packet.resize(int(m_sock.pendingDatagramSize()));
                m_sock.readDatagram(packet.data(), packet.size());
                DnsMessage message;
                if (parseMessage(packet, message))
                    out.append(message);
            }
        }
        return out;
    }

    void drain() { listen(50); }

  private:
    bool bindShared() {
        return m_sock.bind(QHostAddress(QHostAddress::AnyIPv4), kMdnsPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }

    QUdpSocket m_sock;
    int m_joined = 0;
};

/// The loopback interface, if it can carry multicast at all. Windows' loopback
/// pseudo-interface can; Linux's `lo` normally does not set IFF_MULTICAST, so this returns
/// an invalid interface there and the caller has to say so rather than assert nothing.
inline QNetworkInterface multicastLoopback() {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if ((iface.flags() & QNetworkInterface::IsLoopBack) &&
            (iface.flags() & QNetworkInterface::CanMulticast) &&
            (iface.flags() & QNetworkInterface::IsUp)) {
            return iface;
        }
    }
    return QNetworkInterface();
}

/// Every record of @p type across @p messages, flattened.
inline QList<DnsRecord> allRecords(const QList<DnsMessage> &messages, quint16 type) {
    QList<DnsRecord> out;
    for (const DnsMessage &m : messages)
        out.append(m.ofType(type));
    return out;
}

} // namespace mdnstest
