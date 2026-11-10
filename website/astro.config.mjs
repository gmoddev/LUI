import { defineConfig } from 'astro/config';
import Starlight from '@astrojs/starlight';

const Version = 'docs/sdk-0.1.0-preview.1';
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
    components: { Header: './src/components/Header.astro', Footer: './src/components/Footer.astro' },
    sidebar: [
      { label: 'Start building', items: [
        { label: 'Your first project', link: `${Version}/learn/first-project/` },
        { label: 'Your first advanced project', link: `${Version}/learn/advanced-project/` },
      ] },
      { label: 'Guides', items: [{ autogenerate: { directory: `${Version}/guides` } }] },
      { label: 'API reference', items: [
        { label: 'Overview', link: `${Version}/api/` },
        { label: 'Classes', items: [{ autogenerate: { directory: `${Version}/api/classes` } }], collapsed: true },
        { label: 'Services', items: [{ autogenerate: { directory: `${Version}/api/services` } }], collapsed: true },
        { label: 'Value and resource types', items: [{ autogenerate: { directory: `${Version}/api/types` } }], collapsed: true },
        { label: 'Globals and constructors', items: [{ autogenerate: { directory: `${Version}/api/globals` } }], collapsed: true },
      ] },
    ],
  })],
});
