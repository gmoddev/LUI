const Assert = require('node:assert/strict');
const { WaitFor, WaitForPaint, LaunchOptions } = require('./Harness.cjs');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const Build = Path.resolve(process.argv[2]);
const Engines = (process.env.LUI_BROWSER_ENGINES || 'chromium,firefox,webkit').split(',');
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.luau': 'text/plain' };
const CSP = "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; img-src blob:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'";
const Server = Http.createServer((Request, Response) => {
    const Name = decodeURIComponent(new URL(Request.url, 'http://localhost').pathname).slice(1);
    const File = Path.resolve(Build, Name);
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404); Response.end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(Name)] || 'application/octet-stream');
    Response.setHeader('Content-Security-Policy', CSP); Response.setHeader('X-Content-Type-Options', 'nosniff');
    Response.end(Fs.readFileSync(File));
});
const Manifest = JSON.parse(Fs.readFileSync(Path.join(__dirname, 'fixture/lui.json'), 'utf8'));
Manifest.Modules.Files = ['Main.luau']; Manifest.Sandbox = { MaxMemoryBytes: 33554432, MaxInterrupts: 1000000 };
const Controls = { 'lui.json': JSON.stringify(Manifest), 'Src/Main.luau': Fs.readFileSync(Path.join(__dirname, 'Controls.luau'), 'utf8') };
const Basic = {};
for (const Name of ['lui.json', 'Src/Main.luau', 'Src/Counter.luau']) Basic[Name] = Fs.readFileSync(Path.join(__dirname, 'fixture', Name), 'utf8');
async function Run(Engine) {
    const Browser = await Playwright[Engine].launch(LaunchOptions(Engine));
    const Context = await Browser.newContext({ viewport: { width: 900, height: 800 } });
    const Page = await Context.newPage();
    Page.setDefaultTimeout(15000);
    const Errors = [], Logs = [];
    Page.on('pageerror', Error => { if (Errors.length < 32) Errors.push(Error.message); });
    Page.on('console', Message => { if (Logs.length < 256) Logs.push(Message.text().slice(0, 4096)); });
    const Open = async Files => {
        await Page.goto(`http://127.0.0.1:${Server.address().port}/Probe.html`);
        await Page.bringToFront();
        await WaitFor(Page, () => document.visibilityState === 'visible');
        await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Input => window.Probe.Start(Input), Files);
    };
    const Settled = () => WaitFor(Page, () => window.Probe.State === 'Running' && window.Probe.Revision > 0 && window.Probe.Acknowledged === window.Probe.Revision && !window.Probe.Pending && !window.Probe.Events.length && !window.Probe.Awaiting && !window.Probe.Activations.size);
    try {
        await Open(Controls);
        await Page.getByRole('button', { name: 'Activate', exact: true }).waitFor();
        await WaitForPaint(Page);
        const Style = await Page.getByText('Top row', { exact: true }).evaluate(Element => ({ Background: getComputedStyle(Element).backgroundColor, Color: getComputedStyle(Element).color, Radius: getComputedStyle(Element).borderRadius }));
        Assert.deepEqual(Style, { Background: 'rgb(12, 34, 56)', Color: 'rgb(230, 240, 250)', Radius: '8px' });
        await Page.getByText('Deferred task ran', { exact: true }).waitFor();
        await Page.getByRole('button', { name: 'Activate', exact: true }).click();
        await Page.getByLabel('Output', { exact: true }).filter({ hasText: 'Activated 1' }).waitFor();
        await Settled();
        await Page.getByRole('button', { name: 'Activate', exact: true }).focus();
        await Page.keyboard.press('Space');
        await Page.getByLabel('Output', { exact: true }).filter({ hasText: 'Activated 2' }).waitFor();
        await Settled();
        await Page.getByRole('checkbox', { name: 'Option' }).check();
        await Page.getByLabel('Output', { exact: true }).filter({ hasText: 'Checked' }).waitFor();
        await Settled();
        Assert.equal(await Page.getByRole('progressbar').getAttribute('value'), '0.5');
        await Page.getByRole('slider').fill('18');
        await Page.getByLabel('Output', { exact: true }).filter({ hasText: 'Value 18' }).waitFor();
        await Settled();
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).fill('A🙂éאב');
        await Page.getByLabel('Output', { exact: true }).filter({ hasText: 'A🙂éאב' }).waitFor();
        await Settled();
        const Scalar = await Page.evaluate(() => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Editor').AcceptedText);
        Assert.equal(Scalar.Start, 6); Assert.equal(Scalar.Length, 0);
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).fill('A🙂B'); await Settled();
        await Page.keyboard.press('ArrowLeft');
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Editor').AcceptedText.Start === 2);
        await Page.keyboard.press('Shift+ArrowLeft');
        await WaitFor(Page, () => {
            const Text = [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Editor').AcceptedText;
            return Text.Start === 1 && Text.Length === 1;
        });
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).fill('A🙂éאב'); await Settled();
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).fill(''); await Settled();
        await Page.keyboard.type('Rapid input 0123456789');
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Editor').AcceptedText.Text === 'Rapid input 0123456789');
        await Settled();
        Assert.equal(await Page.getByRole('textbox', { name: 'Editor', exact: true }).inputValue(), 'Rapid input 0123456789');
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).fill(''); await Settled();
        await Page.keyboard.type('x'); await Page.keyboard.press('Backspace'); await Settled();
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Editor').AcceptedText.Text === '');
        Assert.equal(await Page.getByRole('textbox', { name: 'Editor', exact: true }).inputValue(), '');
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).fill('A🙂éאב'); await Settled();
        await Page.getByRole('textbox', { name: 'Multiline', exact: true }).fill('one\ntwo🙂');
        Assert.equal(await Page.getByRole('textbox', { name: 'Multiline', exact: true }).inputValue(), 'one\ntwo🙂');
        await Settled();
        Assert.equal(await Page.getByLabel('Secret', { exact: true }).getAttribute('type'), 'password');
        await Page.getByLabel('Secret', { exact: true }).fill('SyntheticBrowserSecretCanary');
        await Settled();
        const Copy = await Page.getByLabel('Secret', { exact: true }).evaluate(Element => !Element.dispatchEvent(new Event('copy', { cancelable: true })));
        Assert.equal(Copy, true);
        await Page.evaluate(() => {
            const Node = [...window.Probe.Nodes.values()].find(Value => Value.Props.get('AccessibilityLabel') === 'Editor');
            window.Probe.Input(Node.Id, { Kind: 'Text', Text: 'Stale edit', Start: 0, Length: 0, InputRevision: [0, 0] });
        });
        await Settled();
        Assert.equal(await Page.getByRole('textbox', { name: 'Editor', exact: true }).inputValue(), 'A🙂éאב');
        await Page.getByRole('textbox', { name: 'Editor', exact: true }).focus(); await Settled();
        await Page.keyboard.down('Shift'); await Settled();
        // Playwright emulates page focus, so a second automation tab cannot
        // qualify native tab switching. Exercise the installed blur handler
        // explicitly; selected native focus remains a separate session.
        await Page.evaluate(() => window.dispatchEvent(new Event('blur')));
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].every(Node => !Node.Keys.size && !Node.Pointers.size && !Node.Composing));
        await Page.bringToFront();
        await WaitFor(Page, () => document.visibilityState === 'visible');
        await Page.keyboard.up('Shift'); await Settled();
        await Page.getByText('Top row', { exact: true }).hover(); await Page.mouse.wheel(0, 1200); await Settled();
        const Top = await Page.getByText('Top row', { exact: true }).boundingBox();
        Assert.ok(Top.y < 400);
        await Page.emulateMedia({ colorScheme: 'dark' }); await Settled();
        if (Engine !== 'webkit') {
            await Page.emulateMedia({ forcedColors: 'active' });
            await WaitFor(Page, () => [...window.Probe.Nodes.values()].some(Node => JSON.parse(Node.Props.get('__Style') || '{}').HighContrast === true));
            await Settled();
            await Page.emulateMedia({ forcedColors: 'none' });
            await WaitFor(Page, () => [...window.Probe.Nodes.values()].every(Node => !JSON.parse(Node.Props.get('__Style') || '{}').HighContrast));
            await Settled();
        }
        await Page.evaluate(() => window.Probe.Retire()); await WaitFor(Page, () => window.Probe.State === 'Retired');
        Assert.equal(await Page.locator('.LuiNode').count(), 0);

        // Real ownership churn uses fresh worker/channel/generation each cycle.
        await Open(Basic); await Page.getByRole('button', { name: 'Count 0' }).waitFor();
        await Page.evaluate(() => window.Probe.Dispose());
        await Page.evaluate(async Files => {
            const { BrowserHost } = await import('./Host.mjs');
            const Container = document.getElementById('Application');
            for (let Cycle = 0; Cycle < 100; ++Cycle) {
                const Host = new BrowserHost(Container); Host.Start(Files);
                const Deadline = performance.now() + 15000;
                while (!Host.Revision && Host.State !== 'Failed') { if (performance.now() > Deadline) throw new Error('CycleTimeout'); await new Promise(Resolve => setTimeout(Resolve, 5)); }
                if (Host.State === 'Failed') throw new Error('CycleFailed');
                Host.Dispose(); Host.Dispose();
                if (Host.Worker || Host.Nodes.size || Host.Images.size || Host.ImageCache.size || Host.Events.length || Host.Timers.size || Container.childNodes.length) throw new Error('CycleLeak');
            }
        }, Basic);
        for (const Source of ['while true do end', 'error("SyntheticRestrictedCanary")', Controls['Src/Main.luau'] + '\nInstance.new("Window")',
            Controls['Src/Main.luau'] + '\nInstance.new("TextBox", {Parent = Window, Secret = true, Multiline = true, Text = "SyntheticBrowserSecretCanary"})']) {
            await Open({ ...Controls, 'Src/Main.luau': Source });
            await WaitFor(Page, () => window.Probe.State === 'Failed');
            Assert.equal(await Page.locator('.LuiNode').count(), 0);
            Assert.equal((await Page.locator('body').textContent()).includes('SyntheticBrowserSecretCanary'), false);
        }
        await Open(Basic); await Page.getByRole('button', { name: 'Count 0' }).waitFor();
        await Page.evaluate(() => window.Probe.Send('Applied', { Revision: 999, Success: true }));
        await WaitFor(Page, () => window.Probe.State === 'Failed');
        Assert.equal(Logs.some(Value => /SyntheticRestrictedCanary|SyntheticBrowserSecretCanary/.test(Value)), false);
        Assert.deepEqual(Errors, []);
        console.log(`[LUI:BrowserTest] ${Engine} ${Browser.version()} controls, Unicode, stale text, refusals, lifecycle 100 cycles, diagnostics and CSP passed`);
    } catch (Error) {
        const State = await Page.evaluate(() => {
            const Node = [...(window.Probe?.Nodes.values() || [])].find(Value => Value.Props.get('AccessibilityLabel') === 'Editor');
            return { State: window.Probe?.State, Failure: window.Probe?.FailureCode, Revision: window.Probe?.Revision, Nodes: window.Probe?.Nodes.size, Pending: window.Probe?.Pending?.Ops.length, Visibility: document.visibilityState,
                Editor: Node && { Accepted: Node.AcceptedText, Value: Node.Control.value, Start: Node.Control.selectionStart, End: Node.Control.selectionEnd, Focused: document.activeElement === Node.Control } };
        }).catch(() => ({}));
        console.error(`[LUI:BrowserTest] ${Engine} failed: ${Error.stack.slice(0, 1500)} ${JSON.stringify(State)}`); throw Error;
    }
    finally { await Browser.close(); }
}
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    try { for (const Engine of Engines) await Run(Engine); }
    finally { Server.close(); }
}
Main().catch(() => { process.exitCode = 1; Server.close(); });
