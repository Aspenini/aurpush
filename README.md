# aurpush

[![AUR version](https://img.shields.io/aur/version/aurpush)](https://aur.archlinux.org/packages/aurpush)

A small native C++ CLI for maintaining and publishing Arch Linux packages to
the AUR.

The current directory's `PKGBUILD` is the source of truth for package identity
(`pkgbase`, `pkgver`, `pkgrel`). aurpush is not an AUR helper: it does not
search, download, or resolve other AUR packages. `install` only builds the
PKGBUILD in the current directory with `makepkg` so you can test it locally
before publishing.

## Commands

```text
aurpush              inspect (read-only)
aurpush --check      inspect; exit 1 if any check failed
aurpush init         initialize workspace (does not publish)
aurpush sync         fast-forward to the AUR remote
aurpush install      build and install locally with makepkg -si
aurpush -m "msg"     publish
```

### Inspect

```bash
aurpush
aurpush --check
```

Never writes, fetches into local refs, runs `makepkg`, or pushes. It reports
whether a `PKGBUILD` is present, whether this directory is an aurpush
workspace, whether the matching AUR repository exists, SSH and push access,
`.SRCINFO` freshness (PKGBUILD newer than `.SRCINFO`), and unpublished local
files by name.

`--check` prints the same report and exits `1` when any check failed (not
initialized, not connected, SSH failure, no push access, behind or diverged
remote). Warnings do not fail `--check`.

### Initialize

```bash
aurpush init
```

Reads the `PKGBUILD`, checks AUR SSH access, connects the directory to
`ssh://aur@aur.archlinux.org/<pkgbase>.git`, generates `.SRCINFO` if needed,
and writes a local `.aurpush` marker. Existing AUR packages are fetched;
brand-new packages are prepared so the first `aurpush -m` can create the
initial commit.

`init` never publishes.

### Publish

```bash
aurpush -m "Update to 1.2.0"
```

Regenerates `.SRCINFO`, synchronizes with the AUR remote, commits the packaging
files with the given message, and pushes to the AUR. If nothing changed, it
reports that there is nothing to publish instead of creating an empty commit.

`-m` is the explicit publishing operation. There is no extra prompt and no
force-push.

### Install

```bash
aurpush install
```

Runs `makepkg -si` in the current directory: resolve build dependencies, build
the package, and install it with pacman. Use this to test a PKGBUILD locally
before `aurpush -m`. It does not require an initialized workspace, does not
commit, and does not push to the AUR.

## Typical workflow

New package:

```bash
cd my-package
aurpush
aurpush init
# work on PKGBUILD
aurpush install       # optional: test the package locally
aurpush
aurpush -m "Initial release"
```

Later update:

```bash
cd my-package
# edit PKGBUILD
aurpush install       # optional: test the package locally
aurpush
aurpush sync          # if status reports the workspace is behind
aurpush -m "Update to 1.1.0"
```

### Sync

```bash
aurpush sync
```

Fetches `aur/master` and fast-forwards the local branch onto it. It never
force-pushes, rebases, or creates a merge commit. If the histories have
diverged, it refuses and leaves the tree for you to resolve with git.

## Safety

aurpush refuses to publish when it detects:

- no `PKGBUILD`
- invalid package metadata / `.SRCINFO` generation failure
- an uninitialized workspace
- a mismatched AUR repository
- failed SSH authentication
- missing push permission
- remote commits that need to be synchronized (`aurpush sync` fast-forwards)
- a Git repository that is not the AUR package repo (`init` will not mix
  histories)

The package directory **is** the AUR Git working tree and must be the git
toplevel. Keep upstream project history in a separate repository. `init` in a
subdirectory of another project creates a nested repository rather than
adopting the parent.

## Requirements

- `git`
- `openssh`
- `makepkg` (from `pacman`)
- An AUR account with an SSH key configured for `aur.archlinux.org`

Example `~/.ssh/config` snippet:

```sshconfig
Host aur.archlinux.org
  IdentityFile ~/.ssh/aur
  User aur
```

## Build

```bash
xmake
xmake install
```

```bash
xmake test
```

Debug builds (`xmake config -m debug`) link ASan and UBSan into the tests.

C++20 and xmake are required. There are no extra C++ library dependencies.

The AUR package (`aurpush`) is maintained in its own AUR Git repository, not
this one. Install it from the AUR, or clone
`ssh://aur@aur.archlinux.org/aurpush.git` and run `makepkg -si` there.
