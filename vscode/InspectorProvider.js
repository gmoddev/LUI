'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const Crypto = require('node:crypto');
const { GetProperties } = require('./PreviewModel');

class InspectorProvider {
    constructor(OnAction) {
        this.OnAction = OnAction;
        this.View = null;
        this.State = { status: 'Start a preview to inspect Instances.', generation: 0,
            nodes: [], selectedId: 0, properties: [], error: '' };
    }

    resolveWebviewView(View) {
        this.View = View;
        View.webview.options = { enableScripts: true };
        const Nonce = Crypto.randomBytes(16).toString('hex');
        View.webview.html = Fs.readFileSync(Path.join(__dirname, 'Inspector.html'), 'utf8')
            .replaceAll('__NONCE__', Nonce);
        View.webview.onDidReceiveMessage(Message => {
            if (Message?.type === 'ready') this.Post();
            else this.OnAction(Message);
        });
        View.onDidDispose(() => { if (this.View === View) this.View = null; });
        this.Post();
    }

    Render(Tree, Generation, SelectedId, Classes, Status, Error) {
        this.State = { status: Status, generation: Generation, nodes: Tree.Nodes,
            selectedId: SelectedId, properties: GetProperties(Tree.ById.get(SelectedId), Classes), error: Error };
        this.Post();
    }

    Post() {
        if (this.View) this.View.webview.postMessage({ type: 'state', ...this.State });
    }
}

module.exports = { InspectorProvider };
