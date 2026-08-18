#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>

#include <memory>

#include "Devices/DirconTestClient.h"
#include "Devices/MdnsTestClient.h"
#include "Tools/testsettings.h"
#include "devices/dircon/dirconmanager.h"
#include "devices/simulatedbike/simulatedbike.h"
#include "qzsettings.h"

#ifndef QZ_RIDE_FIXTURES
#error "QZ_RIDE_FIXTURES must name the directory holding the .ride fixtures"
#endif

/**
 * Layer C of docs/fork/VIRTUAL-BIKE.md, phase 4: discovery.
 *
 * Phases 2 and 3 assume a client has already found QZ. This is the half that gets it there,
 * and it is the half with the worst history in this fork: announcements went out on one
 * interface, loopback queries went unanswered, the service type was not fully qualified, QZ
 * renamed its own service every few seconds by mistaking its own announcement for a
 * competing claim, the SRV target carried a smuggled space, and no goodbye was sent on quit.
 * Every one of those was found by a real training app refusing to connect.
 *
 * The browser here shares no code with `qmdnsengine` - see `MdnsTestClient.h` for why that
 * matters more here than anywhere else in Layer C.
 *
 * ## Why these run in the Rouvy profile
 *
 * `rouvy_compatibility` builds exactly one DIRCON endpoint, and therefore one mDNS provider
 * announcing one service. The default profile builds two - the bike and the heart rate
 * monitor - and both answer a query for the type, so every assertion would have to sort two
 * interleaved replies before it could say anything. That sorting would be test machinery
 * with no product behind it.
 *
 * The records are built by the same code in both profiles, so nothing about mDNS goes
 * untested by choosing the unambiguous one - and it is the profile this fork actually ships
 * to Rouvy and MyWhoosh users, with the name and MAC those clients see.
 *
 * ## Which assertions need multicast, and what happens when it is missing
 *
 * Announcements and goodbyes only exist as multicast, and so do replies to a query sent from
 * port 5353 the way a real browser sends it. The tests that need those probe the capability
 * first, using the same socket and the same group: if an *announcement* cannot be heard,
 * nothing multicast can be, and the test skips with a reason rather than passing quietly.
 * The probe cannot mask the bug it is guarding, because a provider that never announces is
 * itself the failure.
 */
namespace {

using namespace mdnstest;
using dircontest::kBasePort;
using dircontest::kBikePort;

const char *const kDiscoveryRide = QZ_RIDE_FIXTURES "/steady.ride";

/// The service type, fully qualified. A query for this exact name is the assertion: record
/// names parsed off the wire always end in a dot, and `onMessageReceived()` compares them
/// exactly, so a type published without it answers nothing at all.
const QByteArray kServiceType = QByteArrayLiteral("_wahoo-fitness-tnp._tcp.local.");

/// `dircon_id` names the instance, and these tests pick one no shipped default produces -
/// the Rouvy profile turns an id of 0 into 1234, so a developer running QZ on the same
/// machine announces "ELITE AVANTI 01234 W". Two responders claiming one name is a genuine
/// conflict, and the loser renames itself to "...-2": the tests would fail for a reason
/// that has nothing to do with the code. Same argument as the port, one line above.
const int kDirconId = 4321;
/// Spaces and all - a service instance name is allowed them, which is exactly why the SRV
/// target must not simply borrow it.
const QByteArray kInstanceName = QByteArrayLiteral("ELITE AVANTI 04321 W");
const QByteArray kInstanceFqdn = kInstanceName + "." + kServiceType;
const QByteArray kSrvTarget = QByteArrayLiteral("ELITE-AVANTI-04321-W.local.");
/// The Rouvy profile synthesises the MAC from the id rather than reading an adapter:
/// 24:DC:C3:E3:B5:xx, with xx the low byte of 4321.
const QByteArray kMacAddress = QByteArrayLiteral("24:DC:C3:E3:B5:E1");
const QByteArray kSerialNumber = QByteArrayLiteral("04321");

class DirconDiscovery : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Dircon Discovery Test"};
    std::unique_ptr<simulatedbike> m_bike;

    void SetUp() override {
        testSettings.activate();

        testSettings.qsettings.setValue(QZSettings::dircon_yes, true);
        testSettings.qsettings.setValue(QZSettings::dircon_server_base_port, kBasePort);
        testSettings.qsettings.setValue(QZSettings::rouvy_compatibility, true);
        testSettings.qsettings.setValue(QZSettings::dircon_id, kDirconId);
        testSettings.qsettings.setValue(QZSettings::zwift_play_emulator, false);
        testSettings.qsettings.setValue(QZSettings::race_mode, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);

        DirconManager::releaseShared();
    }

