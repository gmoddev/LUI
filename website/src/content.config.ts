import { defineCollection } from 'astro:content';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema } from '@astrojs/starlight/schema';

export const collections = {
  docs: defineCollection({
    loader: docsLoader({ generateId: ({ entry: Entry }) => Entry.replaceAll('\\', '/').replace(/\.mdx?$/, '').replace(/\/index$/, '') }),
    schema: docsSchema(),
  }),
};
