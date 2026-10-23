const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const { WaitFor, LaunchOptions } = require('./Harness.cjs');
const Build = Path.resolve(process.argv[2]);
const Cases = ['Normal', 'UnknownDescriptor', 'WrongRuntimeHash', 'WrongContentHash', 'MissingContent', 'RedirectContent', 'BlockedWasm', 'NoWorker', 'NoWasmCompilation', 'StreamLimit'];
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png' };
const Server = Http.createServer((Request, Response) => {
    const URL = new global.URL(Request.url, 'http://localhost');
    const Parts = decodeURIComponent(URL.pathname).split('/').filter(Boolean);
    const Case = Cases.includes(Parts[0]) ? Parts.shift() : URL.searchParams.get('Case') || 'Normal';
    const Name = Parts.join('/'), File = Path.resolve(Build, Name);
    if (Name === 'Stalled.mjs') { Response.setHeader('Content-Type', 'text/javascript'); Response.end('self.onmessage = () => {};'); return; }
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404).end(); return; }
    const Worker = Case === 'NoWorker' ? "'none'" : "'self'";
    const Wasm = Case === 'NoWasmCompilation' ? '' : " 'wasm-unsafe-eval'";
    Response.setHeader('Content-Security-Policy', `default-src 'none'; script-src 'self'${Wasm}; worker-src ${Worker}; style-src 'self'; img-src blob:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'`);
    Response.setHeader('Content-Type', Types[Path.extname(File)] || 'application/octet-stream');
    Response.setHeader('X-Content-Type-Options', 'nosniff');
    if ((Case === 'MissingContent' && Name.includes('/Content/')) || (Case === 'BlockedWasm' && Name.endsWith('.wasm'))) { Response.writeHead(503).end(); return; }
    if (Case === 'RedirectContent' && Name.includes('/Content/')) { Response.writeHead(302, { Location: '/' + Name }).end(); return; }
    if (Name.endsWith('/lui.browser.json')) {
        if (Case === 'StreamLimit') {
            Response.setHeader('Transfer-Encoding', 'chunked');
            for (let Index = 0; Index < 8; ++Index) Response.write(Buffer.alloc(32768, 32));
            Response.end(); return;
        }
        const Descriptor = JSON.parse(Fs.readFileSync(File, 'utf8'));
        if (Case === 'UnknownDescriptor') Descriptor.Unexpected = true;
        if (Case === 'WrongRuntimeHash') Descriptor.Runtime[0].Sha256 = '0'.repeat(64);
        if (Case === 'WrongContentHash') Descriptor.Content[0].Sha256 = '0'.repeat(64);
        Response.end(JSON.stringify(Descriptor)); return;
    }
    Response.end(Fs.readFileSync(File));
});
async function Run(Engine) {
    const Browser = await Playwright[Engine].launch(LaunchOptions(Engine));
    try {
        const Page = await Browser.newPage();
        const Logs = []; Page.on('console', Message => { if (Logs.length < 256) Logs.push(Message.text().slice(0, 2048)); });
        const Base = `http://127.0.0.1:${Server.address().port}`;
        // Exercise the actual generated launcher under the published CSP/MIME profile.
        await Page.goto(Base + '/Normal/Packages/Modular/index.html');
        await Page.getByRole('button', { name: 'Library', exact: true }).waitFor();
        Assert.ok((await Page.locator('section[role=region]').count()) > 0);
        for (const Case of Cases.filter(Name => Name !== 'Normal')) {
            await Page.goto(Base + '/Probe.html?Case=' + Case);
            await WaitFor(Page, () => Boolean(window.Probe));
            await Page.evaluate(Address => window.Probe.Load(new URL(Address)).catch(() => {}), Base + '/' + Case + '/Packages/Modular/lui.browser.json');
            await WaitFor(Page, () => window.Probe.State === 'Failed');
            Assert.equal(await Page.locator('.LuiNode').count(), 0);
            Assert.equal(await Page.getByRole('status').textContent(), 'The application could not continue.');
            Assert.equal(await Page.evaluate(() => window.Probe.Timers.size + window.Probe.Assets.size + window.Probe.ImageCache.size + window.Probe.Events.length), 0);
            Assert.equal(await Page.evaluate(() => window.Probe.Worker), null);
        }
        await Page.goto(Base + '/Probe.html'); await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Address => {
            window.Probe.State = 'Starting'; window.Probe.Connect(new URL(Address)); window.Probe.Retire();
        }, Base + '/Stalled.mjs');
        await WaitFor(Page, () => window.Probe.State === 'Retired', null, { timeout: 2500 });
        Assert.equal(await Page.evaluate(() => window.Probe.Worker), null);
        Assert.equal(await Page.evaluate(() => window.Probe.Timers.size + window.Probe.Nodes.size), 0);
        Assert.equal(Logs.some(Value => /SyntheticRestrictedCanary|SyntheticBrowserSecretCanary/.test(Value)), false);
        console.log(`[LUI:BrowserTest] ${Engine} product launcher, CSP/worker/WASM, descriptor/hash, redirect/missing/stream limits and fail-closed cleanup passed`);
    } finally { await Browser.close(); }
}
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    try { for (const Engine of (process.env.LUI_BROWSER_ENGINES || 'chromium,firefox,webkit').split(',')) await Run(Engine); }
    finally { Server.close(); }
}
Main().catch(Error => { console.error(`[LUI:BrowserTest] Policy regression failed: ${Error.name}`); process.exitCode = 1; Server.close(); });
