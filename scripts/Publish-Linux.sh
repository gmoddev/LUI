#!/bin/sh
# Build a schema v2 GTK application folder. Distribution GTK/OpenSSL and their
# transitive libraries remain system prerequisites; this is not an AppImage.
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    printf '%s\n' '[LUI:Package] Usage: Publish-Linux.sh <manifest> <new output directory> [build directory]' >&2
    exit 1
fi
Root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
Manifest=$1
Output=$2
Build=${3:-"$Root/build/linux-release"}
cmake -S "$Root" -B "$Build" -DCMAKE_BUILD_TYPE=Release -DLUI_BUILD_GTK4=ON
cmake --build "$Build" --parallel 2 --target LuiGtkHost LuiProjectTool
Host=$(mktemp -d "$Build/published-gtk.XXXXXX")
cmake --install "$Build" --prefix "$Host" --component GtkHost
dotnet build "$Root/tools/cli/Lui.Cli.csproj" --configuration Release -m:2
dotnet "$Root/tools/cli/bin/Release/net9.0/lui.dll" build "$Manifest" --mode project --host "$Host" --project-tool "$Build/LuiProjectTool" --output "$Output"
printf '%s\n' '[LUI:Package] Run Start-LUI.sh in the output folder'
