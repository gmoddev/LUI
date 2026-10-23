#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
Build=$(realpath -m "${1:-build/browser}")
Native=$(realpath -m "${2:-build/browser-tools-native}")
if (($# >= 2)); then shift 2; else set --; fi
node scripts/Check-Browser-Toolchains.cjs
cmake -S . -B "$Native" -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "$Native" --parallel 2 --target LuiBrowserTool LuiCapturedProjectTests
ctest --test-dir "$Native" -R '^LuiCapturedProjectTests$' --output-on-failure --no-tests=error
emcmake cmake -S . -B "$Build" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$Build" --parallel 2 --target LuiBrowserProbe
Stage=$(mktemp -d "$Build/qualification.XXXXXX")
for Name in LuiBrowser.mjs LuiBrowser.wasm Worker.mjs Host.mjs Protocol.mjs Deployment.mjs Application.mjs Entry.mjs Probe.html Probe.css LICENSE; do cp "$Build/$Name" "$Stage/$Name"; done
cp -a "$Build/THIRD-PARTY-LICENSES" "$Stage/THIRD-PARTY-LICENSES"
mkdir "$Stage/Packages" "$Stage/Inputs"
cp -a examples/conversation "$Stage/Inputs/Conversation"
python3 "$Stage/Inputs/Conversation/Prepare.py"
"$Native/LuiBrowserTool" examples/modular/lui.json "$Stage" "$Stage/Packages/Modular"
"$Native/LuiBrowserTool" "$Stage/Inputs/Conversation/lui.browser.json" "$Stage" "$Stage/Packages/Conversation"
node tests/browser/Protocol.mjs
node tests/browser/Artifacts.cjs "$Stage"
python3 tests/browser/Packaging.py "$Native/LuiBrowserTool" "$Stage"
node tests/browser/Scheduling.cjs "$Stage"
node tests/browser/Probe.cjs "$Stage"
node tests/browser/Conformance.cjs "$Stage"
node tests/browser/Acceptance.cjs "$Stage"
node tests/browser/Packages.cjs "$Stage"
node tests/browser/Policy.cjs "$Stage"
node tests/browser/Performance.cjs "$Stage"
echo "[LUI:BrowserTest] Automated browser pass completed in $Stage"
if [[ ${LUI_BROWSER_NATIVE_INPUT:-0} == 1 ]]; then
    bash tests/browser/NativeInput.sh "$Stage"
fi
