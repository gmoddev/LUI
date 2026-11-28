# 0091. Package profiles and explicit source imports

Keep the authored module graph, native loader, captured compiler and
standalone packager. Version selection remains one package per alias.
The resolver filters SDK and platform compatibility before backtracking.
Frozen replay keeps the selected version and package digest.

An app declares required, optional and target-specific root dependencies.
A package can declare optional dependencies with separate feature
modules. Consumers explicitly include aliases for a profile. The
resolver and the generated native lock use the same selected edges
and module files. Named locks and output manifests let projects keep
different profiles without overwriting one another.

`package add` and `remove` update requests under the same guard as
restore. They leave the lock and runnable manifest available. A named
immutable cache and generated metadata are published before the lock
and manifest. An interrupted last write can be retried using the same
lock. A deliberate manifest replacement still requires explicit
consent.

Source imports are operator commands. HTTPS fetch requires a SHA-256
of the archive, rejects redirects and only publishes verified archive
content. Git import walks a full commit in a selected local repository
with a selected executable and captures its declared blobs. Both
publish immutable catalog entries. Restore has no network operation.

Native package verification checks the platform-specific library
header and digest. Newly generated module locks and provider
declarations identify the exact bytes by their path. The native
capture checks those bytes, and staging preserves generated locks
verbatim. Older generated paths retain their existing behavior.
Regenerate the manifest and repackage to gain the new check.
The native application and host authority contracts remain in effect.
