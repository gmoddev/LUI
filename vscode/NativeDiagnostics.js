'use strict';

const Messages = Object.freeze({
    UnknownCapability: 'lui.json Capabilities contains an unknown name. Use network.client for HttpService or network.server for HttpServerService. Service names are not capability names.',
    DuplicateCapability: 'lui.json Capabilities repeats a token. Keep each capability only once.',
    NetworkClientRequired: 'Declare network.client in lui.json Capabilities for outbound HTTP or networking, and use a host that grants it.',
    NetworkServerRequired: 'Declare network.server in lui.json Capabilities for HttpServerService or inbound networking, and use a host that grants it.',
    NetworkRawRequired: 'NetworkService needs network.raw plus network.client or network.server for the operation. HTTP services do not need network.raw.',
    HostServicesRequired: 'This provider needs HostServices and a trusted host installation. A manifest declaration alone does not install it.',
    ClipboardUnavailable: 'ClipboardService needs Clipboard in lui.json Capabilities and a host with clipboard support.',
    DialogsUnavailable: 'DialogService needs Dialogs in lui.json Capabilities and a host with file-picker support.',
    ProviderUnavailable: 'The requested native service is not installed. Check its platform support and host setup; adding a capability alone does not install it.',
    InspectorUnsupported: 'Inspect Layout cannot run privileged capabilities or native extensions. Use Run App to test this project.',
    CapabilityDenied: 'The host did not grant a requested capability. Check supported capabilities and explicit native-provider trust settings.',
    StorageUnavailable: 'Check application-data permissions or close another running copy. Application storage is unavailable; no temporary fallback was granted.',
    InvalidManifest: 'Check lui.json schema, required fields and exact-case declared files, then run Check Types.',
    MissingFile: 'A declared project file is missing. Check lui.json and the exact-case relative filename.',
    CompileFailed: 'A declared source could not be compiled. Run Check Types to locate source errors.',
    OperationFailed: 'Run Check Types and reproduce the last action. Private source and exception text are omitted from this diagnostic.',
    StartupFailed: 'Run Check Types, confirm the selected SDK with lui doctor, and check declared files and host requirements. No detailed startup reason was available.',
});

function ReadDiagnostic(Line) {
    const Prefix = '[LUI:Diagnostic] ';
    if (Line.length > 1024 || !Line.startsWith(Prefix)) return null;
    let Value;
    try { Value = JSON.parse(Line.slice(Prefix.length)); } catch { return null; }
    if (!Value || Array.isArray(Value) || Object.keys(Value).sort().join(',') !== 'Code,Phase,Version' ||
        Value.Version !== 1 || typeof Value.Code !== 'string' || !Object.hasOwn(Messages, Value.Code) ||
        (Value.Phase !== 'Startup' && Value.Phase !== 'Runtime')) return null;
    return { Code: Value.Code, Phase: Value.Phase,
        Message: '[LUI:App] ' + (Value.Phase === 'Startup' ? 'Startup failed. ' : 'An app callback or operation failed; the window may still be open. ') + Messages[Value.Code] };
}

module.exports = { ReadDiagnostic };
