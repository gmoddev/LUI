const Assert = require('node:assert/strict');
const { LaunchOptions } = require('./Harness.cjs');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const Build = Path.resolve(process.argv[2]);
const Files = {};
for (const Name of ['lui.json', 'Src/Main.luau', 'Src/Counter.luau']) Files[Name] = Fs.readFileSync(Path.join(__dirname, 'fixture', Name), 'utf8');
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css' };
const Server = Http.createServer((Request, Response) => {
    const File = Path.resolve(Build, '.' + new URL(Request.url, 'http://localhost').pathname);
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404).end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(File)] || 'application/octet-stream');
    Response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'");
    Response.end(Fs.readFileSync(File));
});
async function Run(Engine) {
    const Browser = await Playwright[Engine].launch({ ...LaunchOptions(Engine), headless: true });
    try {
        const Page = await Browser.newPage();
        // A throttled compositor must not hold DOM protocol acknowledgment hostage.
        // Poll through timers because the fixture deliberately withholds paint callbacks.
        await Page.addInitScript(() => {
            window.FrameRequests = 0;
            window.requestAnimationFrame = () => ++window.FrameRequests;
            window.cancelAnimationFrame = () => {};
        });
        await Page.goto(`http://127.0.0.1:${Server.address().port}/Probe.html`);
        await Page.waitForFunction(() => Boolean(window.Probe), null, { polling: 10 });
        await Page.evaluate(Input => window.Probe.Start(Input), Files);
        await Page.waitForFunction(() => window.Probe.Revision > 0 && window.Probe.Acknowledged === window.Probe.Revision, null, { polling: 10, timeout: 5000 });
        const Count = () => Page.locator('button').textContent();
        Assert.equal(await Count(), 'Count 0');
        await Page.evaluate(() => document.querySelector('button').click());
        await Page.waitForFunction(() => document.querySelector('button')?.textContent === 'Count 1' && window.Probe.Acknowledged === window.Probe.Revision, null, { polling: 10, timeout: 5000 });
        // Exercise queued input as well as presentation without a compositor callback.
        await Page.evaluate(() => document.querySelector('button').focus());
        await Page.waitForFunction(() => !window.Probe.Events.length && !window.Probe.Awaiting && window.Probe.Acknowledged === window.Probe.Revision, null, { polling: 10, timeout: 5000 });
        await Page.evaluate(() => { window.Probe.Hold = true; document.querySelector('button').click(); });
        await Page.waitForFunction(() => Boolean(window.Probe.Pending), null, { polling: 10 });
        await Page.evaluate(() => {
            window.Probe.Results = [];
            const Id = [...window.Probe.Nodes.values()].find(Node => Node.Class === 'TextButton').Id;
            window.Probe.Input(Id, { Kind: 'Focus', Value: false });
        });
        await Page.waitForFunction(() => window.Probe.Results.some(Result => Result.For === 'Input' && Result.Accepted === 1), null, { polling: 10 });
        Assert.equal(await Page.evaluate(() => Boolean(window.Probe.Pending) && window.Probe.TerminalInputs.size === 0), true);
        await Page.evaluate(() => document.querySelector('button').click());
        await Page.evaluate(() => { window.Probe.Hold = false; window.Probe.ApplyPending(); });
        await Page.waitForFunction(() => !window.Probe.Events.length && window.Probe.Acknowledged === window.Probe.Revision, null, { polling: 10 });
        Assert.equal(await Count(), 'Count 2');
        // A long successful stream must not retain a deadline per completed request.
        await Page.evaluate(async () => {
            const Host = window.Probe;
            const Id = [...Host.Nodes.values()].find(Node => Node.Class === 'TextButton').Id;
            for (let Index = 0; Index < 200; ++Index) {
                Host.Activate(Id);
                const Deadline = performance.now() + 5000;
                while (Host.Events.length || Host.Activations.size || Host.Acknowledged !== Host.Revision) {
                    if (Host.State !== 'Running' || performance.now() > Deadline) throw new Error('AdmissionTimeout');
                    await new Promise(Resolve => setTimeout(Resolve, 1));
                }
            }
        });
        Assert.ok(await Page.evaluate(() => window.Probe.Timers.size <= 4));
        await Page.evaluate(() => { window.Probe.Dispose(); window.Probe.Dispose(); });
        Assert.equal(await Page.evaluate(() => window.Probe.Timers.size + window.Probe.Events.length + window.Probe.Nodes.size), 0);
        await Page.evaluate(async Input => {
            const { BrowserHost } = await import('./Host.mjs');
            window.Probe = new BrowserHost(document.getElementById('Application'));
            window.Probe.Start(Input);
        }, Files);
        await Page.waitForFunction(() => window.Probe.Revision > 0 && window.Probe.Acknowledged === window.Probe.Revision, null, { polling: 10 });
        await Page.evaluate(() => {
            const Id = [...window.Probe.Nodes.values()].find(Node => Node.Class === 'Window').Id;
            window.Probe.Send('Viewport', { Id, Width: 700, Height: 500 });
            window.Probe.Retire();
        });
        await Page.waitForFunction(() => window.Probe.State === 'Retired', null, { polling: 10, timeout: 2000 });
        Assert.equal(await Page.evaluate(() => window.Probe.Nodes.size + window.Probe.Timers.size), 0);
        await Page.evaluate(async Input => {
            const { BrowserHost } = await import('./Host.mjs');
            window.Probe = new BrowserHost(document.getElementById('Application')); window.Probe.Start(Input);
        }, Files);
        await Page.waitForFunction(() => window.Probe.Revision > 0 && window.Probe.Acknowledged === window.Probe.Revision, null, { polling: 10 });
        await Page.evaluate(() => {
            const Host = window.Probe, Receive = Host.Worker.onmessage;
            Host.Worker.onmessage = Event => { if (Event.data.Kind !== 'Result' || Event.data.For !== 'Activate') Receive(Event); };
            const Id = [...Host.Nodes.values()].find(Node => Node.Class === 'TextButton').Id;
            for (let Index = 0; Index < 257; ++Index) Host.Activate(Id, Host.Revision);
        });
        Assert.equal(await Page.evaluate(() => window.Probe.State), 'Failed');
        Assert.equal(await Page.evaluate(() => window.Probe.Timers.size + window.Probe.Activations.size + window.Probe.TerminalInputs.size), 0);
        console.log(`[LUI:BrowserTest] ${Engine} presentation, input and cleanup progress without animation frames`);
    } finally { await Browser.close(); }
}
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    try { for (const Engine of (process.env.LUI_BROWSER_ENGINES || 'chromium,firefox,webkit').split(',')) await Run(Engine); }
    finally { Server.close(); }
}
Main().catch(Error => { console.error(`[LUI:BrowserTest] Scheduling regression failed: ${Error.name}`); process.exitCode = 1; Server.close(); });
