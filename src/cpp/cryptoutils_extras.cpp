#include "cryptoutils_extras.h"
#include <sodium.h>
#ifndef sodium_base32_VARIANT_RFC4648
#define sodium_base32_VARIANT_RFC4648 1
#endif
#include <QCryptographicHash>
#include <QDebug>

using namespace CryptoUtils;

static const QByteArray OnionPrefix = QByteArray(".onion checksum");
static constexpr quint8  OnionVer   = 0x03;


namespace {
#if defined(SODIUM_LIBRARY_VERSION_MAJOR) && \
(SODIUM_LIBRARY_VERSION_MAJOR > 10 || \
 (SODIUM_LIBRARY_VERSION_MAJOR == 10 && SODIUM_LIBRARY_VERSION_MINOR >= 19)) && \
    defined(sodium_base32_VARIANT_ORIGINAL)

    static QByteArray base32Lower(const QByteArray &in)
{
    char out[128] = {0};
    const size_t outLen = sodium_bin2base32(
        out, sizeof out,
        reinterpret_cast<const unsigned char*>(in.constData()),
        static_cast<size_t>(in.size()),
        sodium_base32_VARIANT_ORIGINAL
        );
    // libsodium uses uppercase for RFC4648; your code wants lowercase
    return QByteArray(out, static_cast<int>(outLen)).toLower();
}
#else

static const char *B32 = "abcdefghijklmnopqrstuvwxyz234567";
static QByteArray base32Lower(const QByteArray &in)
{
    QByteArray out;
    int i = 0, index = 0, currByte, nextByte;
    while (i < in.size()) {
        currByte = (unsigned char)in[i++];
        /* first 5 bits */
        out.append(B32[currByte >> 3]);
        index = (currByte & 0x07) << 2;
        if (i == in.size()) { out.append(B32[index]); break; }
        nextByte = (unsigned char)in[i];
        out.append(B32[index | (nextByte >> 6)]);
        out.append(B32[(nextByte >> 1) & 0x1F]);
        index = (nextByte & 0x01) << 4;
        if (++i == in.size()) { out.append(B32[index]); break; }
        currByte = (unsigned char)in[i];
        out.append(B32[index | (currByte >> 4)]);
        index = (currByte & 0x0F) << 1;
        if (++i == in.size()) { out.append(B32[index]); break; }
        nextByte = (unsigned char)in[i];
        out.append(B32[index | (nextByte >> 7)]);
        out.append(B32[(nextByte >> 2) & 0x1F]);
        index = (nextByte & 0x03) << 3;
        if (++i == in.size()) { out.append(B32[index]); break; }
        currByte = (unsigned char)in[i++];
        out.append(B32[index | (currByte >> 5)]);
        out.append(B32[currByte & 0x1F]);
    }
    return out;
}
#endif

static const char *B32_ALPHABET = "abcdefghijklmnopqrstuvwxyz234567";

bool base32DecodeRfc4648Lower(const QString &s, QByteArray &out)
{
    int len = s.size();
    if (len == 0) return false;

    // For v3 onions: 56 chars -> 35 bytes -> bitsLeft=0 at end
    int buffer = 0;
    int bitsLeft = 0;

    out.clear();
    out.reserve((len * 5) / 8 + 1);

    for (QChar qc : s) {
        const QChar c = qc.toLower();
        const char ch = c.toLatin1();

        const char *p = std::strchr(B32_ALPHABET, ch);
        if (!p) return false; // invalid character

        int val = int(p - B32_ALPHABET); // 0..31

        buffer = (buffer << 5) | val;
        bitsLeft += 5;

        while (bitsLeft >= 8) {
            bitsLeft -= 8;
            unsigned char byte = (buffer >> bitsLeft) & 0xFF;
            out.append(char(byte));
        }
    }

    // For well-formed 56-char onion addresses, this *must* be 0
    if (bitsLeft != 0)
        return false;

    return true;
}

}


QByteArray CryptoUtils::onionFromPub(const QByteArray &pub32)
{
    QByteArray chkInput = OnionPrefix + pub32 + QByteArray(1, char(OnionVer));
    QByteArray csum = QCryptographicHash::hash(chkInput, QCryptographicHash::Sha3_256).left(2);
    QByteArray full = pub32 + csum + QByteArray(1, char(OnionVer));
    return base32Lower(full) + ".onion";
}


