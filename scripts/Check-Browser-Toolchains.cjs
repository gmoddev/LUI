const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Child = require('node:child_process');
const Root = Path.resolve(__dirname, '..');
const Pins = JSON.parse(Fs.readFileSync(Path.join(Root, 'tests/browser/Toolchain.json'), 'utf8'));
Assert.equal(process.version, 'v' + Fs.readFileSync(Path.join(Root, '.node-version'), 'utf8').trim());
const Emcc = Child.execFileSync('emcc', ['--version'], { encoding: 'utf8' });
Assert.ok(Emcc.split('\n')[0].includes(Pins.EmsdkVersion));
if (process.env.EMSDK) Assert.equal(Child.execFileSync('git', ['-C', process.env.EMSDK, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(), Pins.EmsdkCommit);
const Module = process.env.LUI_PLAYWRIGHT_MODULE || 'playwright';
const Directory = Path.dirname(require.resolve(Module));
Assert.equal(JSON.parse(Fs.readFileSync(Path.join(Directory, 'package.json'), 'utf8')).version, Pins.PlaywrightVersion);
const Core = Path.dirname(require.resolve('playwright-core', { paths: [Directory] }));
const Browsers = JSON.parse(Fs.readFileSync(Path.join(Core, 'browsers.json'), 'utf8')).browsers;
for (const [Name, Key] of [['chromium', 'ChromiumRevision'], ['firefox', 'FirefoxRevision'], ['webkit', 'WebKitRevision']]) {
    Assert.equal(Browsers.find(Browser => Browser.name === Name).revision, Pins[Key]);
}
console.log(`[LUI:BrowserTest] Node ${process.version}, Emscripten ${Pins.EmsdkVersion}, Playwright ${Pins.PlaywrightVersion} and all browser revisions match pins`);
