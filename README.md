# git-overleaf-cli

This repository contains an experimental native C command-line client for
git-overleaf. It is a standalone project with its own source, build,
configuration, and release process.

## Scope

Implemented in this MVP:

- store a raw Overleaf Cookie header in a local file;
- import Overleaf cookies from an existing Firefox profile;
- list accessible Overleaf projects;
- clone a project snapshot into a new Git repository;
- bind an existing Git repository to an Overleaf project;
- pull the latest Overleaf snapshot into an existing Git repository.

Not implemented yet:

- webdriver/browser authentication;
- push/overwrite;
- Overleaf WebSocket project tree fetch;
- ShareJS/OT text updates that preserve remote document ids.

`pull` uses only the snapshot download path, so it does not need WebSocket/OT.
When a pull produces merge conflicts, it records an internal pending-pull state
in Git config. Resolve the merge and commit it locally; native push/overwrite is
not implemented yet.

## Build

Dependencies:

- C11 compiler;
- `cmake`;
- `pkg-config`;
- `libcurl`;
- `googletest`;
- `jansson`;
- `sqlite3`;
- `git`;
- `unzip`.

Check C library dependencies:

```sh
pkg-config --exists libcurl jansson sqlite3
```

Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is written to:

```sh
build/git-overleaf-cli
```

Check the binary:

```sh
./build/git-overleaf-cli --help
```

Clean generated build files:

```sh
cmake --build build --target clean
```

The root `Makefile` is a small compatibility wrapper around CMake, so `make`,
`make test`, and `make clean` remain available for local use.

## Test

Configure, build, and run the unit tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests are offline GTest tests discovered by CTest under `tests/`. They
cover utility helpers, cookie loading precedence, filesystem operations, process
execution, Overleaf project page parsing, and Git metadata exclude handling.

To collect a local coverage summary with GCC/Clang and `gcovr`:

```sh
cmake -S . -B build-coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DGIT_OVERLEAF_ENABLE_COVERAGE=ON
cmake --build build-coverage --target coverage
```

The `coverage` target runs the GTest suite through CTest, prints the gcovr
summary, and writes `build-coverage/coverage.xml`. The normal `test` target is
kept coverage-free so regular test runs stay fast and compiler-independent.

GitHub Actions runs the CMake build, CTest suite, a CLI smoke test, and a
coverage job on pushes and pull requests.

## Usage

Subcommands:

- `auth`: save a raw Overleaf Cookie header or import one from Firefox to a
  local cookie file.
- `list`: list Overleaf projects visible to the current cookie.
- `clone`: download a project snapshot and create a new Git repository.
- `init`: bind an existing Git repository to an Overleaf project without
  changing its working tree.
- `pull`: download the latest project snapshot and merge it into the bound Git
  repository.

Save cookies manually with `auth`:

```sh
./build/git-overleaf-cli auth \
  --cookie 'connect.sid=...; overleaf_session=...' \
  --cookie-file ~/.git-overleaf-cookies
```

Or import cookies from the default Firefox profile after logging in to Overleaf
in Firefox:

```sh
./build/git-overleaf-cli auth --from-firefox
```

For a self-hosted Overleaf instance, pass `--url` before `auth` so the Firefox
importer selects cookies for that host:

```sh
./build/git-overleaf-cli --url https://latex.example.edu auth --from-firefox
```

To bypass Firefox profile discovery:

```sh
./build/git-overleaf-cli auth \
  --from-firefox \
  --firefox-profile /path/to/firefox/profile
```

The Firefox importer reads Firefox `profiles.ini`, prefers the active
`[Install...]` default profile, copies `cookies.sqlite` plus readable `-wal` and
`-shm` sidecar files to a temporary directory, and imports only non-expired
cookies for the configured Overleaf host. It fails if no valid Overleaf session
cookie is present.

List accessible projects with `list`:

```sh
./build/git-overleaf-cli list
```

Create a new local repository from a project snapshot with `clone`:

```sh
./build/git-overleaf-cli clone ./project-name
```

When `clone` runs in an interactive terminal without `--project-id`, it fetches
the visible project list and prompts for a project search. Enter search terms to
match by project name, owner email, or project id, then choose one of the first
20 displayed matches. If the target path is omitted, it is derived from the
selected project name using lowercase words separated by `-`. In
non-interactive contexts, pass `--project-id`. The target path must not exist or
must be an empty directory.

Clone by project id:

```sh
./build/git-overleaf-cli clone \
  --project-id PROJECT_ID \
  --project-name 'Project Name' \
  ./project-name
```

When cloning by project id, `TARGET` can be omitted if `--project-name` is
provided. If both `TARGET` and `--project-name` are omitted, the CLI tries to
resolve the project name from the visible project list before deriving the
target directory:

```sh
./build/git-overleaf-cli clone \
  --project-id PROJECT_ID \
  --project-name 'Project Name'
```

Record project metadata in an existing repository with `init`:

```sh
./build/git-overleaf-cli init \
  --project-id PROJECT_ID \
  --project-name 'Project Name' \
  --repo /path/to/repo
```

Fetch and merge remote Overleaf changes with `pull`:

```sh
./build/git-overleaf-cli pull --repo /path/to/repo
```

Use `--url` before any subcommand to target a self-hosted Overleaf instance:

```sh
./build/git-overleaf-cli --url https://latex.example.edu list
```

## Repository Metadata

The CLI stores its repository metadata in these Git config keys:

- `git-overleaf.projectId`
- `git-overleaf.projectName`
- `git-overleaf.url`
- `git-overleaf.baseRef`
- `git-overleaf.pendingAction`
- `git-overleaf.pendingRemoteCommit`

It uses this base ref:

```text
refs/git-overleaf/base
```

The reserved remote metadata file remains:

```text
.git-overleaf-sync.json
```

Downloaded snapshots remove that file before local Git comparisons.

## Development Notes

Shared C structs and public types use the `GitOverleaf*` prefix, for example
`GitOverleafConfig` and `GitOverleafError`. Project constants use the
`GIT_OVERLEAF_*` prefix. Public helper functions keep the `git_overleaf_`
prefix and are declared in `include/git-overleaf-cli.h`.

## Security

The cookie file contains account-bearing Overleaf session cookies.  The `auth`
command writes it with mode `0600`, but users should still keep it outside Git
repositories and avoid pasting real cookies into logs or issue reports.
