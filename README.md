# git-overleaf-cli

Experimental native C CLI for `git-overleaf`.

## What It Does

- `auth`: save a raw Overleaf Cookie header or import one from Firefox
- `list`: list projects visible to the current cookie
- `clone`: download a project snapshot into a new Git repo
- `init`: bind an existing Git repo to an Overleaf project
- `pull`: merge the latest Overleaf snapshot into a repo
- `push`: upload local Git changes when the Overleaf project is unchanged
- `overwrite`: replace Overleaf project contents with local HEAD

Not implemented yet:

- webdriver/browser authentication

## Build And Test

Dependencies:

- C11 compiler
- `cmake`
- `pkg-config`
- `libcurl`
- `googletest`
- `jansson`
- `sqlite3`
- `git`
- `unzip`

Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run:

```sh
./build/git-overleaf-cli --help
```

Test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Coverage:

```sh
cmake -S . -B build-coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DGIT_OVERLEAF_ENABLE_COVERAGE=ON
cmake --build build-coverage --target coverage
```

The root `Makefile` is a thin wrapper around CMake, so `make`, `make test`,
and `make clean` still work locally.

## Usage

Save cookies:

```sh
./build/git-overleaf-cli auth \
  --cookie 'connect.sid=...; overleaf_session=...' \
  --cookie-file ~/.git-overleaf-cookies
```

Import cookies from Firefox:

```sh
./build/git-overleaf-cli auth --from-firefox
```

Target a self-hosted Overleaf instance:

```sh
./build/git-overleaf-cli --url https://latex.example.edu list
```

Clone a project:

```sh
./build/git-overleaf-cli clone --project-id PROJECT_ID --project-name 'Project Name'
```

Bind or pull an existing repo:

```sh
./build/git-overleaf-cli init --project-id PROJECT_ID --repo /path/to/repo
./build/git-overleaf-cli pull --repo /path/to/repo
```

Push or explicitly overwrite Overleaf from a bound repo:

```sh
./build/git-overleaf-cli push --repo /path/to/repo
./build/git-overleaf-cli overwrite --repo /path/to/repo
```

## Notes

The Firefox importer reads `profiles.ini`, copies `cookies.sqlite` plus any
readable `-wal` / `-shm` sidecar files to a temporary directory, and imports
only valid Overleaf session cookies for the configured host.

Repository metadata lives in local Git config:

- `git-overleaf.projectId`
- `git-overleaf.projectName`
- `git-overleaf.url`
- `git-overleaf.baseRef`
- `git-overleaf.pendingAction`
- `git-overleaf.pendingRemoteCommit`

The reserved sync base ref is `refs/git-overleaf/base`, and the internal
metadata file `.git-overleaf-sync.json` is removed from downloaded snapshots
before Git comparisons.

## Security

The cookie file contains live Overleaf session credentials. `auth` writes it
with mode `0600`, but it should still stay outside Git repositories and logs.
