// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>

#include "controller.hpp"
#include "coverart.hpp"
#include "discordipc.hpp"
#include "listener.hpp"
#include "settings.hpp"

using namespace riftwii;
using namespace riftwii::rpc;

namespace {

const QHostAddress kWii(QStringLiteral("192.168.1.30"));
constexpr qint64 kT0 = 1'800'000'000;

PresenceMessage game(const char* id, const char* console = "aaaa0001") {
    PresenceMessage m;
    m.event = PresenceEvent::Game;
    m.console = console;
    m.version = "2610-541";
    m.game_id = id;
    m.title = "Mario Kart Wii";
    m.platform = "wii";
    return m;
}

PresenceMessage menu(const char* console = "aaaa0001") {
    PresenceMessage m;
    m.event = PresenceEvent::Menu;
    m.console = console;
    m.next_seconds = kPresenceMenuSeconds;
    return m;
}

PresenceMessage bye() {
    PresenceMessage m;
    m.event = PresenceEvent::Bye;
    m.console = "aaaa0001";
    return m;
}

Settings defaults() {
    Settings s;
    s.timeoutMinutes = 30;
    return s;
}

}  // namespace

class RpcTests : public QObject {
    Q_OBJECT
private slots:
    // A game stays until its timeout when nothing more is heard: the Wii
    // was switched off from the game.
    void gameEndsAtTimeout() {
        Controller c;
        c.setSettings(defaults());
        c.handle(game("RMCP01"), kWii, kT0);
        QCOMPARE(c.state(), Controller::State::Game);
        QCOMPARE(c.activity().value("details").toString(), QStringLiteral("Mario Kart Wii"));
        QCOMPARE(c.activity().value("timestamps").toObject().value("start").toInteger(), kT0);
        c.tick(kT0 + 29 * 60);
        QCOMPARE(c.state(), Controller::State::Game);
        c.tick(kT0 + 30 * 60);
        QCOMPARE(c.state(), Controller::State::Idle);
        QVERIFY(c.activity().isEmpty());
    }

    // RiftWii back in its menu means the game has ended.
    void menuEndsGame() {
        Controller c;
        c.setSettings(defaults());
        c.handle(game("RMCP01"), kWii, kT0);
        c.handle(menu(), kWii, kT0 + 600);
        QCOMPARE(c.state(), Controller::State::Menu);
        QCOMPARE(c.activity().value("details").toString(), QStringLiteral("In the RiftWii menu"));
        // The menu goes when it stops saying it is there.
        c.tick(kT0 + 600 + 89);
        QCOMPARE(c.state(), Controller::State::Menu);
        c.tick(kT0 + 600 + 91);
        QCOMPARE(c.state(), Controller::State::Idle);
    }

    void menuHiddenClears() {
        Controller c;
        Settings s = defaults();
        s.showMenu = false;
        c.setSettings(s);
        c.handle(menu(), kWii, kT0);
        QCOMPARE(c.state(), Controller::State::Idle);
        c.handle(game("RMCP01"), kWii, kT0);
        c.handle(menu(), kWii, kT0 + 60);
        QCOMPARE(c.state(), Controller::State::Idle);
    }

    void byeClears() {
        Controller c;
        c.setSettings(defaults());
        c.handle(game("RMCP01"), kWii, kT0);
        c.handle(bye(), kWii, kT0 + 5);
        QCOMPARE(c.state(), Controller::State::Idle);
    }

    // The three copies of one message are one start; a Reset keeps the
    // time; a new game starts the clock again.
    void copiesResetsAndNewGames() {
        Controller c;
        c.setSettings(defaults());
        QSignalSpy published(&c, &Controller::activityChanged);
        c.handle(game("RMCP01"), kWii, kT0);
        c.handle(game("RMCP01"), kWii, kT0 + 1);
        c.handle(game("RMCP01"), kWii, kT0 + 1);
        QCOMPARE(published.count(), qsizetype(1));
        PresenceMessage reset = game("RMCP01");
        reset.restart = true;
        c.handle(reset, kWii, kT0 + 900);
        QCOMPARE(c.startedAt(), kT0);
        c.handle(game("SB4P01"), kWii, kT0 + 1000);
        QCOMPARE(c.startedAt(), kT0 + 1000);
        QCOMPARE(c.gameId(), QStringLiteral("SB4P01"));
    }

    // Another Wii's menu does not end this one's game; "only this Wii"
    // leaves other Wiis out altogether.
    void severalWiis() {
        Controller c;
        c.setSettings(defaults());
        c.handle(game("RMCP01", "aaaa0001"), kWii, kT0);
        c.handle(menu("bbbb0002"), QHostAddress("192.168.1.31"), kT0 + 10);
        QCOMPARE(c.state(), Controller::State::Game);
        QCOMPARE(c.wiis().size(), qsizetype(2));

        // Made-up console tags from anyone on the network stop at 32.
        for (int i = 0; i < 100; ++i) {
            const std::string tag = "f" + std::to_string(1000000 + i);
            c.handle(menu(tag.c_str()), QHostAddress("192.168.1.99"), kT0 + 20);
        }
        QCOMPARE(c.wiis().size(), qsizetype(32));
        QCOMPARE(c.state(), Controller::State::Game);

        Controller only;
        Settings s = defaults();
        s.console = QStringLiteral("bbbb0002");
        only.setSettings(s);
        only.handle(game("RMCP01", "aaaa0001"), kWii, kT0);
        QCOMPARE(only.state(), Controller::State::Idle);
    }