    void TearDown() override {
        DirconManager::releaseShared();
        m_bike.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    void startEndpointWithBike() {
        m_bike.reset(new simulatedbike(false, false, QString::fromUtf8(kDiscoveryRide)));
        DirconManager::shared(m_bike.get(), 4, 1.0);
        turnEventLoop(200);
    }

    /// The PTR answer for the service type, once the provider has confirmed its name.
    /// Everything here needs it, and it is the first thing that breaks.
    static QList<DnsMessage> browse(Browser &browser) {
        return browser.askUntilAnswered(kServiceType, DNS_PTR);
    }
};

// ---------------------------------------------------------------------------------------
// The record, over loopback
// ---------------------------------------------------------------------------------------

TEST_F(DirconDiscovery, TheServiceAnswersAQueryForItsFullyQualifiedType) {
    startEndpointWithBike();

    Browser browser;
    ASSERT_TRUE(browser.open());

    const QList<DnsMessage> replies = browse(browser);
    ASSERT_FALSE(replies.isEmpty())
        << "no answer to a PTR query for " << kServiceType.constData()
        << " - either the type is published without its trailing dot, so the record name "
           "matches no query, or the provider never confirmed its name";

    const QList<DnsRecord> ptrs = allRecords(replies, DNS_PTR);
    ASSERT_FALSE(ptrs.isEmpty());

    bool found = false;
    for (const DnsRecord &ptr : ptrs)
        if (ptr.target == kInstanceFqdn)
            found = true;
    EXPECT_TRUE(found) << "the PTR did not point at " << kInstanceFqdn.constData();
}

TEST_F(DirconDiscovery, TheSrvTargetIsALegalHostnameAndPointsAtTheListeningPort) {
    startEndpointWithBike();

    Browser browser;
    ASSERT_TRUE(browser.open());
    const QList<DnsMessage> replies = browse(browser);
    ASSERT_FALSE(replies.isEmpty());

    const QList<DnsRecord> srvs = allRecords(replies, DNS_SRV);
    ASSERT_FALSE(srvs.isEmpty()) << "a PTR answer must carry the SRV with it, or a browser "
                                    "has to ask again for something we already knew";

    const DnsRecord &srv = srvs.first();
    EXPECT_EQ(kInstanceFqdn, srv.name);

    // The instance name has spaces in it and is allowed them. A hostname is not, and the
    // target used to borrow the instance name unchanged. MyWhoosh keys its pending-service
    // map on the instance name with spaces hyphenated and looks it up by the SRV target, so
    // a target carrying a space missed the lookup and the device was dropped in silence.
    EXPECT_EQ(kSrvTarget, srv.target);
    EXPECT_FALSE(srv.target.contains(' ')) << "the SRV target smuggled a space through";

    // ...and it has to point at the port phase 2 connects to, or discovery leads nowhere.
    EXPECT_EQ(kBikePort, srv.port);
}

TEST_F(DirconDiscovery, TheAnswerCarriesTheAddressAndTheDirconAttributes) {
    startEndpointWithBike();

    Browser browser;
    ASSERT_TRUE(browser.open());
    const QList<DnsMessage> replies = browse(browser);
    ASSERT_FALSE(replies.isEmpty());

    // The A record is published for the SRV target, and it used to be published empty:
    // `localipaddress::getIP` returned a null address on every desktop platform, so a
    // browser resolved the service to nothing at all.
    const QList<DnsRecord> as = allRecords(replies, DNS_A);
    ASSERT_FALSE(as.isEmpty()) << "no A record: the SRV target resolves to nothing";
    EXPECT_EQ(kSrvTarget, as.first().name);
    EXPECT_FALSE(as.first().address.isNull());
    EXPECT_NE(QHostAddress(QStringLiteral("0.0.0.0")), as.first().address);

    const QList<DnsRecord> txts = allRecords(replies, DNS_TXT);
    ASSERT_FALSE(txts.isEmpty());
    const DnsRecord &txt = txts.first();
    EXPECT_EQ(kInstanceFqdn, txt.name);

    // What a Wahoo-protocol client reads off the record. In the Rouvy profile the MAC is
    // synthesised from dircon_id rather than taken from an adapter, and only the fitness
    // machine service is advertised - 0x1818 and 0x1816 are filtered out.
    EXPECT_EQ(kMacAddress, txt.txtValue("mac-address"));
    EXPECT_EQ(kSerialNumber, txt.txtValue("serial-number"));
    EXPECT_EQ(QByteArrayLiteral("0x1826"), txt.txtValue("ble-service-uuids"));
}

TEST_F(DirconDiscovery, TheServiceNameSurvivesItsOwnRecordComingBack) {
    Browser browser;
    ASSERT_TRUE(browser.open());

    startEndpointWithBike();

    // Hand the responder its own claim back, over and over, while it is probing.
    //
    // This is the bug reproduced rather than waited for. QZ's own announcements always come
    // back to it - multicast loops back to the sending host, and any other responder sharing
    // UDP 5353 relays them too - and the prober used to count that as a competitor and bump
    // its suffix. The service then renamed itself against itself, every couple of seconds,
    // for ever: a browser that resolved one name found it gone moments later, and the name
    // never settled long enough to connect to.
    //
    // RFC 6762 8.2 is explicit that a record identical to the one being claimed is not a
    // conflict, and that is the whole fix. The record built here is byte-for-byte the SRV
    // the provider proposes - same name, same target, same port, priority and weight at
    // zero - so a responder that renames on this is renaming on itself.
    const QByteArray ownClaim = buildSrvResponse(kInstanceFqdn, kSrvTarget, kBikePort);
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        browser.sendRaw(ownClaim);
        turnEventLoop(150);
    }

