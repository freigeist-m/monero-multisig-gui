#ifndef ROUTERHANDLER_H
#define ROUTERHANDLER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QObject>
#include <multiwalletcontroller.h>


class RouterHandler : public QTcpServer
{
    Q_OBJECT
public:
    explicit RouterHandler(QObject *parent = nullptr);

    bool start(quint16 port = 5000);
    void close();
    quint16 port() const { return m_port; }

    static void  setWalletManager(MultiWalletController *wm) { s_wm = wm; }
    static MultiWalletController *walletManager()            { return s_wm; }

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    quint16 m_port = 0;
    inline  static MultiWalletController *s_wm = nullptr;
};

#endif
