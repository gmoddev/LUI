const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Child = require('node:child_process');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const { WaitFor, WaitForPaint, LaunchOptions } = require('./Harness.cjs');
const Build = Path.resolve(process.argv[2]);
const Engine = process.env.LUI_BROWSER_NATIVE_ENGINE || 'firefox';
const Manifest = JSON.parse(Fs.readFileSync(Path.join(__dirname, 'fixture/lui.json'), 'utf8'));
Manifest.Modules.Files = ['Main.luau'];
const Files = { 'lui.json': JSON.stringify(Manifest), 'Src/Main.luau': Fs.readFileSync(Path.join(__dirname, 'Interaction.luau'), 'utf8') };
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css' };
const Server = Http.createServer((Request, Response) => {
    const File = Path.resolve(Build, '.' + new URL(Request.url, 'http://localhost').pathname);
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404).end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(File)] || 'application/octet-stream');
    Response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'");
    Response.end(Fs.readFileSync(File));
});
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    const Options = LaunchOptions(Engine);
    const Browser = await Playwright[Engine].launch({ ...Options, headless: false,
        ...(Engine === 'chromium' ? { args: [...(Options.args || []), '--force-renderer-accessibility'] } : {}),
        ...(Engine === 'firefox' ? { firefoxUserPrefs: { 'accessibility.force_disabled': -1 } } : {}) });
    let Stage = 'Startup';
    try {
        const Page = await Browser.newPage({ viewport: { width: 1000, height: 800 } });
        const Errors = []; Page.on('pageerror', () => Errors.push('PageError'));
        await Page.goto(`http://127.0.0.1:${Server.address().port}/Probe.html`); await Page.bringToFront();
        await WaitFor(Page, () => Boolean(window.Probe));
        await Page.evaluate(Input => { document.title = 'LUI input qualification'; window.Probe.Start(Input); }, Files);
        await WaitFor(Page, () => window.Probe.Acknowledged > 0);
        await WaitForPaint(Page);
        const Settled = () => WaitFor(Page, () => window.Probe.State === 'Running' && !window.Probe.Events.length && !window.Probe.Awaiting && !window.Probe.Activations.size && window.Probe.Acknowledged === window.Probe.Revision);
        Stage = 'Keyboard focus';
        await Page.getByRole('button', { name: 'Qualification action', exact: true }).focus(); await Settled();
        await Page.keyboard.press('Tab');
        await WaitFor(Page, () => document.activeElement?.getAttribute('aria-label') === 'Qualification input'); await Settled();
        Stage = 'X11 client focus';
        const Windows = Child.execFileSync('xdotool', ['search', '--onlyvisible', '--name', 'LUI input qualification'], { encoding: 'utf8' }).trim().split(/\s+/);
        Assert.ok(Windows.length && Windows.every(Value => /^\d+$/.test(Value)));
        Child.execFileSync('xdotool', ['windowactivate', '--sync', Windows[0]]);
        if (process.env.LUI_BROWSER_AT_ONLY !== '1') {
        // Selecting an engine before Firefox's native input context owns focus
        // can configure the previous context, then be reset by the later click.
        await Page.getByRole('textbox', { name: 'Qualification input', exact: true }).click(); await Settled();
        Stage = 'Select IBus Pinyin';
        // IBus can return failure while asynchronously starting the requested
        // engine. Qualify its observed selection rather than the launch result.
        Child.spawnSync('ibus', ['engine', 'libpinyin'], { timeout: 10000 });
        const EngineDeadline = Date.now() + 10000;
        let Selected = '';
        do {
            Selected = Child.execFileSync('ibus', ['engine'], { encoding: 'utf8', timeout: 1000 }).trim();
            if (Selected === 'libpinyin') break;
            await new Promise(Resolve => setTimeout(Resolve, 100));
        } while (Date.now() < EngineDeadline);
        Assert.equal(Selected, 'libpinyin');
        await Page.evaluate(() => {
            window.NativeEvents = [];
            const Host = window.Probe, Apply = Host.Apply.bind(Host);
            Host.Apply = Op => {
                const Before = document.activeElement;
                Apply(Op);
                if (Before !== document.activeElement && window.NativeEvents.length < 32) window.NativeEvents.push({ Name: 'ProjectionFocus', Kind: Op.Kind, Property: Op.Name, Id: Op.Id, Input: Op.Input });
            };
            for (const Name of ['focusin', 'focusout', 'compositionstart', 'compositionupdate', 'compositionend', 'input']) document.addEventListener(Name, Event => {
                if (window.NativeEvents.length < 32) window.NativeEvents.push({ Name, Target: Event.target.tagName, Value: Event.target.value, Focus: document.activeElement?.tagName });
            }, true);
        });
        Stage = 'Type through XTest';
        Child.execFileSync('xdotool', ['type', '--clearmodifiers', '--delay', '150', 'nihao']);
        Stage = 'Commit through XTest';
        Child.execFileSync('xdotool', ['key', 'space']);
        Stage = 'Real IBus committed value';
        await WaitFor(Page, () => document.querySelector('input[aria-label="Qualification input"]')?.value === '你好', null, { timeout: 10000 });
        await Settled();
        Assert.equal(await Page.evaluate(() => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Qualification input').AcceptedText.Text), '你好');
        Stage = 'Arrow input';
        await Page.keyboard.press('ArrowLeft'); await Settled();
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Qualification input').AcceptedText.Start === 1);
        Assert.equal(await Page.evaluate(() => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('AccessibilityLabel') === 'Qualification input').AcceptedText.Start), 1);
        Stage = 'Activation and composition trace';
        await Page.getByRole('button', { name: 'Qualification action', exact: true }).click(); await Settled();
        const Trace = await Page.getByLabel('Qualification trace', { exact: true }).textContent();
        Assert.match(Trace, /Composition [1-9]\d*\/\d+\/[1-9]\d*/);
        Assert.match(Trace, /keys [^/]*ArrowLeft[^/]*\/.*ArrowLeft/);
        Assert.equal(await Page.getByLabel('Qualification output', { exact: true }).textContent(), 'Activated qualification action');
        console.log(`[LUI:BrowserTest] ${Engine} ${Browser.version()} real IBus/libpinyin commit, scalar cursor and composition/key trace passed: ${Trace}`);
        }
        if (process.env.LUI_BROWSER_INPUT_ONLY !== '1') {
        Stage = 'AT-SPI';
        await Page.getByRole('textbox', { name: 'Qualification input', exact: true }).focus(); await Settled();
        const Accessibility = JSON.parse(Child.execFileSync('/usr/bin/python3', [Path.join(__dirname, 'Accessibility.py')], { encoding: 'utf8', timeout: 15000 }));
        await Page.getByRole('button', { name: 'Qualification action', exact: true }).focus(); await Settled();
        await Page.keyboard.press('Enter'); await Settled();
        Assert.equal(await Page.getByLabel('Qualification output', { exact: true }).textContent(), 'Activated qualification action');
        Stage = 'Orca speech submissions';
        await Page.getByRole('textbox', { name: 'Qualification input', exact: true }).focus(); await Settled();
        const Deadline = Date.now() + 10000;
        let Evidence;
        do {
            Evidence = Fs.existsSync(process.env.LUI_ORCA_EVIDENCE) ? Fs.readFileSync(process.env.LUI_ORCA_EVIDENCE, 'utf8') : '';
            if (Evidence.includes('Qualification action') && Evidence.includes('Qualification input')) break;
            await new Promise(Resolve => setTimeout(Resolve, 50));
        } while (Date.now() < Deadline);
        Assert.ok(Evidence.includes('Qualification action') && Evidence.includes('Qualification input'));
        console.log(`[LUI:BrowserTest] ${Engine} ${Browser.version()} keyboard activation, AT-SPI ${JSON.stringify(Accessibility)} and Orca speech submissions passed`);
        }
        Assert.deepEqual(Errors, []);
        await Page.evaluate(() => window.Probe.Dispose());
    } catch (Error) {
        // This isolated fixture contains synthetic text only.
        const Page = Browser.contexts()[0]?.pages()[0];
        const State = await Page?.evaluate(() => ({ Value: document.querySelector('input[aria-label="Qualification input"]')?.value, Focus: document.activeElement?.tagName, State: window.Probe?.State, Trace: document.querySelector('[aria-label="Qualification trace"]')?.textContent, Events: window.NativeEvents })).catch(() => ({}));
        console.error(`[LUI:BrowserTest] Native input qualification failed at ${Stage}: ${Error.name}: ${Error.message.slice(0, 900)} ${JSON.stringify(State)}`); throw Error;
    }
    finally { await Browser.close(); Server.close(); }
}
Main().catch(() => { process.exitCode = 1; Server.close(); });