    const QList<DnsMessage> replies = browse(browser);
    ASSERT_FALSE(replies.isEmpty())
        << "the service never settled on a name: it kept re-probing against its own record "
           "and so never confirmed one";
    const QList<DnsRecord> ptrs = allRecords(replies, DNS_PTR);
    ASSERT_FALSE(ptrs.isEmpty());

    // Un-suffixed. A responder that took the bait renames to "... W-2", then "-3", and the
    // instance a client resolved a moment ago no longer exists.
    EXPECT_EQ(kInstanceFqdn, ptrs.first().target) << "the service renamed itself";

    // ...and it stays where it is once the noise stops.
    turnEventLoop(4000);
    const QList<DnsMessage> later = browser.ask(kServiceType, DNS_PTR, 2000);
    ASSERT_FALSE(later.isEmpty()) << "the service stopped answering";
    const QList<DnsRecord> laterPtrs = allRecords(later, DNS_PTR);
    ASSERT_FALSE(laterPtrs.isEmpty());
    EXPECT_EQ(kInstanceFqdn, laterPtrs.first().target) << "the service renamed itself later";
}

TEST_F(DirconDiscovery, DiscoveryAnswersBeforeAnyBikeExists) {
    // The same process lifetime phase 2 asserts for the TCP listener. A client that caches
    // discovery results has to be able to find QZ from launch, or it caches nothing and
    // never comes back.
    DirconManager::startIdleEndpoint();
    turnEventLoop(200);
    ASSERT_NE(nullptr, DirconManager::sharedIfAny());
    ASSERT_EQ(nullptr, DirconManager::sharedIfAny()->device());

    Browser browser;
    ASSERT_TRUE(browser.open());
    const QList<DnsMessage> replies = browse(browser);
    ASSERT_FALSE(replies.isEmpty()) << "nothing announced until a bike turns up";

    const QList<DnsRecord> srvs = allRecords(replies, DNS_SRV);
    ASSERT_FALSE(srvs.isEmpty());
    EXPECT_EQ(kBikePort, srvs.first().port);
}

// ---------------------------------------------------------------------------------------
// The parts that only exist as multicast
// ---------------------------------------------------------------------------------------

/**
 * @brief Bring up an endpoint and confirm the listener can hear it announce.
 *
 * This is the capability probe described in the file comment. It cannot hide a regression:
 * the thing it checks for - an announcement on the group - is itself one of the behaviours
 * under test, so if it is missing there is nothing left to assert.
 */
#define REQUIRE_MULTICAST(listener)                                                            \
    do {                                                                                       \
        if (!(listener).open()) {                                                              \
            GTEST_SKIP() << "this environment will not let a second socket share UDP 5353 or " \
                            "join 224.0.0.251, so nothing multicast can be observed here";     \
        }                                                                                      \
    } while (0)

TEST_F(DirconDiscovery, ABrowserQueryIsAnsweredOnLoopback) {
    const QNetworkInterface loopback = multicastLoopback();
    if (!loopback.isValid()) {
        // Linux does not set IFF_MULTICAST on `lo`, so there is no loopback multicast to
        // assert on and QZ's send loop skips it there too. Saying so is better than
        // asserting something weaker and calling it this test.
        GTEST_SKIP() << "loopback here cannot carry multicast, so this fix is not observable";
    }

    MulticastListener listener;
    if (!listener.openOn(loopback))
        GTEST_SKIP() << "could not join 224.0.0.251 on loopback in this environment";

    startEndpointWithBike();

    // Wait for the provider to confirm - it answers nothing before that - over the group
    // query path, which does not depend on what is being tested here.
    Browser browser;
    ASSERT_TRUE(browser.open());
    ASSERT_FALSE(browse(browser).isEmpty());

    listener.drain();
    listener.askOnTheGroup(kServiceType, DNS_PTR);
    const QList<DnsMessage> heard = listener.listen(3000);

    // A query from port 5353 is answered on the group rather than back to the asker, and
    // the answer has to come out *this* interface. The listener is joined on loopback and
    // nowhere else on purpose: skipping loopback in the send loop is what left MyWhoosh
    // asking over ::1 and QZ answering on fe80::, which is not where it asked, and a
    // listener joined everywhere would be satisfied by that wrong answer.
    bool answered = false;
    for (const DnsRecord &ptr : allRecords(heard, DNS_PTR))
        if (ptr.target == kInstanceFqdn)
            answered = true;
    EXPECT_TRUE(answered) << "a browser-style query on this host was not answered on "
                             "loopback; "
                          << heard.size() << " messages heard there";
}

