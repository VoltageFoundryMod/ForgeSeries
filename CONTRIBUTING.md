# Contributing

Bug reports, fixes and new modules are welcome.

## Sign your commits (DCO)

Every commit must carry a `Signed-off-by` line. It is a statement that you wrote
the change, or otherwise have the right to submit it under this project's
licence — the [Developer Certificate of Origin](DCO), the same one the Linux
kernel uses. Nothing is assigned to anyone: you keep the copyright in your work,
and it is licensed under GPL-3.0-or-later like the rest of the tree.

Git adds the line for you:

```sh
git commit -s -m "clk: fix swing on divided outputs"
```

which appends:

```
Signed-off-by: Your Name <your.email@example.com>
```

It has to be your real name and a real address — the point is a traceable
record, so pseudonyms and `noreply` addresses do not work. Set them once with
`git config user.name` / `git config user.email` and `-s` fills them in from
there.

Forgot on the last commit:

```sh
git commit --amend -s --no-edit
```

On several, rebase the range and sign each:

```sh
git rebase --signoff main
```

## Before you open a pull request

```sh
make everything
```

That is the gate CI runs: unit tests for every module that has them, the unified
firmware image, every single-module image, and both plugin flavours. It is also
the only way to find out that a change to `core/` broke a module you were not
looking at — the modules share more than they appear to.

If your change touches firmware globals, run `make isolation` too. It catches
state leaking between VCV Rack instances, which a passing firmware build will
not.

## What goes where

- `core/` — shared platform: board bring-up, CV calibration, storage, display,
  menu plumbing. A change here reaches every module, so it needs the full build.
- `apps/<module>/lib/` — that module's engine. Compiled for hardware *and*, via
  the shim, inside the VCV Rack plugin. Keep it free of Arduino and of `rack.hpp`.
- `apps/<module>/vcv-plugin/` — the Rack front end for one module.
- `vcv/` — the aggregate plugin that the VCV Library publishes.

Each module directory has its own `AGENTS.md` or design notes worth reading
before changing its engine.

## Panels, graphics and names

Code is GPL-3.0-or-later. Panel designs, graphics, the logo and the module and
brand names are **not** — see [LICENSE-ASSETS.md](LICENSE-ASSETS.md).

Practically: a pull request that changes code is fine. One that redraws a panel,
alters the logo, or renames a module is a design decision rather than a
contribution, so raise it as an issue first. Contributions to the artwork are
not accepted, since the whole point of holding it separately is that one person
answers for how a Voltage Foundry module looks.

## Style

Match the file you are editing. The codebase leans on comments that explain
*why* a thing is the way it is — particularly where the answer is
non-obvious — and that convention is worth keeping.
