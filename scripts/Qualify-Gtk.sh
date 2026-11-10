#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
Build=${1:-build/gtk4}
if (($#)); then shift; fi
Build=$(realpath -m "$Build")
python3 scripts/Check-Toolchains.py
cmake -S . -B "$Build" -DCMAKE_BUILD_TYPE=Debug -DLUI_BUILD_GTK4=ON "$@"
cmake --build "$Build" --parallel 2 --target LuiGtkQualification
ctest --test-dir "$Build" -R '^LuiGtk(Tests|TemporaryTextTests)$' --output-on-failure --no-tests=error
GDK_SCALE=2 ctest --test-dir "$Build" -R '^LuiGtkTests$' --output-on-failure --no-tests=error
# Keep the X server alive while discovery temporarily closes the last client.
timeout 20s xvfb-run -a -e /dev/stderr -s '-screen 0 1280x1024x24 -noreset' dbus-run-session -- \
    /usr/bin/python3 tests/gtk4/Accessibility.py "$Build/backends/gtk4/lui-gtk" "$PWD/tests/gtk4/a11y/lui.json"
export GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none
dotnet build tools/cli/Lui.Cli.csproj --configuration Release
Cli="$PWD/tools/cli/bin/Release/net10.0/lui.dll"
timeout 15s xvfb-run -a dbus-run-session -- dotnet "$Cli" run tests/gtk4/launch/lui.json --mode project --host "$Build/backends/gtk4"
# A unique parent keeps qualification repeatable without deleting earlier packages.
Stage=$(mktemp -d "$Build/qualification.XXXXXX")
cmake --install "$Build" --prefix "$Stage/host" --component GtkHost
dotnet "$Cli" build tests/gtk4/launch/lui.json --mode project --host "$Stage/host" --project-tool "$Build/LuiProjectTool" --output "$Stage/package"
mv "$Stage/package" "$Stage/GTK relocated package"
dotnet "$Cli" verify "$Stage/GTK relocated package/Application" --project-tool "$Build/LuiProjectTool"
test -f "$Stage/GTK relocated package/LICENSE"
test -f "$Stage/GTK relocated package/THIRD-PARTY-LICENSES/Luau.txt"
timeout 15s xvfb-run -a dbus-run-session -- "$Stage/GTK relocated package/Start-LUI.sh"
echo '[LUI:Build] GTK qualification passed'