bool CryptoUtils::splitV3Blob(const QString &blobB64,
                              QByteArray &scalar,
                              QByteArray &prefix,
                              QByteArray &pubOut)
{
    QByteArray raw = QByteArray::fromBase64(blobB64.toUtf8(), QByteArray::Base64Encoding);
    if (raw.size() != 64) return false;
    scalar = raw.left(32);
    prefix = raw.mid(32, 32);
    pubOut.resize(32);
    crypto_scalarmult_ed25519_base_noclamp(
        reinterpret_cast<unsigned char*>(pubOut.data()),
        reinterpret_cast<const unsigned char*>(scalar.constData()));
    return true;
}


bool CryptoUtils::trySplitV3BlobFlexible(const QString &in,
                                   QByteArray &scalar,
                                   QByteArray &prefix,
                                   QByteArray &pubOut)
{
    QString s = in.trimmed();
    if (s.startsWith("ED25519-V3:", Qt::CaseInsensitive))
        s = s.section(':', 1);


    auto decode = [](const QString &b64, QByteArray::Base64Options opt){
        return QByteArray::fromBase64(b64.toUtf8(), opt);
    };
    QByteArray raw = decode(s, QByteArray::Base64Encoding);
    if (raw.size() != 64)
        raw = decode(s, QByteArray::Base64UrlEncoding);
    if (raw.size() != 64)
        return false;

    scalar = raw.left(32);
    prefix = raw.mid(32, 32);

    pubOut.resize(32);
    crypto_scalarmult_ed25519_base_noclamp(
        reinterpret_cast<unsigned char*>(pubOut.data()),
        reinterpret_cast<const unsigned char*>(scalar.constData()));
    return true;
}


QByteArray CryptoUtils::ed25519Sign(const QByteArray &msg,
                                    const QByteArray &scalar,
                                    const QByteArray &prefix)
{
    unsigned char rHash[64];
    crypto_hash_sha512_state st;
    crypto_hash_sha512_init(&st);
    crypto_hash_sha512_update(&st,
                              reinterpret_cast<const unsigned char*>(prefix.constData()),
                              static_cast<unsigned long long>(prefix.size()));
    crypto_hash_sha512_update(&st,
                              reinterpret_cast<const unsigned char*>(msg.constData()),
                              static_cast<unsigned long long>(msg.size()));
    crypto_hash_sha512_final(&st, rHash);

    unsigned char rScalar[32];
    crypto_core_ed25519_scalar_reduce(rScalar, rHash);

    unsigned char R[32];
    crypto_scalarmult_ed25519_base_noclamp(R, rScalar);

    unsigned char A[32];
    crypto_scalarmult_ed25519_base_noclamp(A,
        reinterpret_cast<const unsigned char*>(scalar.constData()));

    unsigned char hHash[64];
    crypto_hash_sha512_state st2;
    crypto_hash_sha512_init(&st2);
    crypto_hash_sha512_update(&st2, R, 32);
    crypto_hash_sha512_update(&st2, A, 32);
    crypto_hash_sha512_update(&st2,
                              reinterpret_cast<const unsigned char*>(msg.constData()),
                              static_cast<unsigned long long>(msg.size()));
    crypto_hash_sha512_final(&st2, hHash);

    unsigned char hScalar[32];
    crypto_core_ed25519_scalar_reduce(hScalar, hHash);

    unsigned char S[32];
    crypto_core_ed25519_scalar_mul(S, hScalar,
        reinterpret_cast<const unsigned char*>(scalar.constData()));
    crypto_core_ed25519_scalar_add(S, S, rScalar);

    QByteArray sig(64, 0);
    memcpy(sig.data(), R, 32);
    memcpy(sig.data() + 32, S, 32);
    return sig;
}


bool CryptoUtils::isValidOnionV3(const QString &addr)
{
    QString s = addr.trimmed().toLower();
    if (!s.endsWith(".onion"))
        return false;

    s.chop(6); // strip ".onion"

    if (s.size() != 56)
        return false;

    QByteArray decoded;
    if (!base32DecodeRfc4648Lower(s, decoded))
        return false;

    if (decoded.size() != 35)
        return false;

    const QByteArray pub   = decoded.left(32);
    const QByteArray csum  = decoded.mid(32, 2);
    const quint8     ver   = static_cast<quint8>(decoded[34]);

    if (ver != OnionVer) // must be 0x03
        return false;

    QByteArray chkInput = OnionPrefix + pub + QByteArray(1, char(ver));
    QByteArray expected = QCryptographicHash::hash(chkInput,
                                                   QCryptographicHash::Sha3_256)
                              .left(2);

    return csum == expected;
}
