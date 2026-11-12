# Releasing ztodo

1. Make sure `project(ztodo VERSION x.y.z)` in `CMakeLists.txt` has the version to release (every PR already bumps it, and adds its `CHANGELOG.md` entry; change nothing else). Replace "unreleased" in the CHANGELOG heading with the release date.
2. Merge to `main`, then tag and push: `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. `.github/workflows/release.yml` builds and tests on Linux and macOS, packages
   the `.deb` and `.dmg`, writes `SHA256SUMS`, **signs it** with minisign
   (`SHA256SUMS.minisig`), checks that signature against the public key in
   `core/UpdateSigningKey.cpp` at the tagged commit, and creates the GitHub
   Release. Without the signing secrets, or with no public key in the source,
   the publish job stops and nothing is released.

## Signing key (one-time setup)

ztodo is set up with **the same minisign key as zterminal** (key ID
`32960BBA77B36F1F`), so if you already have `zterminal-release.key` you only
need step 3: add it and its password as secrets of the `release` environment
in `sbj-ee/ztodo`. Signatures can't be swapped between the apps: the signed
comment names the app and the tag (`ztodo vX.Y.Z SHA256SUMS`). To give ztodo
its own key instead, follow every step below.

The in-app updater installs only releases whose `SHA256SUMS` verifies against
the minisign public key compiled into the app, so a compromised GitHub release
alone can't push malware to users.

1. **Generate the key on your own machine** (not in CI, not on a shared box):

   ```sh
   brew install minisign            # or: sudo apt install minisign
   minisign -G -p ztodo-release.pub -s ztodo-release.key
   ```

   Choose a strong password. `ztodo-release.key` is the secret key
   (encrypted with that password); `ztodo-release.pub` is public.

2. **Keep the secret key safe:** store `ztodo-release.key` and its password
   in your password manager, plus an offline backup (an encrypted USB stick).
   If it is lost, users on the old key can't update in-app any more (they'd
   install the next release by hand); if it leaks, rotate it (below).

3. **Give CI the key** as secrets of the protected `release` environment
   (never committed). The environment requires a reviewer's approval before
   the publish job runs, and only `v*` tags may deploy to it:

   ```sh
   gh secret set MINISIGN_SECRET_KEY -R sbj-ee/ztodo --env release < ztodo-release.key
   gh secret set MINISIGN_PASSWORD   -R sbj-ee/ztodo --env release   # paste the password when asked
   ```

   Then remove any repository-wide copies so no other job can read them:
   `gh secret delete MINISIGN_SECRET_KEY -R sbj-ee/ztodo` and the same for
   `MINISIGN_PASSWORD`. On each release, approve the waiting **Publish** job
   (Actions → the run → Review deployments).

4. **Embed the public key:** copy the second line of `ztodo-release.pub`
   (it starts with `RW`) into `kUpdateSigningPublicKey` in
   `core/UpdateSigningKey.cpp`, and update the key in the README's Install section. Commit, then release as usual.

5. Check a release by hand once:

   ```sh
   minisign -Vm SHA256SUMS -P RWQfb7N3uguWMhJ7jsE/Z0hZ7pDWHQ9OEeWSBTlN/R5bxQ8HWSMxp/l9
   ```

## Rotating the key

Generate a new key pair, update the secrets and `core/UpdateSigningKey.cpp`,
and release. Clients that still have the old key can't verify releases signed
with the new key, so tell users to install that release by hand.

## Current release key

Key ID `32960BBA77B36F1F`:

```
RWQfb7N3uguWMhJ7jsE/Z0hZ7pDWHQ9OEeWSBTlN/R5bxQ8HWSMxp/l9
```
