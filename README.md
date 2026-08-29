# aurpush

A small native C++ CLI for maintaining and publishing Arch Linux packages to
the AUR.

The current directory's `PKGBUILD` is the source of truth for package identity
(`pkgbase`, `pkgver`, `pkgrel`). aurpush does not install packages and is not
an AUR helper. Its only job is to make the maintainer-side AUR Git workflow
short and hard to do accidentally.

## Commands

```text
aurpush              inspect (read-only)
aurpush init         initialize workspace (does not publish)
aurpush -m "msg"     publish
```

### Inspect

```bash
aurpush
```

Never writes, fetches into local refs, or pushes. It reports whether a
`PKGBUILD` is present, whether this directory is an aurpush workspace, whether
the matching AUR repository exists, SSH and push access, `.SRCINFO` freshness,
and unpublished local changes.

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

## Typical workflow

New package:

```bash
cd my-package
aurpush
aurpush init
# work on PKGBUILD
aurpush
aurpush -m "Initial release"
```

Later update:

```bash
cd my-package
# edit PKGBUILD
aurpush
aurpush -m "Update to 1.1.0"
```

## Safety

aurpush refuses to publish when it detects:

- no `PKGBUILD`
- invalid package metadata / `.SRCINFO` generation failure
- an uninitialized workspace
- a mismatched AUR repository
- failed SSH authentication
- missing push permission
- remote commits that need to be synchronized
- a Git repository that is not the AUR package repo (`init` will not mix
  histories)

The package directory **is** the AUR Git working tree. Keep upstream project
history in a separate repository.

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

C++20 and xmake are required. There are no extra C++ library dependencies.

Packaging files live in `packaging/` (Arch `PKGBUILD`, desktop entry, and later other build scripts). Build and install from this tree with:

```bash
cd packaging
makepkg -si
```

The AUR Git working tree stays separate from this repository.
