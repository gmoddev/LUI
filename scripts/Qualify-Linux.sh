#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
Build=${1:-build/linux-x64}
if (($#)); then shift; fi
Build=$(realpath -m "$Build")
python3 scripts/Check-Toolchains.py --node --rust
cmake -S . -B "$Build" -DCMAKE_BUILD_TYPE=Debug -DLUI_BUILD_RUST_PROOF=ON "$@"
cmake --build "$Build" --parallel 2 --target LuiNativeQualification
ctest --test-dir "$Build" -C Debug --output-on-failure --no-tests=error -E '^LuiGtkTests$'
dotnet run --project tests/manifest/ManifestTests.csproj --configuration Release -- "$PWD"
dotnet run --project tests/packaging/PackagingTests.csproj --configuration Release
dotnet run --project tests/diagnostics/DiagnosticsTests.csproj --configuration Release
dotnet build tools/cli/Lui.Cli.csproj --configuration Release
dotnet run --project tests/cli/CliTests.csproj --configuration Release -- "$Build/libLuiRuntime.so" "$Build/LuiTypeCheck"
Cli="$PWD/tools/cli/bin/Release/net10.0/lui.dll"
dotnet "$Cli" check examples/dashboard/dashboard.manifest.json --checker "$Build/LuiTypeCheck"
dotnet "$Cli" check examples/native-integration/lui.json --checker "$Build/LuiTypeCheck" --mode project --diagnostics development
dotnet build tools/preview-host/Lui.PreviewHost.csproj --configuration Release
Preview="$PWD/tools/preview-host/bin/Release/net10.0/lui-preview-host.dll"
dotnet run --project tests/preview/PreviewTests.csproj --configuration Release -- "$Preview" "$Build/libLuiRuntime.so"
LUI_TEST_PREVIEW_HOST="$Preview" LUI_TEST_RUNTIME="$Build/libLuiRuntime.so" LUI_TEST_CLI="$Cli" LUI_TEST_TYPECHECK="$Build/LuiTypeCheck" \
    node --test tests/editor/EditorTests.js tests/editor/TypeTests.js tests/editor/ProjectTests.js
"$Build/LuiSchemaDump" > "$Build/schema.json"
cmp types/schema.json "$Build/schema.json"
echo '[LUI:Build] Linux qualification passed'
