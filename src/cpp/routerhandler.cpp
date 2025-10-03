#include "routerhandler.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <iostream>

RouterHandler::RouterHandler(QObject *parent)
    : QTcpServer(parent)
{}

bool RouterHandler::start(quint16 port )
{

    if (isListening())
        return true;

    m_port = 0;


    if (!port) {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) {
            std::cerr << "[RouterHandler] could not pick a random port\n";
            return false;
        }
        port = probe.serverPort();
        probe.close();
    }

    if (!listen(QHostAddress::LocalHost, port)) {
        std::cerr << "[RouterHandler] bind failed on port "
                  << port << " : "
                  << errorString().toStdString() << '\n';
        return false;
    }
    m_port = serverPort();
    qDebug() << "RouterHandler listening on 127.0.0.1:" << m_port;
    std::cout << "[RouterHandler] listening on 127.0.0.1:"  << m_port << std::endl;
    return true;
}

void RouterHandler::incomingConnection(qintptr sd)
{
    auto *sock = new QTcpSocket(this);
    sock->setSocketDescriptor(sd);

    connect(sock, &QTcpSocket::readyRead, sock, [this, sock]() {
        const QByteArray req = sock->readAll();

        const QList<QByteArray> lines = req.split('\n');
        if (lines.isEmpty()) { sock->disconnectFromHost(); return; }

        const QList<QByteArray> first = lines[0].split(' ');
        const QByteArray method = first.value(0);
        const QByteArray path   = first.value(1);

        QByteArray body;
        QByteArray statusLine;
        QByteArray headers("Connection: close\r\n");

        if (method == "GET" && path == "/") {
            body = "Tor server online";
            statusLine = "HTTP/1.0 200 OK\r\n";
            headers    += "Content-Type: text/plain\r\n";
        } else {
            const QJsonObject obj{{"error", "Not found"}};
            body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            statusLine = "HTTP/1.0 404 Not Found\r\n";
            headers    += "Content-Type: application/json\r\n";
        }
        headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";

        sock->write(statusLine);
        sock->write(headers);
        sock->write(body);
        sock->disconnectFromHost();
    });

    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
}

void RouterHandler::close()
{
    if (isListening())
        QTcpServer::close();
    m_port = 0;
}
