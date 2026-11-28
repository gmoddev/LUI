# 0089. Restore packages through the existing module lock

The CLI resolves local directory and ZIP candidates into an immutable,
content-addressed cache. A generated sibling manifest points to those
ordinary module owners and the existing SHA-256 module lock. Publication
preserves the authored manifest and cached snapshots. The native project
capture handles checking, execution and standalone composition.

The existing global alias namespace admits one selected version per alias.
The resolver uses exact, caret and patch constraints with bounded
backtracking. The explicit lock and offline reuse follow the separation
between [Cargo requirements and locked versions](https://doc.rust-lang.org/cargo/reference/resolver.html).
Catalog directories provide a [local development workflow](https://docs.pesde.dev/guides/dependencies/).
Wally's Luau-facing manifest informs the alias-oriented package declarations.
The lock records exact inputs and integrity for offline use, as npm's lock
does for dependency trees. LUI's global alias namespace determines the
resolution constraints. [Wally manifest](https://github.com/UpliftGames/wally#manifest-format)
and [npm lock format](https://docs.npmjs.com/cli/v11/configuring-npm/package-lock-json/).

Linux source reads anchor each directory component and reject special
files. Archive entries have bounded central directories, safe portable
names, regular file modes and declared members. Managed publication
assumes a developer-owned project directory without hostile concurrent
filesystem writers. A local guard serializes instances of this restore
command. The runtime retains its existing anchored capture and grants.

Packages may carry exact native sidecars and binary members. Package
verification hashes every declared platform file. Restore requires the
application to name each provider in its authored manifest and declare
the requisite capabilities. It emits existing-format sidecars that select
the current x64 target and refer to its content-addressed inputs. The
native project capture validates the resulting contract and applies its
own trust checks on launch. A lock is portable between hosts sharing the
same SDK version, while the generated manifest is target-specific.
Remote catalogs and publisher signatures require separate contracts.
