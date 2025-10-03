#ifndef CRYPTOUTILS_H
#define CRYPTOUTILS_H

#include <QtGlobal>
#include <QByteArray>
#include <QString>


namespace CryptoUtils {

constexpr int SaltBytes   = 16;
constexpr int KeyBytes    = 32;
constexpr int NonceBytes  = 24;


bool ensureSodium();


QByteArray generateSalt();


QByteArray deriveKey(const QString &password, const QByteArray &salt);


QByteArray encrypt(const QByteArray &plain,
                   const QByteArray &key,
                   QByteArray       &nonceOut);


bool decrypt(const QByteArray &cipher,
             const QByteArray &key,
             QByteArray       &plainOut);

}
#endif