    // What reaches Discord: mods under the name, the cover when there is
    // one, every text 2-128 characters, no game ID that is not one.
    void activityShape() {
        Controller c;
        c.setSettings(defaults());
        PresenceMessage m = game("RMCP01");
        m.mods = {"CTGP Revolution", "Retro Rewind"};
        c.handle(m, kWii, kT0);
        QCOMPARE(c.activity().value("state").toString(),
                 QStringLiteral("Wii · CTGP Revolution, Retro Rewind"));
        QCOMPARE(c.activity().value("buttons").toArray().size(), qsizetype(1));
        const QString cover = QStringLiteral("https://art.gametdb.com/wii/cover/EN/RMCP01.png");
        c.setCover(QStringLiteral("RMCP01"), cover);
        const QJsonObject assets = c.activity().value("assets").toObject();
        QCOMPARE(assets.value("large_image").toString(), cover);
        QVERIFY(assets.value("small_image").toString().startsWith("https://"));

        PresenceMessage longTitle = game("SB4P01");
        longTitle.title = std::string(300, 'x');
        c.handle(longTitle, kWii, kT0 + 100);
        QCOMPARE(c.activity().value("details").toString().size(), qsizetype(128));

        Controller bad;
        bad.setSettings(defaults());
        bad.handle(game("../../x"), kWii, kT0);
        QCOMPARE(bad.state(), Controller::State::Idle);

        PresenceMessage vc = game("NAKE");
        vc.platform = "vc";
        vc.system = "Nintendo 64";
        bad.handle(vc, kWii, kT0);
        QCOMPARE(bad.activity().value("state").toString(), QStringLiteral("Virtual Console (Nintendo 64)"));
    }

    void timeoutChoicesAreRiiTagRpcs() {
        const QVector<int> choices = timeoutChoices();
        QCOMPARE(choices.front(), 10);
        QCOMPARE(choices.back(), 720);
        QCOMPARE(choices.size(), qsizetype(72));
        QCOMPARE(timeoutLabel(10), QStringLiteral("10 minutes"));
        QCOMPARE(timeoutLabel(60), QStringLiteral("1 hour"));
        QCOMPARE(timeoutLabel(150), QStringLiteral("2 hours 30 minutes"));
    }

    void discordFrame() {
        const QByteArray f = DiscordIpc::frame(1, QJsonObject{{"cmd", "SET_ACTIVITY"}});
        QCOMPARE(qFromLittleEndian<quint32>(f.constData()), 1u);
        const quint32 length = qFromLittleEndian<quint32>(f.constData() + 4);
        QCOMPARE(int(length), f.size() - 8);
        QCOMPARE(QJsonDocument::fromJson(f.mid(8)).object().value("cmd").toString(), QStringLiteral("SET_ACTIVITY"));
    }

    void coverLinks() {
        QCOMPARE(CoverArt::regionFor("RMCE01"), QStringLiteral("US"));
        QCOMPARE(CoverArt::regionFor("RMCP01"), QStringLiteral("EN"));
        QCOMPARE(CoverArt::regionFor("RMCJ01"), QStringLiteral("JA"));
        const QStringList links = CoverArt::candidates("RMCP01");
        QCOMPARE(links.front(), QStringLiteral("https://art.gametdb.com/wii/cover/EN/RMCP01.png"));
        QVERIFY(links.contains(QStringLiteral("https://art.gametdb.com/wii/cover/US/RMCP.png")));
        QCOMPARE(links.size(), qsizetype(2 * 2 * 3));  // EN is both the region and a fallback
    }

    // A message sent on the loopback reaches the listener whole, and
    // something that is not one does not.
    void listenerHearsMessages() {
        Listener listener;
        QString error;
        QVERIFY2(listener.start(0, error), qPrintable(error));
        QSignalSpy heard(&listener, &Listener::received);
        QUdpSocket sender;
        sender.writeDatagram("not a message", QHostAddress::LocalHost, listener.port());
        const std::string packet = presence_packet(game("RMCP01"));
        sender.writeDatagram(QByteArray::fromStdString(packet), QHostAddress::LocalHost, listener.port());
        QVERIFY(heard.wait(3000));
        QCOMPARE(heard.count(), qsizetype(1));
        const auto message = heard.at(0).at(0).value<PresenceMessage>();
        QCOMPARE(QString::fromStdString(message.game_id), QStringLiteral("RMCP01"));
    }
};

QTEST_GUILESS_MAIN(RpcTests)
#include "rpc_tests.moc"
