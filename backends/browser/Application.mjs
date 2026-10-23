import { BrowserHost } from './Host.mjs';
const Container = document.getElementById('Application');
const Host = new BrowserHost(Container);
Host.Load(new URL('../lui.browser.json', import.meta.url)).catch(() => { if (!Host.Closed) Host.Fail(); });
window.addEventListener('pagehide', () => Host.Dispose(), { once: true });
