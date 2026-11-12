// Auto-update logic: versions, release JSON, trusted URLs, checksums, minisign.
#include "Minisign.hpp"
#include "Sodium.hpp"
#include "Update.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <sodium.h>

using namespace ztodo;

namespace {
// A minisign-format key pair and signer, built with libsodium, so the tests
// don't need the minisign tool.
struct TestSigner {
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    QByteArray keyId = QByteArray("\x01\x02\x03\x04\x05\x06\x07\x08", 8);

    TestSigner() { crypto_sign_keypair(pk, sk); }

    QString publicKey() const
    {
        return QString::fromLatin1(
            (QByteArray("Ed") + keyId + QByteArray(reinterpret_cast<const char *>(pk), sizeof pk)).toBase64());
    }

    QByteArray sign(const QByteArray &msg, const QByteArray &trusted) const
    {
        unsigned char hash[crypto_generichash_BYTES_MAX];
        crypto_generichash(hash, sizeof hash, reinterpret_cast<const unsigned char *>(msg.constData()),
                           std::size_t(msg.size()), nullptr, 0);
        unsigned char sig[crypto_sign_BYTES];
        crypto_sign_detached(sig, nullptr, hash, sizeof hash, sk);
        const QByteArray sigBytes(reinterpret_cast<const char *>(sig), sizeof sig);
        const QByteArray global = sigBytes + trusted;
        unsigned char gsig[crypto_sign_BYTES];
        crypto_sign_detached(gsig, nullptr, reinterpret_cast<const unsigned char *>(global.constData()),
                             std::size_t(global.size()), sk);
        return "untrusted comment: test\n" + (QByteArray("ED") + keyId + sigBytes).toBase64() + "\ntrusted comment: "
            + trusted + "\n" + QByteArray(reinterpret_cast<const char *>(gsig), sizeof gsig).toBase64() + "\n";
    }
};
} // namespace

