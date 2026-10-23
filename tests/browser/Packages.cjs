const Assert = require('node:assert/strict');
const { WaitFor, WaitForPaint, LaunchOptions } = require('./Harness.cjs');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const Build = Path.resolve(process.argv[2]);
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png' };
const Server = Http.createServer((Request, Response) => {
    const Name = decodeURIComponent(new URL(Request.url, 'http://localhost').pathname).slice(1), File = Path.resolve(Build, Name);
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404); Response.end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(Name)] || 'application/octet-stream');
    Response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; img-src blob:; connect-src 'self'; object-src 'none'; base-uri 'none'");
    Response.end(Fs.readFileSync(File));
});
async function Run(Engine) {
    const Browser = await Playwright[Engine].launch(LaunchOptions(Engine));
    const Page = await Browser.newPage({ viewport: { width: 1200, height: 900 } });
    Page.setDefaultTimeout(20000);
    const Errors = []; Page.on('pageerror', Error => { if (Errors.length < 32) Errors.push(Error.message); });
    const Base = `http://127.0.0.1:${Server.address().port}`;
    const Open = async Name => {
        await Page.goto(Base + '/Probe.html');
        await Page.bringToFront();
        await WaitFor(Page, () => document.visibilityState === 'visible');
        await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Address => window.Probe.Load(new URL(Address)).catch(() => {}), Base + '/Packages/' + Name + '/lui.browser.json');
    };
    const Running = () => WaitFor(Page, () => window.Probe.Revision > 0 || window.Probe.State === 'Failed');
    const Settled = () => WaitFor(Page, () => window.Probe.State === 'Running' && window.Probe.Acknowledged === window.Probe.Revision && !window.Probe.Pending && !window.Probe.Awaiting && !window.Probe.Events.length && !window.Probe.Activations.size);
    let Step = 'Modular';
    try {
        await Open('Modular'); await Running();
        await WaitForPaint(Page);
        Assert.equal(await Page.evaluate(() => window.Probe.State), 'Running');
        console.log(`[LUI:BrowserTest] ${Engine} modular package running`);
        await Page.getByRole('button', { name: 'Library', exact: true }).click();
        await Page.getByRole('button', { name: 'Home', exact: true }).click();
        Step = 'Conversation startup';
        await Open('Conversation'); await Running();
        await WaitForPaint(Page);
        Assert.equal(await Page.evaluate(() => window.Probe.State), 'Running');
        console.log(`[LUI:BrowserTest] ${Engine} conversation package running`);
        await Page.getByRole('button', { name: 'Append', exact: true }).waitFor();
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].some(Node => /10000 messages/.test(Node.Element.textContent)) || window.Probe.State === 'Failed');
        Assert.equal(await Page.evaluate(() => window.Probe.State), 'Running');
        await WaitFor(Page, () => !window.Probe.Images.size && window.Probe.ImageActive === 0);
        const Resources = await Page.evaluate(() => ({ Nodes: window.Probe.Nodes.size, Images: [...window.Probe.Nodes.values()].filter(Node => Node.Image).length, Cache: window.Probe.ImageCache.size }));
        Assert.ok(Resources.Nodes <= 256); Assert.ok(Resources.Images > 0); Assert.ok(Resources.Cache <= 16);
        const Samples = [];
        const SampleCount = Number(process.env.LUI_BROWSER_SAMPLES ?? 200);
        for (let Index = 0; Index < SampleCount; ++Index) {
            const Before = await Page.evaluate(() => ({ Frames: window.Probe.Frames, Time: performance.now() }));
            await Page.getByRole('button', { name: Index % 2 ? 'Edit visible' : 'Insert above', exact: true }).evaluate(Element => Element.click());
            await WaitFor(Page, Frame => window.Probe.Frames > Frame || window.Probe.State === 'Failed', Before.Frames);
            Assert.equal(await Page.evaluate(() => window.Probe.State), 'Running');
            Samples.push(await Page.evaluate(Time => performance.now() - Time, Before.Time));
        }
        Samples.sort((A, B) => A - B);
        if (Samples.length) console.log(`[LUI:BrowserTest] ${Engine} ${Samples.length} conversation input/DOM observations median=${Samples[Math.ceil(Samples.length * 0.5) - 1].toFixed(2)} p95=${Samples[Math.ceil(Samples.length * 0.95) - 1].toFixed(2)} p99=${Samples[Math.ceil(Samples.length * 0.99) - 1].toFixed(2)} max=${Samples.at(-1).toFixed(2)} ms; includes automation transport`);
        // These end-to-end samples include automation transport. They are observations,
        // not substitutes for the in-page input/presentation performance gate.
        Step = 'Image retirement';
        await Settled();
        await Page.getByRole('button', { name: 'Settings', exact: true }).click();
        await Settled();
        await Page.getByRole('button', { name: '18 px', exact: true }).click();
        await Settled();
        await Page.getByRole('button', { name: 'History', exact: true }).click();
        await Settled();
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].some(Node => Node.Class === 'ScrollingFrame' && !Node.Element.hidden));
        await WaitFor(Page, () => window.Probe.Images.size === 0 && window.Probe.ImageActive === 0);
        const Cleared = await Page.evaluate(() => {
            const Node = [...window.Probe.Nodes.values()].find(Value => Value.Image);
            if (!Node) return false;
            const Entry = Node.Image, PreviousUsers = Entry.Users;
            // Apply the actual backend operation for a cleared canonical Source.
            window.Probe.Apply({ Kind: 'Property', Id: Node.Id, Name: 'Source', Value: '' });
            return !Node.Image && !Node.Element.hasAttribute('src') && Entry.Users === PreviousUsers - 1;
        });
        Assert.equal(Cleared, true);
        await Page.evaluate(() => window.Probe.Dispose());
        await WaitFor(Page, () => window.Probe.ImageActive === 0);
        Assert.equal(await Page.locator('.LuiNode').count(), 0);
        Assert.equal(await Page.evaluate(() => window.Probe.Images.size + window.Probe.ImageCache.size + window.Probe.Assets.size + window.Probe.Timers.size), 0);
        for (const Pattern of ['**/Content/Src/Main.luau', '**/Runtime/LuiBrowser.wasm']) {
            await Page.route(Pattern, Route => Route.fulfill({ status: 200, body: 'tampered' }));
            await Open('Modular'); await WaitFor(Page, () => window.Probe.State === 'Failed');
            Assert.equal(await Page.locator('.LuiNode').count(), 0);
            await Page.unroute(Pattern);
        }
        Assert.deepEqual(Errors, []);
        console.log(`[LUI:BrowserTest] ${Engine} packaged modular/conversation, captured PNG ownership, tamper and cleanup passed`);
    } catch (Error) {
        const State = await Page.evaluate(() => ({ Visibility: document.visibilityState, Frame: window.Probe?.Frame, State: window.Probe?.State, Revision: window.Probe?.Revision, Nodes: window.Probe?.Nodes.size, Images: window.Probe?.Images.size, Pending: window.Probe?.Pending?.Ops.length, Hold: window.Probe?.Hold, Incoming: window.Probe?.Incoming, Closed: window.Probe?.Closed, Results: window.Probe?.Results.slice(-4) })).catch(() => ({}));
        console.error(`[LUI:BrowserTest] ${Engine} package failure at ${Step}: ${Error.message.slice(0, 1000)} ${JSON.stringify(State)}`); throw Error;
    }
    finally { await Browser.close(); }
}
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    try { for (const Engine of (process.env.LUI_BROWSER_ENGINES || 'chromium,firefox,webkit').split(',')) await Run(Engine); }
    finally { Server.close(); }
}
Main().catch(() => { process.exitCode = 1; Server.close(); });
