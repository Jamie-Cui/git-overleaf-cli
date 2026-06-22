# git-overleaf-cli

This repository contains an experimental native C command-line client for
git-overleaf. It is a standalone project with its own source, build,
configuration, and release process.

## Scope

Implemented in this MVP:

- store a raw Overleaf Cookie header in a local file;
- list accessible Overleaf projects;
- clone a project snapshot into a new Git repository;
- bind an existing Git repository to an Overleaf project;
- pull the latest Overleaf snapshot into an existing Git repository.

Not implemented yet:

- webdriver/browser authentication;
- Firefox cookie import;
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
- `pkg-config`;
- `libcurl`;
- `jansson`;
- `git`;
- `unzip`.

Build:

```sh
make
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
make clean
```

## Usage

Subcommands:

- `auth`: save a raw Overleaf Cookie header to a local cookie file.
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

List accessible projects with `list`:

```sh
./build/git-overleaf-cli list
```

Create a new local repository from a project snapshot with `clone`:

```sh
./build/git-overleaf-cli clone \
  --project-id PROJECT_ID \
  --project-name 'Project Name' \
  ./project-name
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

## Security

The cookie file contains account-bearing Overleaf session cookies.  The `auth`
command writes it with mode `0600`, but users should still keep it outside Git
repositories and avoid pasting real cookies into logs or issue reports.
