#include "webserverinfosender.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QtWebSockets/QWebSocket>

WebServerInfoSender::WebServerInfoSender(const QString &id, QObject *parent) : TemplateInfoSender(id, parent) {
}
WebServerInfoSender::~WebServerInfoSender() { innerStop(); }

bool WebServerInfoSender::listen() {
    if (!innerTcpServer) {
        innerTcpServer = new QTcpServer(this);
        connect(innerTcpServer, SIGNAL(acceptError(QAbstractSocket::SocketError)), this, SLOT(acceptError(QAbstractSocket::SocketError)));
    }
    if (!innerTcpServer->isListening()) {
        if (innerTcpServer->listen(QHostAddress::Any, port)) {
            if (!port) {
                settings.setValue(QStringLiteral("template_") + templateId + QStringLiteral("_port"),
                                  port = innerTcpServer->serverPort());
            }
            httpServer->bind(innerTcpServer);

            connect(&watchdogTimer, SIGNAL(timeout()), this, SLOT(watchdogEvent()));
            watchdogTimer.start(5000);

            return true;
        } else {
            delete innerTcpServer;
            innerTcpServer = 0;
        }
    }
    return false;
}

void WebServerInfoSender::acceptError(QAbstractSocket::SocketError socketError) {qDebug() << "WebServerInfoSender::acceptError" << socketError;}
bool WebServerInfoSender::isRunning() const { return innerTcpServer && innerTcpServer->isListening(); }
bool WebServerInfoSender::send(const QString &data) {
    if (isRunning() && !data.isEmpty()) {
        QMutexLocker locker(&clientsMutex);
        bool rv = true, oldrv = false;
        for (auto it = sendToClients.begin(); it != sendToClients.end();) {
            if (it->isNull()) {
                // Remove null pointers
                it = sendToClients.erase(it);
            } else {
                QWebSocket *client = it->data();
                if (client) {
                    rv = client->sendTextMessage(data) > 0;
                    if (!oldrv)
                        oldrv = rv;
                }
                ++it;
            }
        }
        return rv;
    } else
        return false;
}

void WebServerInfoSender::innerStop() {
    if (innerTcpServer) {
        if (isRunning())
            innerTcpServer->close();
        httpServer->deleteLater();
        clients.clear();
        sendToClients.clear();
        innerTcpServer = 0;
        httpServer = 0;
    }

    // Clear all collections
    QMutexLocker locker(&clientsMutex);
    clients.clear();
    sendToClients.clear();
}

bool WebServerInfoSender::init() {
    bool ok;
    port = settings.value(QStringLiteral("template_") + templateId + QStringLiteral("_port"), 6666).toInt(&ok);
    if (!ok)
        port = 6666;
    if (!httpServer)
        httpServer = new QHttpServer(this);
    if (listen()) {
        qDebug() << QStringLiteral("WebServer listening on port") << port;
        connect(httpServer, SIGNAL(newWebSocketConnection()), this, SLOT(onNewConnection()));
        return true;
    }
    reinit();
    return false;
}

void WebServerInfoSender::watchdogEvent() {
    if(innerTcpServer->serverError() != QAbstractSocket::UnknownSocketError)
        qDebug() << "WebServerInfoSender is " << innerTcpServer->serverError();
    if(innerTcpServer && !innerTcpServer->isListening()) {
        qDebug() << QStringLiteral("innerTcpServer is not LISTENING!");
    }
}

void WebServerInfoSender::processTextMessage(QString message) {
    /*QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        pClient->sendTextMessage(message);
    }*/
    //qDebug() << QStringLiteral("Message received:") << message;
    emit onDataReceived(message.toUtf8());
}

void WebServerInfoSender::onNewConnection() {
    // Qt 5's qt-labs QHttpServer handed back a raw QWebSocket*; Qt 6's module
    // returns a unique_ptr, so ownership has to be taken explicitly. Everything
    // below - and the clients list - still works on a raw pointer.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QWebSocket *pSocket = httpServer->nextPendingWebSocketConnection().release();
#else
    QWebSocket *pSocket = httpServer->nextPendingWebSocketConnection();
#endif
    QUrl requestUrl = pSocket->requestUrl();
    qDebug() << QStringLiteral("WebSocket connection") << requestUrl;

    QMutexLocker locker(&clientsMutex);
    
    // Handle different types of WebSocket connections based on the path
    connect(pSocket, SIGNAL(textMessageReceived(QString)), this, SLOT(processTextMessage(QString)));
    connect(pSocket, SIGNAL(binaryMessageReceived(QByteArray)), this, SLOT(processBinaryMessage(QByteArray)));
    sendToClients << QPointer<QWebSocket>(pSocket);
    connect(pSocket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));

    // Store the WebSocket connection
    clients << QPointer<QWebSocket>(pSocket);
}

void WebServerInfoSender::socketDisconnected() {
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    qDebug() << QStringLiteral("socketDisconnected:") << pClient;
    if (pClient) {
        QMutexLocker locker(&clientsMutex);
        qDebug() << QStringLiteral("socketDisconnected:") << clients.size();
        
        // Remove from sendToClients (QPointer)
        for (auto it = sendToClients.begin(); it != sendToClients.end();) {
            if (it->isNull() || it->data() == pClient) {
                it = sendToClients.erase(it);
            } else {
                ++it;
            }
        }
        qDebug() << QStringLiteral("socketDisconnected: sendToClients removed");
        
        // Remove from clients (QPointer)
        for (auto it = clients.begin(); it != clients.end();) {
            if (it->isNull() || it->data() == pClient) {
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
        
        qDebug() << QStringLiteral("socketDisconnected: cleanup completed");
    }
}

void WebServerInfoSender::processBinaryMessage(QByteArray message) {
    /*QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        pClient->sendBinaryMessage(message);
    }*/
    //qDebug() << QStringLiteral("Binary Message received:") << message.toHex();
    emit onDataReceived(message);
}
