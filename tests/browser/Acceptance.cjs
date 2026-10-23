const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const { WaitFor, WaitForPaint, LaunchOptions } = require('./Harness.cjs');
const Build = Path.resolve(process.argv[2]);
const ModuleCases = JSON.parse(Fs.readFileSync(Path.join(__dirname, 'Modules.json'), 'utf8'));
Assert.equal(ModuleCases.Version, 1);
const Manifest = JSON.parse(Fs.readFileSync(Path.join(__dirname, 'fixture/lui.json'), 'utf8'));
Manifest.Modules.Files.push('Scope.luau', 'VirtualList.luau');
const Files = { 'lui.json': JSON.stringify(Manifest), 'Src/Main.luau': Fs.readFileSync(Path.join(__dirname, 'Acceptance.luau'), 'utf8'),
    'Src/Counter.luau': Fs.readFileSync(Path.join(__dirname, 'fixture/Src/Counter.luau'), 'utf8') };
for (const Name of ['Scope', 'VirtualList']) Files[`Src/${Name}.luau`] = Fs.readFileSync(Path.join(__dirname, '../../examples/conversation/Vendor/LuiUI', Name + '.luau'), 'utf8');
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css' };
const Server = Http.createServer((Request, Response) => {
    if (Request.url === '/Trap.mjs') {
        Response.setHeader('Content-Type', 'text/javascript');
        Response.end('const Bytes = new Uint8Array([0,97,115,109,1,0,0,0,1,4,1,96,0,0,3,2,1,0,7,8,1,4,84,114,97,112,0,0,10,5,1,3,0,0,11]); const Instance = new WebAssembly.Instance(new WebAssembly.Module(Bytes)); self.onmessage = () => Instance.exports.Trap();'); return;
    }
    const File = Path.resolve(Build, '.' + new URL(Request.url, 'http://localhost').pathname);
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404).end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(File)] || 'application/octet-stream');
    Response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'");
    Response.end(Fs.readFileSync(File));
});
async function Run(Engine) {
    const Browser = await Playwright[Engine].launch(LaunchOptions(Engine));
    const Context = await Browser.newContext({ viewport: { width: 1000, height: 900 } });
    const Page = await Context.newPage();
    Page.setDefaultTimeout(10000);
    const Errors = []; let ModuleCanarySeen = false;
    Page.on('pageerror', Error => { if (Errors.length < 32) Errors.push('PageError'); ModuleCanarySeen ||= Error.message.includes('SyntheticRestrictedModuleCanary'); });
    Page.on('console', Message => { ModuleCanarySeen ||= Message.text().includes('SyntheticRestrictedModuleCanary'); });
    let Stage = 'Shared semantics', Owner;
    Page.on('worker', Worker => { Owner = Worker; });
    const Settled = () => WaitFor(Page, () => window.Probe.State === 'Running' && window.Probe.Revision > 0 && window.Probe.Acknowledged === window.Probe.Revision && !window.Probe.Pending && !window.Probe.Events.length && !window.Probe.Awaiting && !window.Probe.Activations.size && !window.Probe.TerminalInputs.size);
    const Open = async (InputFiles = Files) => {
        await Page.goto(`http://127.0.0.1:${Server.address().port}/Probe.html`); await Page.bringToFront();
        await WaitFor(Page, () => Boolean(window.Probe)); await Page.evaluate(Input => window.Probe.Start(Input), InputFiles);
        await Page.getByLabel('State', { exact: true }).filter({ hasText: 'Shared accepted' }).waitFor(); await Settled(); await WaitForPaint(Page);
    };
    const Command = async Name => { await Page.getByRole('button', { name: Name, exact: true }).evaluate(Element => Element.click()); await Settled(); };
    try {
        for (let Generation = 0; Generation < 2; ++Generation) for (const Case of ModuleCases.Cases) {
            Stage = `Module ${Case.Name} generation ${Generation}`;
            const CaseManifest = { ...Manifest, Modules: { ...Manifest.Modules, Files: Object.keys(Case.Files).sort() } };
            const CaseFiles = { 'lui.json': JSON.stringify(CaseManifest) };
            for (const [Name, Source] of Object.entries(Case.Files)) CaseFiles['Src/' + Name] = Name === 'Main.luau' && !Case.CompileError ? ModuleCases.Prelude + Source + ModuleCases.Epilogue : Source;
            await Page.goto(`http://127.0.0.1:${Server.address().port}/Probe.html`); await Page.bringToFront();
            await WaitFor(Page, () => Boolean(window.Probe)); await Page.evaluate(Input => window.Probe.Start(Input), CaseFiles);
            if (Case.CompileError) {
                await WaitFor(Page, () => window.Probe.State === 'Failed');
                Assert.equal(await Page.evaluate(() => window.Probe.Nodes.size + window.Probe.Events.length + window.Probe.Timers.size), 0);
            } else {
                await Page.getByLabel('Module state', { exact: true }).filter({ hasText: 'Module accepted' }).waitFor(); await Settled();
                await Page.evaluate(() => window.Probe.Retire()); await WaitFor(Page, () => window.Probe.State === 'Retired');
                Assert.equal(await Page.evaluate(() => window.Probe.Nodes.size + window.Probe.Events.length + window.Probe.Timers.size), 0);
            }
        }
        console.log(`[LUI:BrowserTest] ${Engine} ${ModuleCases.Cases.length} shared module/cache/isolation/cycle/initializer/task cases in two fresh generations passed`);
        await Open();
        Stage = 'Pointer local coordinates';
        const Box = await Page.getByRole('button', { name: 'Pointer target', exact: true }).boundingBox();
        await Page.getByRole('button', { name: 'Pointer target', exact: true }).evaluate(Element => Element.addEventListener('pointerdown', Event => {
            const Rect = Element.getBoundingClientRect();
            window.ExpectedPointer = 'Pointer ' + Math.floor(Event.clientX - Rect.left) + '/' + Math.floor(Event.clientY - Rect.top);
        }, { once: true }));
        await Page.mouse.move(Box.x + 12, Box.y + 12); await Settled(); await Page.mouse.down();
        await WaitFor(Page, () => Boolean(window.ExpectedPointer) && document.querySelector('[aria-label="Pointer state"]').textContent === window.ExpectedPointer); await Settled();
        await Page.mouse.up();
        await Page.getByLabel('Pointer state', { exact: true }).filter({ hasText: 'Ended 1/1/0' }).waitFor(); await Settled();
        Assert.equal(await Page.getByLabel('State', { exact: true }).textContent(), 'Activated 1');
        Stage = 'Stale pointer release';
        await Page.mouse.down(); await Settled();
        await Page.evaluate(() => window.Probe.Hold = true);
        await Page.getByRole('button', { name: 'Move target', exact: true }).evaluate(Element => Element.click());
        await WaitFor(Page, () => Boolean(window.Probe.Pending));
        await Page.mouse.up();
        await Page.evaluate(() => { window.Probe.Hold = false; window.Probe.ApplyPending(); });
        await Page.getByLabel('Pointer state', { exact: true }).filter({ hasText: 'Ended 2/2/1' }).waitFor(); await Settled();
        Stage = 'Actual lost pointer capture';
        const Moved = await Page.getByRole('button', { name: 'Pointer target', exact: true }).boundingBox();
        await Page.mouse.move(Moved.x + 12, Moved.y + 12); await Settled(); await Page.mouse.down(); await Settled();
        await Page.mouse.move(Moved.x + 13, Moved.y + 13); await Settled();
        await Page.getByRole('button', { name: 'Pointer target', exact: true }).evaluate(Element => {
            const Node = window.Probe.Nodes.get(Number(Element.dataset.id)); Element.releasePointerCapture([...Node.Pointers][0]);
        });
        await Page.mouse.move(Moved.x + 14, Moved.y + 14);
        await Page.getByLabel('Pointer state', { exact: true }).filter({ hasText: 'Ended 3/3/2' }).waitFor();
        await Page.mouse.up(); await Settled();
        Stage = 'Tab and disabled/hidden controls';
        await Page.getByRole('textbox', { name: 'First', exact: true }).focus(); await Settled();
        await Page.keyboard.press('Tab'); Assert.equal(await Page.getByRole('textbox', { name: 'Second', exact: true }).evaluate(Element => Element === document.activeElement), true);
        await Page.keyboard.press('Shift+Tab'); Assert.equal(await Page.getByRole('textbox', { name: 'First', exact: true }).evaluate(Element => Element === document.activeElement), true); await Settled();
        Stage = 'Text limits and CRLF';
        await Page.getByRole('textbox', { name: 'First', exact: true }).fill('Valid'); await Settled();
        await Page.getByRole('textbox', { name: 'First', exact: true }).fill('Too long for this field'); await Settled();
        Assert.equal(await Page.getByRole('textbox', { name: 'First', exact: true }).inputValue(), 'Valid');
        await Page.getByRole('textbox', { name: 'Lines', exact: true }).fill('a\r\nb'); await Settled();
        Assert.equal(await Page.getByRole('textbox', { name: 'Lines', exact: true }).inputValue(), 'a\nb');
        Stage = 'Browser editing copy cut paste';
        await Page.getByRole('textbox', { name: 'Lines', exact: true }).fill('a🙂b'); await Settled();
        await Page.getByRole('textbox', { name: 'Lines', exact: true }).evaluate(Element => { Element.setSelectionRange(1, 3); document.dispatchEvent(new Event('selectionchange')); }); await Settled();
        await Page.keyboard.press('Control+c'); await Page.keyboard.press('Control+End'); await Page.keyboard.press('Control+v'); await Settled();
        Assert.equal(await Page.getByRole('textbox', { name: 'Lines', exact: true }).inputValue(), 'a🙂b🙂');
        await Page.keyboard.press('Shift+ArrowLeft'); await Settled(); await Page.keyboard.press('Control+x'); await Settled();
        Assert.equal(await Page.getByRole('textbox', { name: 'Lines', exact: true }).inputValue(), 'a🙂b');
        Stage = 'Reparent and destroy focused field';
        await Page.getByRole('textbox', { name: 'First', exact: true }).focus(); await Settled(); await Command('Reparent field');
        await Page.getByRole('textbox', { name: 'First', exact: true }).focus(); await Settled();
        await Page.keyboard.down('Q'); await Settled(); await Command('Remove field');
        Assert.equal(await Page.getByRole('textbox', { name: 'First', exact: true }).count(), 0); await Page.keyboard.up('Q');
        Stage = 'Nested scrolling and z order';
        const Nested = await Page.evaluate(() => [...window.Probe.Nodes.values()].find(Node => Node.Props.get('Text') === 'Nested top').Bounds.Y);
        const Inner = await Page.getByLabel('Inner', { exact: true }).boundingBox();
        await Page.getByText('Nested top', { exact: true }).hover(); await Settled(); await Page.mouse.wheel(0, 70);
        await WaitFor(Page, Before => [...window.Probe.Nodes.values()].some(Node => Node.Props.get('Text') === 'Nested top' && Node.Bounds.Y < Before), Nested);
        Assert.equal((await Page.getByLabel('Inner', { exact: true }).boundingBox()).y, Inner.y);
        await Page.getByRole('button', { name: 'Upper', exact: true }).click(); await Settled();
        Assert.equal(await Page.getByLabel('State', { exact: true }).textContent(), 'Upper activated');
        Stage = 'Resident collection identity and retirement';
        await Page.getByRole('button', { name: 'Row 1', exact: true }).focus(); await Settled();
        await Page.evaluate(() => window.Resident = document.activeElement);
        await Command('Insert row');
        Assert.equal(await Page.evaluate(() => window.Resident.isConnected && document.activeElement === window.Resident && window.Resident.textContent === 'Row 1'), true);
        await Page.keyboard.down('Shift'); await Settled(); await Command('Retire rows');
        Assert.equal(await Page.evaluate(() => !window.Resident.isConnected && document.activeElement !== window.Resident), true);
        await Page.evaluate(() => window.Resident.dispatchEvent(new KeyboardEvent('keyup', { key: 'Shift', bubbles: true }))); await Settled();
        Assert.equal(await Page.evaluate(() => [...window.Probe.Nodes.values()].every(Node => !Node.Keys.size && !Node.Pointers.size)), true);
        await Page.keyboard.up('Shift'); await Settled();
        Stage = 'Escape to embedding page';
        await Page.evaluate(() => { const After = document.createElement('button'); After.textContent = 'Embedding after'; After.id = 'After'; document.body.append(After); });
        // Commands follow the collection in canonical Instance/DOM child order.
        await Page.getByRole('button', { name: 'Retire rows', exact: true }).focus(); await Page.keyboard.press('Tab');
        Assert.equal(await Page.evaluate(() => document.activeElement.id), 'After');
        Stage = 'Malformed live channel and generation cleanup';
        for (const Fields of [{ Version: 2 }, { Generation: 'f'.repeat(32) }, { Sequence: 1 }, { Extra: true }, { Id: 0 }]) {
            await Open(); await Page.evaluate(Input => window.Probe.Send('Activate', { Id: 1, Revision: window.Probe.Revision, ...Input }), Fields);
            await WaitFor(Page, () => window.Probe.State === 'Failed');
            Assert.equal(await Page.evaluate(() => window.Probe.Nodes.size + window.Probe.Timers.size + window.Probe.Events.length), 0);
        }
        await Open();
        await Page.evaluate(() => { const Bytes = new Uint8Array(4); structuredClone(Bytes, { transfer: [Bytes.buffer] }); window.Probe.Send('File', { Path: 'Unused', Chunk: Bytes, Final: true }); });
        await WaitFor(Page, () => window.Probe.State === 'Failed');
        await Open();
        await Page.evaluate(() => { window.Old = window.Probe; window.OldCallback = window.Probe.Worker.onmessage; window.Probe.Dispose(); });
        await Page.evaluate(async Input => { const { BrowserHost } = await import('./Host.mjs'); window.Probe = new BrowserHost(document.getElementById('Application')); window.Probe.Start(Input); }, Files);
        await WaitFor(Page, () => window.Probe.Acknowledged > 0); await Settled();
        await Page.evaluate(() => window.OldCallback({ data: { Kind: 'Fatal' } }));
        Assert.equal(await Page.evaluate(() => window.Probe.State), 'Running');
        if (Engine === 'chromium') {
            Stage = 'Actual tab focus loss';
            const Session = await Context.newCDPSession(Page);
            await Session.send('Emulation.setFocusEmulationEnabled', { enabled: false });
            await Page.getByRole('textbox', { name: 'First', exact: true }).focus(); await Settled();
            await Page.keyboard.down('Shift'); await Settled();
            const Background = await Context.newPage();
            const OtherSession = await Context.newCDPSession(Background);
            await OtherSession.send('Emulation.setFocusEmulationEnabled', { enabled: false });
            await Background.goto('about:blank'); await Background.bringToFront();
            await WaitFor(Page, () => document.hidden || !document.hasFocus());
            await WaitFor(Page, () => [...window.Probe.Nodes.values()].every(Node => !Node.Keys.size && !Node.Pointers.size && !Node.Composing));
            await Background.close(); await Page.bringToFront();
            await Page.keyboard.up('Shift'); await Settled();
            await Session.detach();
        }
        Stage = 'Heap growth and long compiler input';
        const MemoryManifest = { ...Manifest, Sandbox: { MaxMemoryBytes: 33554432, MaxInterrupts: 1000000 }, Modules: { ...Manifest.Modules, Files: ['Main.luau'] } };
        const Source = 'local Padding = "' + 'x'.repeat(160000) + '"\nlocal Held = buffer.create(31*1024*1024)\n' +
            'local Window=Instance.new("Window"); Instance.new("TextLabel", {Parent=Window,AccessibilityLabel="State",Text="Shared accepted"}); Window.Visible=true';
        await Open({ 'lui.json': JSON.stringify(MemoryManifest), 'Src/Main.luau': Source });
        const Memory = await Owner.evaluate(() => self.LuiOwnerWork);
        Assert.ok(Memory.PeakMemoryBytes > 33554432 && Memory.PeakMemoryBytes <= 268435456);
        console.log(`[LUI:BrowserTest] ${Engine} bounded heap growth after long compiler input ${Memory.PeakMemoryBytes} bytes`);
        await Page.evaluate(() => window.Probe.Dispose());
        Stage = 'Actual WASM worker trap';
        await Page.evaluate(async () => {
            const { BrowserHost } = await import('./Host.mjs'); window.Probe = new BrowserHost(document.getElementById('Application'));
            window.Probe.State = 'Starting'; window.Probe.Connect(new URL('./Trap.mjs', location.href)); window.Probe.Send('Initialize', { Capture: '{}' });
        });
        await WaitFor(Page, () => window.Probe.State === 'Failed');
        Assert.equal(await Page.evaluate(() => window.Probe.FailureCode), 'WorkerError');
        Assert.equal(await Page.evaluate(() => window.Probe.Nodes.size + window.Probe.Timers.size + window.Probe.Events.length), 0);
        Assert.deepEqual(Errors, []);
        Assert.equal(ModuleCanarySeen, false);
        console.log(`[LUI:BrowserTest] ${Engine} shared semantics, pointer coordinates/cancellation, focus/collection retirement, nested scroll, live malformed channels and generation isolation passed`);
    } catch (Error) {
        const State = await Page.evaluate(() => ({ State: window.Probe?.State, Failure: window.Probe?.FailureCode, Revision: window.Probe?.Revision, Ack: window.Probe?.Acknowledged,
            Pending: Boolean(window.Probe?.Pending), Events: window.Probe?.Events.length, Pointer: document.querySelector('[aria-label="Pointer state"]')?.textContent,
            Resident: window.Resident && { Connected: window.Resident.isConnected, Focused: window.Resident === document.activeElement, Text: window.Resident.textContent } })).catch(() => ({}));
        console.error(`[LUI:BrowserTest] ${Engine} acceptance failed at ${Stage}: ${Error.stack.slice(0, 1200)} ${JSON.stringify(State)}`); throw Error;
    }
    finally { await Browser.close(); }
}
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    try { for (const Engine of (process.env.LUI_BROWSER_ENGINES || 'chromium,firefox,webkit').split(',')) await Run(Engine); }
    finally { Server.close(); }
}
Main().catch(() => { process.exitCode = 1; Server.close(); });