TEST_F(DirconDiscovery, AGoodbyeIsSentWhenTheEndpointGoesAway) {
    MulticastListener listener;
    REQUIRE_MULTICAST(listener);

    startEndpointWithBike();

    Browser browser;
    ASSERT_TRUE(browser.open());
    ASSERT_FALSE(browse(browser).isEmpty()) << "never announced, so a goodbye proves nothing";

    listener.drain();
    DirconManager::releaseShared();
    const QList<DnsMessage> heard = listener.listen(2500);

    // A goodbye is the same records with their TTL set to zero, which is how a browser is
    // told to drop them. Without it a client keeps a cached record pointing at a port
    // nobody is listening on - and Rouvy, which does not retry a failed connect, stays
    // broken until its cache is cleared by hand.
    bool farewell = false;
    for (const DnsRecord &ptr : allRecords(heard, DNS_PTR))
        if (ptr.target == kInstanceFqdn && ptr.ttl == 0)
            farewell = true;
    EXPECT_TRUE(farewell) << "no zero-TTL PTR after teardown; " << heard.size()
                          << " messages heard on the group";
}

TEST_F(DirconDiscovery, AnnouncementsGoOutOnEveryInterface) {
    // The fault this guards: a socket bound to the any-address sends a multicast datagram
    // out exactly one interface, whichever the OS picks for 224.0.0.251, and on a host with
    // a virtual adapter - VirtualBox, VMware, Docker, Hyper-V - that is regularly not the
    // interface the training app is on. Discovery then fails with no error anywhere.
    QList<QNetworkInterface> usable;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if ((iface.flags() & QNetworkInterface::CanMulticast) &&
            (iface.flags() & QNetworkInterface::IsUp) &&
            (iface.flags() & QNetworkInterface::IsRunning)) {
            usable.append(iface);
        }
    }

    if (usable.size() < 2) {
        // Honest rather than convenient: with one interface, sending to one interface and
        // sending to all of them are the same datagram, and no assertion here can tell them
        // apart. This is a real test on a multi-homed developer machine - which is where the
        // bug appeared - and a no-op on a single-homed CI runner.
        GTEST_SKIP() << "only " << usable.size()
                     << " multicast-capable interface(s): sending to one and sending to all "
                        "are indistinguishable here";
    }

    QList<QUdpSocket *> sockets;
    for (const QNetworkInterface &iface : usable) {
        QUdpSocket *sock = new QUdpSocket();
        if (!sock->bind(QHostAddress::AnyIPv4, kMdnsPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint) ||
            !sock->joinMulticastGroup(mdnsGroup(), iface)) {
            delete sock;
            continue;
        }
        sockets.append(sock);
    }
    if (sockets.size() < 2) {
        qDeleteAll(sockets);
        GTEST_SKIP() << "could not listen on two interfaces at once in this environment";
    }

    startEndpointWithBike();

    // The provider announces on confirmation and then repeats with a widening gap, so a few
    // seconds covers several chances for each listener to hear one.
    Browser browser;
    ASSERT_TRUE(browser.open());
    ASSERT_FALSE(browse(browser).isEmpty());
    turnEventLoop(3000);

    int heardOn = 0;
    for (QUdpSocket *sock : sockets) {
        bool heard = false;
        while (sock->hasPendingDatagrams()) {
            QByteArray packet;
            packet.resize(int(sock->pendingDatagramSize()));
            sock->readDatagram(packet.data(), packet.size());
            DnsMessage message;
            if (!parseMessage(packet, message))
                continue;
            for (const DnsRecord &ptr : message.ofType(DNS_PTR))
                if (ptr.target == kInstanceFqdn)
                    heard = true;
        }
        if (heard)
            ++heardOn;
    }
    const int listening = sockets.size();
    qDeleteAll(sockets);

    EXPECT_EQ(listening, heardOn)
        << "the announcement reached " << heardOn << " of " << listening
        << " interfaces: it is going out one interface rather than all of them";
}

} // namespace
