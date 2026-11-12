// The public key that release SHA256SUMS files are signed with (minisign).
// Not secret. ztodo installs an update only if SHA256SUMS.minisig verifies
// against this key AND its trusted comment is "ztodo <tag> SHA256SUMS", so a
// signature made for another app with the same key can't be replayed here.
//
// Key ID 32960BBA77B36F1F: the same release key as zterminal (minisign -G,
// October 2026). To use a separate key or rotate it, see docs/RELEASING.md.
#include "Update.hpp"

namespace ztodo {

namespace {
constexpr const char kUpdateSigningPublicKey[] = "RWQfb7N3uguWMhJ7jsE/Z0hZ7pDWHQ9OEeWSBTlN/R5bxQ8HWSMxp/l9";

QString &testKey()
{
    static QString k;
    return k;
}
} // namespace

QString updateSigningPublicKey()
{
    if (!testKey().isEmpty()) {
        return testKey();
    }
    return QString::fromLatin1(kUpdateSigningPublicKey);
}

void setUpdateSigningPublicKeyForTests(const QString &base64)
{
    testKey() = base64;
}

} // namespace ztodo
