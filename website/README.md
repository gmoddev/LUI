# LUI documentation website

Public tutorials and API reference for the released Windows SDK. The site is
static Astro/Starlight output, served by GitHub Pages under `/LUI/`.

Use the Node version in the repository's `.node-version`. From this directory:

```text
npm ci --ignore-scripts
npm test
npm run build
npm run preview
```

`Release.json` identifies the documented public source and SDK. Generation
checks reflection and generated-definition hashes before writing any pages.
`Content/` contains authored tutorial/guide templates; `scripts/Editorial.mjs`
contains descriptions and behavior notes keyed to actual public symbols.
Structural reference data comes from the repository's `types/schema.json` and
`types/LUI.d.luau`; constructor and helper declarations are included as well.
Generated pages, search data, downloads, dependencies and output are ignored.

Tutorial snippets and downloadable chapter projects come from the same source
in `scripts/Tutorials.mjs`. Generation writes complete projects to
`../build/DocsTutorials` for checking with the real pinned SDK. The site build
verifies local links, anchors, assets, base paths and the search index. Reference
tests reject source drift, unknown documentation keys and invalid inheritance.

The public repository's Documentation workflow builds and validates the site,
then deploys main to GitHub Pages. Pull requests build without deployment.
Published version paths are retained when new SDK documentation is introduced;
do not point an existing version at a different API contract. The first version
has no single-option version picker.

Suggestions and bug reports are welcome. Code contributions are paused while
the project changes quickly; see the repository's contribution guide.
