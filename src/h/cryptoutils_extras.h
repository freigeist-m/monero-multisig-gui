
#pragma once
#include "cryptoutils.h"
#include <QByteArray>
#include <QString>
#include <QStringView>
namespace CryptoUtils {


inline QByteArray b64url_encode(const QByteArray &in)
{
    QByteArray out = in.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return out;
}
inline QByteArray b64url_decode(QStringView s)
{
    QByteArray tmp = s.toUtf8();

    while (tmp.size() % 4) tmp.append('=');
    return QByteArray::fromBase64(tmp, QByteArray::Base64UrlEncoding);
}


QByteArray onionFromPub(const QByteArray &pub32);


bool splitV3Blob(const QString &blobB64,
                 QByteArray &scalar,
                 QByteArray &prefix,
                 QByteArray &pubOut);

bool trySplitV3BlobFlexible(const QString &in,
                 QByteArray &scalar,
                 QByteArray &prefix,
                 QByteArray &pubOut);


QByteArray ed25519Sign(const QByteArray &msg,
                       const QByteArray &scalar,
                       const QByteArray &prefix);


inline QByteArray _b64(const QByteArray &in) { return b64url_encode(in); }
inline bool       split_v3_blob(const QString &b,QByteArray &s,QByteArray &p,QByteArray &pub){ return splitV3Blob(b,s,p,pub); }
inline QByteArray ed25519_sign(const QByteArray &m,const QByteArray &s,const QByteArray &p){ return ed25519Sign(m,s,p); }
inline QByteArray onion_from_pub(const QByteArray &pub){ return onionFromPub(pub); }

bool isValidOnionV3(const QString &addr);

}
