// Fuzz what the updater reads from the network: the GitHub release JSON, the
// SHA256SUMS file and its minisign signature, and version strings. These are
// the inputs that decide whether ztodo installs software, so beyond not
// crashing the harness checks that nothing the fuzzer makes is accepted as
// signed by the release key, and that version comparison is a strict order.
#include "FuzzMain.hpp"
#include "Minisign.hpp"
#include "Update.hpp"

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace ztodo;

ZTODO_FUZZ_INIT
{
    return 0;
}

ZTODO_FUZZ_TARGET(const uint8_t *data, size_t size)
{
    const QByteArray in(reinterpret_cast<const char *>(data), qsizetype(size));

    // Release JSON.
    if (const auto rel = ReleaseInfo::fromJson(in)) {
        (void)rel->packageAsset();
        (void)rel->debAsset();
        (void)rel->dmgAsset();
        (void)rel->checksumAsset();
        (void)rel->signatureAsset();
        for (const ReleaseAsset &a : rel->assets) {
            (void)isTrustedAssetUrl(a.url, rel->tag, a.name);
        }
        (void)isTrustedReleasePageUrl(rel->htmlUrl);
    }

    // SHA256SUMS: whatever is returned must be a hash.
    const QString h = checksumFor(in, QStringLiteral("ztodo_0.4.1_amd64.deb"));
    if (!h.isEmpty() && (h.size() != 64 || h.contains(QRegularExpression(QStringLiteral("[^0-9a-f]"))))) {
        std::fprintf(stderr, "checksumFor returned \"%s\"\n", qPrintable(h));
        std::abort();
    }

    // Signature file: the input split into message and .minisig at the first
    // NUL. The fuzzer has no secret key, so nothing may verify.
    const qsizetype nul = in.indexOf('\0');
    const QByteArray message = nul >= 0 ? in.left(nul) : QByteArray("SHA256SUMS");
    const QByteArray sig = nul >= 0 ? in.mid(nul + 1) : in;
    if (const auto key = MinisignPublicKey::parse(updateSigningPublicKey())) {
        const MinisignResult r = verifyMinisign(message, sig, *key);
        if (r.ok) {
            std::fprintf(stderr, "a fuzzed signature verified against the release key\n");
            std::abort();
        }
    }
    (void)MinisignPublicKey::parse(QString::fromUtf8(in));

    // Versions: compare() must be antisymmetric and consistent with equality.
    const QList<QByteArray> parts = in.split('\n');
    if (parts.size() >= 2) {
        const SemVer a = SemVer::parse(QString::fromUtf8(parts[0]));
        const SemVer b = SemVer::parse(QString::fromUtf8(parts[1]));
        const int ab = SemVer::compare(a, b);
        const int ba = SemVer::compare(b, a);
        if ((ab < 0) != (ba > 0) || (ab == 0) != (ba == 0)) {
            std::fprintf(stderr, "SemVer::compare is not antisymmetric\n");
            std::abort();
        }
        (void)isNewerVersion(QString::fromUtf8(parts[0]), QString::fromUtf8(parts[1]));
    }
    return 0;
}
