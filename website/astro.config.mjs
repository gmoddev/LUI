import { defineConfig } from 'astro/config';
import Starlight from '@astrojs/starlight';

const Version = 'docs/sdk-0.1.0-preview.2';
const Previous = 'docs/sdk-0.1.0-preview.1';
export default defineConfig({
  site: 'https://gmoddev.github.io',
  base: '/LUI',
  trailingSlash: 'always',
  integrations: [Starlight({
    title: 'LUI',
    description: 'Learn to build native apps with Luau. Tutorials, practical guides and the LUI API reference.',
    social: [{ icon: 'github', label: 'LUI on GitHub', href: 'https://github.com/gmoddev/LUI' }],
    editLink: undefined,
    lastUpdated: false,
    customCss: ['./src/styles/Docs.css'],
    components: { Header: './src/components/Header.astro', Footer: './src/components/Footer.astro', Search: './src/components/Search.astro' },
    sidebar: [
      { label: 'Start building · Preview.2', items: [
        { label: 'Your first project', link: `${Version}/learn/first-project/` },
        { label: 'Build a modular app', link: `${Version}/learn/advanced-project/` },
      ] },
      { label: 'Guides · Preview.2', items: [{ autogenerate: { directory: `${Version}/guides` } }] },
      { label: 'API reference · Preview.2', items: [
        { label: 'Overview', link: `${Version}/api/` },
        { label: 'How to read the API', link: `${Version}/guides/reading-the-api/` },
        { label: 'Classes', items: [{ autogenerate: { directory: `${Version}/api/classes` } }], collapsed: true },
        { label: 'Services', items: [{ autogenerate: { directory: `${Version}/api/services` } }], collapsed: true },
        { label: 'Types and callback records', items: [{ autogenerate: { directory: `${Version}/api/types` } }], collapsed: true },
        { label: 'Globals and constructors', items: [{ autogenerate: { directory: `${Version}/api/globals` } }], collapsed: true },
      ] },
      { label: 'Optional libraries · Preview.2', items: [
        { label: 'Workbench libraries', link: `${Version}/libraries/luiworkbench/` },
        { label: 'JSON codec', link: `${Version}/libraries/json/` },
        { label: 'JSON HTTP client', link: `${Version}/libraries/jsonclient/` },
        { label: 'Numeric input', link: `${Version}/libraries/luiui/` },
      ] },
      { label: 'Preview.1 archive', collapsed: true, items: [
        { label: 'First project', link: `${Previous}/learn/first-project/` },
        { label: 'Advanced project', link: `${Previous}/learn/advanced-project/` },
        { label: 'Guides', items: [{ autogenerate: { directory: `${Previous}/guides` } }] },
        { label: 'API', items: [{ autogenerate: { directory: `${Previous}/api` } }] },
      ] },
    ],
  })],
});
