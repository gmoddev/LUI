#!/usr/bin/env bash
# Android native build/host gate. SDK installation, device installation and publication
# are separate actions. Invoke with bash; artifacts stay in the supplied output.
set -euo pipefail
Root="$(cd "$(dirname "$0")/.." && pwd)"
Output="$(realpath -m "${1:-$Root/build/mobile-8a}")"
Sdk="${ANDROID_HOME:?Set ANDROID_HOME to the selected Android SDK}"
Gradle="${LUI_GRADLE:?Set LUI_GRADLE to the pinned Gradle 8.11.1 executable}"
Cmake="$Sdk/cmake/3.31.6/bin/cmake"
mkdir -p "$Output/Assets" "$Output/Evidence"
test -x "$Cmake" && test -x "$Gradle"
test "$(sed -n 's/^Pkg.Revision *= *//p' "$Sdk/ndk/28.2.13676358/source.properties" | tr -d '\r')" = 28.2.13676358
"$Gradle" --version > "$Output/Evidence/Gradle.txt"
grep -q '^Gradle 8.11.1$' "$Output/Evidence/Gradle.txt"
java -version 2> "$Output/Evidence/Java.txt"
grep -Eq 'version "17\.' "$Output/Evidence/Java.txt"
Arguments=()
GradleArguments=()
if test -n "${LUI_LUAU_SOURCE:-}"; then
  Revision="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["LuauRevision"])' "$Root/cmake/Dependencies.json")"
  test "$(git -c safe.directory="$LUI_LUAU_SOURCE" -C "$LUI_LUAU_SOURCE" rev-parse HEAD)" = "$Revision"
  git -c safe.directory="$LUI_LUAU_SOURCE" -C "$LUI_LUAU_SOURCE" diff --quiet HEAD
  Arguments+=("-DLUAU_SOURCE_DIR=$LUI_LUAU_SOURCE")
  GradleArguments+=("-PLuiLuauSource=$LUI_LUAU_SOURCE")
fi
if test -n "${LUI_JSON_SOURCE:-}"; then
  Arguments+=("-DFETCHCONTENT_SOURCE_DIR_LUIJSON=$LUI_JSON_SOURCE")
  GradleArguments+=("-PLuiJsonSource=$LUI_JSON_SOURCE")
fi
"$Cmake" -S "$Root" -B "$Output/native" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$Sdk/cmake/3.31.6/bin/ninja" -DCMAKE_BUILD_TYPE=Release \
  -DLUI_BUILD_MOBILE_FEASIBILITY=ON "${Arguments[@]}"
"$Cmake" --build "$Output/native" --target LuiMobileHostTests LuiMobileSchedulerTests LuiMobileStorageTests LuiMobileDomainTests LuiMobilePersistenceTests LuiMobileStorageFilesTests LuiMobileWorkloadTests -j2
python3 "$Root/tests/mobile/MakeWorkloads.py" --source "$Root" --output "$Output/Captures"
export LUI_MOBILE_CAPTURES="$Output/Captures"
ctest --test-dir "$Output/native" --output-on-failure | tee "$Output/Evidence/HostTests.txt"
"$Output/native/LuiMobileHostTests" --bundle "$Output/Assets/Fixture.json"
"$Output/native/LuiMobileWorkloadTests" --bundle "$Output/Assets"
mkdir -p "$Output/Assets/THIRD-PARTY-LICENSES"
cp "$Root/LICENSE" "$Output/Assets/LICENSE"
for Name in Json PicoSHA2 Kotlin JetBrainsAnnotations; do
  cp "$Root/third_party/licenses/$Name.txt" "$Output/Assets/THIRD-PARTY-LICENSES/$Name.txt"
done
LuauSource="${LUI_LUAU_SOURCE:-$Output/native/_deps/luau-src}"
cp "$LuauSource/LICENSE.txt" "$Output/Assets/THIRD-PARTY-LICENSES/Luau.txt"
cp "$Sdk/ndk/28.2.13676358/NOTICE" "$Output/Assets/THIRD-PARTY-LICENSES/AndroidNdk.txt"
cp "$Sdk/ndk/28.2.13676358/NOTICE.toolchain" "$Output/Assets/THIRD-PARTY-LICENSES/AndroidNdkToolchain.txt"
"$Gradle" -p "$Root/backends/android" --no-daemon --max-workers=2 \
  "-PLuiFixtureDirectory=$Output/Assets" "${GradleArguments[@]}" \
  assembleDebug assembleDebugAndroidTest | tee "$Output/Evidence/AndroidBuild.txt"
Apk="$Root/backends/android/app/build/outputs/apk/debug/app-debug.apk"
"$Sdk/build-tools/36.0.0/zipalign" -c -P 16 -v 4 "$Apk" > "$Output/Evidence/ZipAlignment.txt"
python3 "$Root/tests/mobile/Package.py" "$Apk" | tee "$Output/Evidence/Package.txt"
sha256sum "$Apk" "$Root/backends/android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk" \
  "$Output/Assets/Fixture.json" > "$Output/Evidence/Artifacts.sha256"
echo '[LUI:Mobile] Build gate passed; physical devices, Android lifecycle tests and iOS remain separate gates'
