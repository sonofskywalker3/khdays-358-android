# Contributing to khdays-port

Thank you for helping with the project.

## Branches: open pull requests against `development`

- `development` is where work comes together: open every pull request against it, and start your
  branch from it (`git switch -c my-change origin/development`).
- `main` holds the released versions; `development` is merged into it for each release, so a change
  reaches players in the next one.
- Keep your branch up to date by merging or rebasing on `development`, not on `main`.

## Keep the port and decompilation separate

Submit work to the repository that matches its purpose:

- Byte-matching Nintendo DS decompilation work belongs in `khdays-decomp`.
- Native platform code, compatibility layers, tools, adapted game code, and PC-specific behavior belong in `khdays-port`.

A port pull request does not need to produce matching Nintendo DS assembly, but it must preserve understood behavior and explain intentional differences.

## Prohibited content

Do not commit or attach:

- ROMs or links intended to obtain copyrighted ROMs;
- extracted game assets or data packs;
- original game binaries or assembly dumps;
- proprietary NitroSDK, CodeWarrior, or Nintendo development files;
- secrets, access tokens, or private keys;
- code copied from a project whose license is incompatible with this repository.

Test data should be synthetic, freely licensed, or generated locally from a user-provided game copy.

## Reverse-engineering provenance

When porting a game function or subsystem, include enough information for another contributor to review it:

- source module, overlay, symbol, or address;
- source `khdays-decomp` commit;
- known inputs, outputs, global state, and side effects;
- assumptions that are not yet verified;
- tests or comparison method used;
- intentional behavioral changes.

Avoid presenting guessed names, types, structures, or behavior as confirmed facts.

## AI-assisted contributions

AI-assisted work is allowed only when the contributor has reviewed, understood, and tested the submitted code. The pull request author remains responsible for correctness, provenance, licensing, and maintainability.

Do not submit large generated rewrites that cannot be explained or reviewed.

## Coding guidelines

- Prefer small, reviewable pull requests.
- Keep platform-neutral game code separate from platform implementations.
- Avoid direct hardware addresses in portable code.
- Avoid adding abstractions before there is a concrete use for them.
- Treat warnings as defects in newly added code.
- Add error messages that explain how the user can fix the problem.
- Never silently download game data or proprietary tools.
- Put a file in the folder of its subject (the README's Repository map); the
  port's tests go in `port/tests/`.
- Keep comments short: at most five lines, a file's on what it is and where it
  fits, a function's on what it does; inside a function, comment only what the
  code does not say. What takes longer goes in `docs/` (the README's map of
  them), how something was found and when in the commit message or in
  `docs/RUNTIME_MAP.md` / `docs/DECOMP_FINDINGS.md`.
- Format what you change: C and C++ with clang-format (the `.clang-format`
  files: C++ at the root, the port's C style in `port/`; Visual Studio ships it
  in `VC/Tools/Llvm`), Python with `ruff format` and `ruff check` (`ruff.toml`,
  `pip install ruff`). Generated files and `external/` are never formatted.

## Commit messages

Use concise conventional-style prefixes where practical:

```text
build: add initial CMake project
docs: define legal and contribution policy
tools: add ROM hash verification
platform: add window bootstrap
renderer: decode first texture format
game: port mission state helper
fix: reject unsupported ROM revisions
```

## Pull request checklist

- [ ] The pull request targets `development`.
- [ ] No copyrighted or proprietary content is included.
- [ ] Third-party code and licenses are documented.
- [ ] The change builds on each platform claimed by the pull request.
- [ ] New behavior has a test or a documented manual verification procedure.
- [ ] Reverse-engineering claims distinguish verified facts from assumptions.
- [ ] Port-specific changes have not been pushed into the matching decompilation without a matching reason.