class TstUpdate : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(ensureSodium()); }

    void semver()
    {
        QVERIFY(isNewerVersion("v0.2.0", "0.1.9"));
        QVERIFY(isNewerVersion("1.0.0", "1.0.0-rc.1"));
        QVERIFY(!isNewerVersion("v0.1.0", "0.1.0"));
        QVERIFY(!isNewerVersion("garbage", "0.1.0"));
        QVERIFY(SemVer::parse("1.0.0-rc.2") < SemVer::parse("1.0.0-rc.10"));
    }

    void releaseJson()
    {
        const QString base = "https://github.com/sbj-ee/ztodo/releases/download/v0.2.0/";
        QJsonArray assets;
        for (const QString &name : {QString("ztodo_0.2.0_amd64.deb"), QString("ztodo-0.2.0-Darwin.dmg"),
                                    QString("SHA256SUMS"), QString("SHA256SUMS.minisig")}) {
            assets.append(QJsonObject{{"name", name}, {"browser_download_url", base + name}, {"size", 10}});
        }
        const QJsonObject rel{{"tag_name", "v0.2.0"},
                              {"name", "ztodo 0.2.0"},
                              {"html_url", "https://github.com/sbj-ee/ztodo/releases/tag/v0.2.0"},
                              {"assets", assets}};
        const QByteArray json = QJsonDocument(rel).toJson();
        const auto r = ReleaseInfo::fromJson(json);
        QVERIFY(r);
        QCOMPARE(r->versionString(), QString("0.2.0"));
        QVERIFY(r->debAsset());
        QVERIFY(r->dmgAsset());
        QVERIFY(r->packageAsset());
        QVERIFY(r->checksumAsset());
        QVERIFY(r->signatureAsset());
        QVERIFY(isTrustedReleasePageUrl(r->htmlUrl));
        for (const ReleaseAsset &a : r->assets) {
            QVERIFY(isTrustedAssetUrl(a.url, r->tag, a.name));
        }
        QVERIFY(!ReleaseInfo::fromJson("{}"));
        QVERIFY(!ReleaseInfo::fromJson("not json"));
    }

    void untrustedUrls()
    {
        QVERIFY(!isTrustedAssetUrl("https://evil.example/sbj-ee/ztodo/releases/download/v1/x.deb", "v1", "x.deb"));
        QVERIFY(!isTrustedAssetUrl("https://github.com/sbj-ee/zterminal/releases/download/v1/x.deb", "v1", "x.deb"));
        QVERIFY(!isTrustedAssetUrl("https://github.com/sbj-ee/ztodo/releases/download/v1/../x.deb", "v1", "../x.deb"));
        QVERIFY(!isTrustedReleasePageUrl("https://github.com/sbj-ee/zterminal/releases/tag/v1"));
        QVERIFY(isTrustedAssetRedirect(QUrl("https://release-assets.githubusercontent.com/x")));
        QVERIFY(!isTrustedAssetRedirect(QUrl("http://release-assets.githubusercontent.com/x")));
    }

    void checksum()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("ztodo_0.2.0_amd64.deb");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("package bytes");
        f.close();
        const QString sha = sha256OfFile(path);
        const QByteArray sums = (sha + "  ztodo_0.2.0_amd64.deb\n").toLatin1();
        QVERIFY(verifyChecksum(path, sums, "ztodo_0.2.0_amd64.deb").ok());
        QCOMPARE(verifyChecksum(path, sums, "other.deb").status, ChecksumResult::Status::NotListed);
        const QByteArray bad = QByteArray(64, 'a') + "  ztodo_0.2.0_amd64.deb\n";
        QCOMPARE(verifyChecksum(path, bad, "ztodo_0.2.0_amd64.deb").status, ChecksumResult::Status::Mismatch);
    }

    void minisign()
    {
        TestSigner s;
        const auto key = MinisignPublicKey::parse(s.publicKey());
        QVERIFY(key);
        const QByteArray msg = "abc  ztodo_0.2.0_amd64.deb\n";
        const QByteArray trusted = expectedSignedComment("v0.2.0").toUtf8();
        const QByteArray sig = s.sign(msg, trusted);
        const MinisignResult ok = verifyMinisign(msg, sig, *key);
        QVERIFY2(ok.ok, qPrintable(ok.error));
        QCOMPARE(ok.trustedComment, QString("ztodo v0.2.0 SHA256SUMS"));
        // Tampered message, tampered trusted comment, other key: all refused.
        QVERIFY(!verifyMinisign(msg + "x", sig, *key).ok);
        QByteArray altered = sig;
        altered.replace("ztodo v0.2.0", "ztodo v9.9.9");
        QVERIFY(!verifyMinisign(msg, altered, *key).ok);
        TestSigner other;
        QVERIFY(!verifyMinisign(msg, sig, *MinisignPublicKey::parse(other.publicKey())).ok);
    }

    void builtInKeyParses()
    {
        // Either no key yet (installs refused) or a valid minisign key.
        const QString k = updateSigningPublicKey();
        QVERIFY(k.isEmpty() || MinisignPublicKey::parse(k));
    }

    void policy()
    {
        UpdateState st;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(UpdatePolicy::shouldCheckAtStartup(true, st, now));
        QVERIFY(!UpdatePolicy::shouldCheckAtStartup(false, st, now));
        st.lastCheck = now.addSecs(-3600);
        QVERIFY(!UpdatePolicy::shouldCheckAtStartup(true, st, now));
        st.lastCheck = now.addDays(-2);
        QVERIFY(UpdatePolicy::shouldCheckAtStartup(true, st, now));

        ReleaseInfo r;
        r.tag = "v0.3.0";
        r.version = SemVer::parse(r.tag);
        st.skippedVersion = "v0.3.0";
        QVERIFY(!UpdatePolicy::shouldOffer(r, "0.1.0", st, false));
        QVERIFY(UpdatePolicy::shouldOffer(r, "0.1.0", st, true));
        QVERIFY(!UpdatePolicy::shouldOffer(r, "0.3.0", st, true));
    }
};

QTEST_GUILESS_MAIN(TstUpdate)
#include "tst_update.moc"
