// Protocol/DOM state is independent of compositor ticks. Physical painting and
// input remain separate checks; timer polling does not assert that pixels painted.
exports.WaitFor = (Page, Predicate, Argument, Options = {}) =>
    Page.waitForFunction(Predicate, Argument, { polling: 10, ...Options });
exports.LaunchOptions = Engine => ({
    headless: !process.env.LUI_BROWSER_HEADED,
    ...(Engine === 'chromium' && process.env.LUI_BROWSER_SOFTWARE ? { args: ['--ozone-platform=x11', '--disable-gpu'] } : {}),
});

// A protocol-ready DOM is not physical-paint evidence. Foreground pointer/keyboard
// checks first require real main-world compositor callbacks, without forced clicks.
exports.WaitForPaint = Page => Page.evaluate(() => new Promise((Resolve, Reject) => {
    let Frame;
    const Deadline = setTimeout(() => { cancelAnimationFrame(Frame); Reject(new Error('PaintUnavailable')); }, 5000);
    Frame = requestAnimationFrame(() => {
        Frame = requestAnimationFrame(() => { clearTimeout(Deadline); Resolve(); });
    });
}));
