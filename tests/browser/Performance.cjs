const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Http = require('node:http');
const Playwright = require(process.env.LUI_PLAYWRIGHT_MODULE || 'playwright');
const { WaitFor, LaunchOptions } = require('./Harness.cjs');
const Build = Path.resolve(process.argv[2]);
const Types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png' };
const Server = Http.createServer((Request, Response) => {
    const File = Path.resolve(Build, '.' + decodeURIComponent(new URL(Request.url, 'http://localhost').pathname));
    if (!File.startsWith(Build + Path.sep) || !Fs.existsSync(File) || !Fs.statSync(File).isFile()) { Response.writeHead(404).end(); return; }
    Response.setHeader('Content-Type', Types[Path.extname(File)] || 'application/octet-stream');
    Response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; img-src blob:; connect-src 'self'; object-src 'none'; base-uri 'none'");
    Response.end(Fs.readFileSync(File));
});
async function Run(Engine) {
    const Browser = await Playwright[Engine].launch(LaunchOptions(Engine));
    try {
        const Page = await Browser.newPage({ viewport: { width: 1200, height: 900 } });
        const Errors = []; Page.on('pageerror', () => Errors.push('PageError'));
        let Owner; Page.on('worker', Worker => { Owner = Worker; });
        const Base = `http://127.0.0.1:${Server.address().port}`;
        await Page.goto(Base + '/Probe.html'); await Page.bringToFront();
        await WaitFor(Page, () => Boolean(window.Probe) && document.visibilityState === 'visible');
        const Start = await Page.evaluate(async Address => {
            const Begin = performance.now();
            await window.Probe.Load(new URL(Address));
            const Deadline = Begin + 10000;
            while (!window.Probe.Revision || window.Probe.Acknowledged !== window.Probe.Revision || window.Probe.Images.size || window.Probe.ImageActive ||
                ![...window.Probe.Nodes.values()].some(Node => Node.Class === 'TextLabel' && /10000 messages/.test(Node.Element.textContent))) {
                if (window.Probe.State === 'Failed' || performance.now() > Deadline) throw new Error('StartupBudget');
                await new Promise(Resolve => setTimeout(Resolve, 1));
            }
            return performance.now() - Begin;
        }, Base + '/Packages/Conversation/lui.browser.json');
        await WaitFor(Page, () => [...window.Probe.Nodes.values()].some(Node => /10000 messages/.test(Node.Element.textContent)));
        const Samples = await Page.evaluate(async () => {
            const Host = window.Probe, Times = [];
            for (let Index = 0; Index < 200; ++Index) {
                const Name = Index % 2 ? 'Edit visible' : 'Insert above';
                const Button = [...Host.Nodes.values()].find(Node => Node.Class === 'TextButton' && Node.Control.textContent === Name);
                const Before = Host.Frames, Begin = performance.now();
                Button.Control.click();
                while (Host.Frames === Before || Host.Events.length || Host.Activations.size || Host.Pending || Host.Awaiting || Host.Acknowledged !== Host.Revision) {
                    if (Host.State !== 'Running' || performance.now() - Begin > 5000) throw new Error('InputAdmission');
                    await new Promise(Resolve => setTimeout(Resolve, 1));
                }
                Times.push(performance.now() - Begin);
            }
            return Times;
        });
        Samples.sort((A, B) => A - B);
        const Summary = { StartupMs: Start, Samples: Samples.length, MedianMs: Samples[99], P95Ms: Samples[189], P99Ms: Samples[197], MaxMs: Samples[199] };
        const Work = await Owner.evaluate(() => self.LuiOwnerWork);
        const State = await Page.evaluate(() => ({ Nodes: window.Probe.Nodes.size, Status: [...window.Probe.Nodes.values()].filter(Node => Node.Class === 'TextLabel').map(Node => Node.Element.textContent).find(Text => /messages \|/.test(Text)) }));
        console.log(`[LUI:BrowserTest] ${Engine} in-page input-to-acknowledged-DOM ${JSON.stringify(Summary)} owner work ${JSON.stringify(Work)} bounds ${JSON.stringify(State)}`);
        Assert.equal(Summary.Samples, 200); Assert.ok(Start <= 10000); Assert.ok(Summary.P95Ms <= 50); Assert.ok(Summary.P99Ms <= 250);
        Assert.ok(Work.MemoryBytes >= 33554432 && Work.PeakMemoryBytes <= 268435456);
        Assert.ok(State.Nodes <= 256); Assert.match(State.Status, /^10100 messages \| (\d+) resident rows/);
        Assert.ok(Number(State.Status.match(/\| (\d+) resident rows/)[1]) <= 48);
        Assert.deepEqual(Errors, []);
        await Page.evaluate(() => window.Probe.Dispose());
        Assert.equal(await Page.evaluate(() => window.Probe.Nodes.size + window.Probe.Events.length + window.Probe.Timers.size + window.Probe.Assets.size + window.Probe.ImageCache.size), 0);
    } finally { await Browser.close(); }
}
async function Main() {
    await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
    try { for (const Engine of (process.env.LUI_BROWSER_ENGINES || 'chromium,firefox,webkit').split(',')) await Run(Engine); }
    finally { Server.close(); }
}
Main().catch(Error => { console.error(`[LUI:BrowserTest] Performance qualification failed: ${Error.name}`); process.exitCode = 1; Server.close(); });
