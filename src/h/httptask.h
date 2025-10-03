#pragma once
#include <QRunnable>
#include <QString>
#include <QJsonObject>

class MultisigSession;

class HttpTask : public QRunnable
{
public:
    HttpTask(MultisigSession *sess, QString onion, QString path, bool signedFlag);
    void run() override;
private:
    MultisigSession *m_sess=nullptr;
    QString m_onion, m_path;
    bool    m_signed=false;
};
