import { BrowserProbe } from './Host.mjs';
// The test harness supplies captured synthetic files. No application auto-download.
window.Probe = new BrowserProbe(document.getElementById('Application'));
