#ifndef WEBSERVERINFOSENDER_H
#define WEBSERVERINFOSENDER_H
#include "templateinfosender.h"
#include <QHttpServer>
#include <QMutex>
#include <QPointer>

class WebServerInfoSender : public TemplateInfoSender {
    Q_OBJECT
  public:
    WebServerInfoSender(const QString &id, QObject *parent = 0);
    virtual ~WebServerInfoSender();
    virtual bool isRunning() const;
    virtual bool send(const QString &data);

  private:
    QHttpServer *httpServer = 0;
    bool listen();
    QTimer watchdogTimer;

  protected:
    virtual void innerStop();
    int port = 0;
    QTcpServer *innerTcpServer = 0;
    virtual bool init();
    QList<QPointer<QWebSocket>> clients;
    QList<QPointer<QWebSocket>> sendToClients;
    mutable QMutex clientsMutex;
  private slots:
    void acceptError(QAbstractSocket::SocketError socketError);
    void watchdogEvent();
    void onNewConnection();
    void processTextMessage(QString message);
    void processBinaryMessage(QByteArray message);
    void socketDisconnected();
};

#endif // WEBSERVERINFOSENDER_H
