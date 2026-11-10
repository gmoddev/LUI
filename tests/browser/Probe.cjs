const Assert = require('node:assert/strict');
const { WaitFor, WaitForPaint, LaunchOptions } = require('./Harness.cjs');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const { chromium: Chromium } = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const Build = Path.resolve(process.argv[2]);
const Files = {};
for (const Name of ['lui.json', 'Src/Main.luau', 'Src/Counter.luau']) Files[Name] = Fs.readFileSync(Path.join(__dirname, 'fixture', Name), 'utf8');
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css' };
const Server = Http.createServer((Request, Response) => {
    const Name = new URL(Request.url, 'http://localhost').pathname.slice(1);
    if (!['Probe.html', 'Probe.css', 'Entry.mjs', 'Host.mjs', 'Worker.mjs', 'Protocol.mjs', 'Deployment.mjs', 'LuiBrowser.mjs', 'LuiBrowser.wasm'].includes(Name)) { Response.writeHead(404); Response.end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(Name)]);
    Response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'");
    Response.end(Fs.readFileSync(Path.join(Build, Name)));
});
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    const Browser = await Chromium.launch(LaunchOptions('chromium'));
    const Context = await Browser.newContext();
    const Page = await Context.newPage();
    const Errors = [];
    const Console = [];
    let Stage = 'Activation';
    Page.on('pageerror', () => Errors.push('PageError'));
    Page.on('console', Message => { if (Console.length < 256) Console.push(Message.text().slice(0,4096)); });
    const Open = async Input => {
        await Page.goto(`http://127.0.0.1:${Server.address().port}/Probe.html`);
        await Page.bringToFront();
        await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Value => window.Probe.Start(Value), Input);
    };
    try {
        await Open(Files);
        Stage = 'InitialPresentation';
        await Page.getByRole('button', { name: 'Count 0' }).waitFor();
        await WaitForPaint(Page);
        Stage = 'PointerActivation';
        await Page.getByRole('button', { name: 'Count 0' }).click();
        Stage = 'ActivationPresentation';
        await Page.getByRole('button', { name: 'Count 1' }).waitFor();
        Assert.equal(await Page.locator('button').count(), 1);
        const Bounds = await Page.locator('button').boundingBox();
        Assert.equal(Bounds.width, 160); Assert.equal(Bounds.height, 48);
        Stage = 'StaleActivation';
        await Page.evaluate(() => window.Probe.Activate(2, 999));
        await WaitFor(Page, () => window.Probe.Results.some(Value => Value.For === 'Activate' && Value.Accepted === 0));
        Assert.equal(await Page.locator('button').textContent(), 'Count 1');
        await Page.evaluate(() => window.Probe.Retire());
        await WaitFor(Page, () => window.Probe.State === 'Retired');
        Assert.equal(await Page.locator('.LuiNode').count(), 0);

        Stage = 'Acknowledgment';
        await Page.reload(); await Page.bringToFront(); await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Value => { window.Probe.Hold = true; window.Probe.Start(Value); }, Files);
        await WaitFor(Page, () => Boolean(window.Probe.Pending));
        await Page.evaluate(() => window.Probe.Activate(2, 1));
        await WaitFor(Page, () => window.Probe.Results.some(Value => Value.For === 'Activate' && Value.Accepted === 0));
        await Page.waitForTimeout(80);
        Assert.equal(await Page.evaluate(() => window.Probe.Frames), 0);
        await Page.evaluate(() => { window.Probe.Hold = false; window.Probe.ApplyPending(); });
        await Page.getByRole('button', { name: 'Count 0' }).waitFor();
        await Page.evaluate(() => window.Probe.Retire());
        await WaitFor(Page, () => window.Probe.State === 'Retired');

        Stage = 'ApplyFailure';
        await Page.reload(); await Page.bringToFront(); await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Value => { window.Probe.FailNext = true; window.Probe.Start(Value); }, Files);
        await WaitFor(Page, () => window.Probe.State === 'Failed');
        Assert.equal(await Page.locator('.LuiNode').count(), 0);

        Stage = 'ExecutionFailure';
        for (const Source of ['while true do end', 'local T = {} for I = 1, 100000 do T[I] = buffer.create(65536) end', 'error("SyntheticRestrictedCanary")']) {
            const Input = { ...Files, 'Src/Main.luau': Source };
            await Open(Input);
            await WaitFor(Page, () => window.Probe.State === 'Failed', null, { timeout: 10000 });
            Assert.equal(await Page.locator('.LuiNode').count(), 0);
        }
        Stage = 'CaptureRefusals';
        const Extra = { ...Files, 'Extra.luau': 'return true' };
        await Open(Extra); await WaitFor(Page, () => window.Probe.State === 'Failed');
        const Missing = { ...Files }; delete Missing['Src/Counter.luau'];
        await Open(Missing); await WaitFor(Page, () => window.Probe.State === 'Failed');
        for (const Source of ["assert(not app:GetService('PlatformService'):Supports('FrameSurface')); Instance.new('FrameSurface')", Files['Src/Main.luau'] + '\nInstance.new("Window")']) {
            await Open({ ...Files, 'Src/Main.luau': Source });
            await WaitFor(Page, () => window.Probe.State === 'Failed');
            Assert.equal(await Page.locator('.LuiNode').count(), 0);
        }
        Stage = 'MalformedMessage';
        await Open(Files); await Page.getByRole('button', { name: 'Count 0' }).waitFor();
        await Page.evaluate(() => window.Probe.Send('Applied', { Revision: 1, Success: true, Unexpected: true }));
        await WaitFor(Page, () => window.Probe.State === 'Failed');
        Assert.equal(await Page.locator('.LuiNode').count(), 0);
        Stage = 'InitializationOverflow';
        let HeldRoute;
        await Page.route('**/LuiBrowser.wasm', Route => { HeldRoute = Route; });
        await Open(Files);
        while (!HeldRoute) await Page.waitForTimeout(5);
        await Page.evaluate(() => { for (let Index = 0; Index < 1024; ++Index) window.Probe.Send('Activate', { Id: 2, Revision: 1 }); });
        await WaitFor(Page, () => window.Probe.State === 'Failed');
        await HeldRoute.abort().catch(() => {});
        await Page.unroute('**/LuiBrowser.wasm');
        Assert.equal(await Page.locator('.LuiNode').count(), 0);
        Stage = 'Diagnostics';
        Assert.deepEqual(Errors, []);
        Assert.equal(Console.some(Value => Value.includes('SyntheticRestrictedCanary')), false);
        console.log('[LUI:BrowserTest] Real WASM worker, DOM/module activation, acknowledgment, stale input, failure, limits and retirement passed');
        console.log(`[LUI:BrowserTest] Chromium ${Browser.version()}`);
    } catch (Error) {
        console.error(`[LUI:BrowserTest] Failed stage ${Stage}, ${['TimeoutError', 'AssertionError', 'Error'].includes(Error.name) ? Error.name : 'UnknownError'}`);
        console.error(Error.message.slice(0, 1200));
        console.error(await Page.evaluate(() => ({ State: window.Probe?.State, Revision: window.Probe?.Revision, Acknowledged: window.Probe?.Acknowledged, Pending: Boolean(window.Probe?.Pending), Visibility: document.visibilityState, Focused: document.hasFocus(), NativeFrames: /native code/.test(window.requestAnimationFrame.toString()), MockFrames: window.FrameRequests !== undefined, Results: window.Probe?.Results.slice(-8) })).catch(() => ({})));
        throw Error;
    } finally { await Browser.close(); Server.close(); }
}
Main().catch(() => { console.error('[LUI:BrowserTest] Probe failed'); Server.close(); process.exitCode = 1; });
