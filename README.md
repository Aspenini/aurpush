# aurpush

[![AUR version](https://img.shields.io/aur/version/aurpush)](https://aur.archlinux.org/packages/aurpush)

A small native C++ CLI for maintaining and publishing Arch Linux packages to
the AUR.

The current directory's `PKGBUILD` is the source of truth for package identity.
aurpush is not an AUR helper: it does not search, download, or resolve other
AUR packages.

## Commands

```text
aurpush                      inspect (read-only)
aurpush --check              inspect; exit 1 if any check failed
aurpush init                 connect this directory to its AUR repository
aurpush sync                 fast-forward to the AUR remote
aurpush install              build and install locally with makepkg -si
aurpush -m "msg"             publish
aurpush -m "msg" --dry-run   show what publishing would do, then stop
```

Options: `--no-color` (or set `NO_COLOR`), `-h`/`--help`, `-V`/`--version`.
One command per invocation — combining two is an error, as is passing `-m`
twice. Arguments after `--` go to `makepkg`, e.g.
`aurpush install -- --noconfirm`.

## Workflow

```bash
cd my-package
aurpush init          # once per package
# edit PKGBUILD
aurpush install       # optional: test the build locally
aurpush               # review status
aurpush sync          # if status says the workspace is behind
aurpush -m "Update to 1.1.0"
```

Publishing regenerates `.SRCINFO`, commits the packaging files, and pushes.
If nothing changed it says so rather than creating an empty commit.

## Safety

aurpush never force-pushes, rebases, or creates merge commits, and it refuses
to publish on a missing `PKGBUILD`, invalid metadata, an uninitialized
workspace, a mismatched AUR repository, an SSH or push-access failure, or a
remote that has commits you do not have. Diverged histories are left for you
to resolve with git.

Inspection is genuinely read-only: it never writes, fetches into local refs,
runs `makepkg`, or pushes. `--check` exits `1` when a check *fails*; warnings
do not.

The package directory **is** the AUR Git working tree and must be the git
toplevel. Keep upstream project history in a separate repository — running
`init` inside another project creates a nested repository rather than adopting
the parent.

## Requirements

`git`, `openssh`, `makepkg`, and an AUR account with an SSH key. Each command
checks for the programs it needs up front and names any that are missing.

```sshconfig
Host aur.archlinux.org
  IdentityFile ~/.ssh/aur
  User aur
```

## Build

```bash
xmake && xmake install
xmake test
```

C++20 and xmake are required; there are no other dependencies. Debug builds
link ASan and UBSan into the tests, and `xmake f --werror=y` turns warnings
into errors.

The `aurpush` AUR package lives in its own AUR Git repository, not this one.
