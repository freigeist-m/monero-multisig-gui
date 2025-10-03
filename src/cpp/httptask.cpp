#include "httptask.h"
#include "multisigsession.h"
#include <QDebug>

HttpTask::HttpTask(MultisigSession *sess, QString onion, QString path, bool signedFlag)
    : m_sess(sess), m_onion(onion), m_path(path), m_signed(signedFlag)
{
    setAutoDelete(true);
}

void HttpTask::run()
{

    QJsonObject res = m_sess->httpGetBlocking(m_onion,m_path,m_signed);
    QString err;
    if (res.contains("error")) err = res.value("error").toString();

    emit m_sess->_httpResult(m_onion, m_path, res, err);
}
