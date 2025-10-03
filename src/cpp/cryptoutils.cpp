#include "cryptoutils.h"
#include <sodium.h>
#include <stdexcept>

using namespace CryptoUtils;

namespace {
bool initOnce()
{
    static bool ok = (sodium_init() >= 0);
    return ok;
}
}

bool CryptoUtils::ensureSodium() { return initOnce(); }

QByteArray CryptoUtils::generateSalt()
{
    ensureSodium();
    QByteArray salt(SaltBytes, 0);
    randombytes_buf(salt.data(), SaltBytes);
    return salt;
}

QByteArray CryptoUtils::deriveKey(const QString &password,
                                  const QByteArray &salt)
{
    ensureSodium();
    QByteArray key(KeyBytes, 0);

    if (crypto_pwhash(reinterpret_cast<unsigned char *>(key.data()), key.size(),
                      password.toUtf8().constData(), password.toUtf8().size(),
                      reinterpret_cast<const unsigned char *>(salt.constData()),
                      crypto_pwhash_OPSLIMIT_MODERATE,
                      crypto_pwhash_MEMLIMIT_MODERATE,
                      crypto_pwhash_ALG_DEFAULT) != 0)
        throw std::runtime_error("Argon2id key derivation failed");

    return key;
}

QByteArray CryptoUtils::encrypt(const QByteArray &plain,
                                const QByteArray &key,
                                QByteArray &nonceOut)
{
    ensureSodium();
    nonceOut.resize(NonceBytes);
    randombytes_buf(nonceOut.data(), NonceBytes);

    QByteArray cipher(plain.size() + crypto_secretbox_MACBYTES, 0);

    crypto_secretbox_easy(reinterpret_cast<unsigned char *>(cipher.data()),
                          reinterpret_cast<const unsigned char *>(plain.constData()),
                          plain.size(),
                          reinterpret_cast<const unsigned char *>(nonceOut.constData()),
                          reinterpret_cast<const unsigned char *>(key.constData()));

    return nonceOut + cipher;
}

bool CryptoUtils::decrypt(const QByteArray &cipherWithNonce,
                          const QByteArray &key,
                          QByteArray &plainOut)
{
    ensureSodium();
    if (cipherWithNonce.size() <= NonceBytes + crypto_secretbox_MACBYTES)
        return false;

    const unsigned char *nonce = reinterpret_cast<const unsigned char *>(cipherWithNonce.constData());
    const unsigned char *cipher = nonce + NonceBytes;
    const qsizetype cipherLen = cipherWithNonce.size() - NonceBytes;

    QByteArray plain(cipherLen - crypto_secretbox_MACBYTES, 0);

    if (crypto_secretbox_open_easy(reinterpret_cast<unsigned char *>(plain.data()),
                                   cipher, cipherLen,
                                   nonce,
                                   reinterpret_cast<const unsigned char *>(key.constData())) != 0)
        return false;

    plainOut.swap(plain);
    return true;
}
