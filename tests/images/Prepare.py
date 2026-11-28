"""Generate deterministic declared-asset projects for native image qualification."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import zlib


def Png(Width, Height, Seed, Atlas=False):
    def Chunk(Kind, Data):
        return struct.pack(">I", len(Data)) + Kind + Data + struct.pack(">I", zlib.crc32(Kind + Data))

    Pixels = bytearray()
    for Y in range(Height):
        Pixels.append(0)
        for X in range(Width):
            Tile = X // 24 + Y // 24 * 8 if Atlas else Seed
            LocalX, LocalY = (X % 24, Y % 24) if Atlas else (X, Y)
            Color = (Tile * 73 + LocalX % 5, Tile * 29 + LocalY % 7, Tile * 17 + Tile // 256)
            Alpha = 0 if LocalX < 2 or LocalY < 2 else 255
            Pixels.extend((*[Channel % 256 for Channel in Color], Alpha))
    Header = struct.pack(">IIBBBBB", Width, Height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + Chunk(b"IHDR", Header) + Chunk(b"IDAT", zlib.compress(Pixels)) + Chunk(b"IEND", b"")


def VendorWorkers(Root, Modules):
    Libraries = Path(__file__).resolve().parents[2] / "libraries"
    Pins = []
    for Alias in ("Workers", "Messages", "Json"):
        Package = Libraries / Alias
        Metadata = json.loads((Package / "Library.json").read_bytes())
        Relative = f"Vendor/{Alias}"
        Target = Root / Relative
        Target.mkdir(parents=True)
        Files = []
        for Entry in Metadata["Modules"]:
            Name = Entry["Name"] + ".luau"
            Bytes = (Package / Name).read_bytes()
            (Target / Name).write_bytes(Bytes)
            Files.append({"Path": Name, "Sha256": hashlib.sha256(Bytes).hexdigest()})
        Modules["Dependencies"].append({"Alias": Alias, "Root": Relative,
            "Requires": Metadata.get("Requires", []), "Files": [File["Path"] for File in Files]})
        Pins.append({"Alias": Alias, "Provenance": {"Kind": "LocalVendor", "Id": "lui-qualification"}, "Files": Files})
    Modules["Requires"].append("Workers")
    Modules["Lock"] = "lui.lock.json"
    (Root / Modules["Lock"]).write_text(json.dumps({"SchemaVersion": 1, "Packages": Pins}), encoding="utf-8")


def Prepare(Root, Case, Count, Frames=False, Gated=False, FrameProfile=None, DesktopMarker=False, MatchedCpu=False):
    Root.mkdir(parents=True, exist_ok=False)
    Assets = []

    def Write(Name, Data):
        Assets.append(Name)
        (Root / Name).write_bytes(Data)

    if Case in ("ClippedAtlas", "Atlas", "Combined"):
        Write("Atlas.png", Png(192, 192, 0, Atlas=True))
    if Case not in ("ClippedAtlas", "Atlas"):
        for Index in range(1, Count + 1):
            Size = (16, 24, 32, 64)[(Index - 1) % 4]
            Write(f"Icon{Index}.png", Png(Size, Size, Index))
    if Case == "Thumbnails":
        for Index in range(1, 190):
            Width, Height = ((96, 64), (160, 120), (256, 192), (384, 256), (768, 512))[Index % 5]
            Write(f"Thumb{Index}.png", Png(Width, Height, Index))
        Write("Large.png", Png(1024, 1024, 11))
        Write("Excessive.png", Png(1025, 1024, 12))
        Write("Malformed.png", b"malformed")
    if Case == "Lifecycle":
        Write("Malformed.png", b"malformed")
        Width, Height = 1025, 1024
        Write("Excessive.png", Png(Width, Height, 12))
    Manifest = {
        "SchemaVersion": 2, "Script": "Main.luau", "Capabilities": [],
        "Extensions": [], "Assets": Assets, "ExtensionDeclarations": [],
        "Diagnostics": {"Mode": "Development"},
        "Modules": {"Root": ".", "Files": ["Main.luau"], "Requires": [], "Dependencies": []},
    }
    SourceRoot = Root
    if Case == "Combined":
        SourceRoot = Root / "Src"
        SourceRoot.mkdir()
        Manifest["Script"] = "Src/Main.luau"
        Manifest["Modules"]["Root"] = "Src"
        Manifest["Sandbox"] = {"MaxMemoryBytes": 33554432, "MaxInterrupts": 1000000}
        VendorWorkers(Root, Manifest["Modules"])
        Manifest["Modules"]["Files"].append("Compute.luau")
        (SourceRoot / "Compute.luau").write_text(
            """local Workers = require("@Workers/Workers")
return Workers.Handler(function(Limit)
    local Sum = 0
    for Index = 1, Limit do Sum = (Sum + Index * 17) % 65521 end
    return Sum
end)
""", encoding="utf-8")
    (Root / "lui.json").write_text(json.dumps(Manifest), encoding="utf-8")
    Legacy = {"SchemaVersion": 1, "Script": Manifest["Script"], "Assets": Assets, "Extensions": [], "Capabilities": []}
    (Root / "Legacy.json").write_text(json.dumps(Legacy), encoding="utf-8")
    Body = Path(__file__).with_name("Workload.luau").read_text(encoding="utf-8")
    Parameters = f'local Case, Count, WithFrames, GateFrames = "{Case}", {Count}, {str(Frames).lower()}, {str(Gated).lower()}\n'
    Width, Height, Rate, Seconds = FrameProfile or (1280, 720, 30, 4)
    Parameters += f"local FrameWidth, FrameHeight, FrameHz, FrameSeconds = {Width}, {Height}, {Rate}, {Seconds}\n"
    Parameters += f"local MeasureFramePacing = {str(FrameProfile is not None).lower()}\n"
    Parameters += f"local ObserveDesktopMarker = {str(DesktopMarker).lower()}\n"
    Parameters += f"local MeasureMatchedCpu = {str(MatchedCpu).lower()}\n"
    if Case == "Combined":
        Parameters += 'local Workers = require("@Workers/Workers")\n'
    if Case == "RuntimeSources":
        # Synthetic 2x2 grayscale JPEG, embedded to avoid an encoder dependency.
        Jpeg = base64.b64decode(
            "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/"
            "wAALCAACAAIBAREA/8QAFAABAAAAAAAAAAAAAAAAAAAABP/EABQQAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQEAAD8AO//Z"
        )
        Encoded = ['"' + ''.join(f"\\x{Byte:02x}" for Byte in Data) + '"' for Data in (Png(4, 4, 1), Jpeg)]
        Parameters += f"local PngBytes, JpegBytes = {', '.join(Encoded)}\n"
    (SourceRoot / "Main.luau").write_text(Parameters + Body, encoding="utf-8")


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Output", type=Path)
    Parser.add_argument("--frames", action="store_true")
    Parser.add_argument("--gated-frames", action="store_true")
    Parser.add_argument("--lifecycle", action="store_true")
    Parser.add_argument("--runtime-thumbnails", action="store_true")
    Parser.add_argument("--runtime-sources", action="store_true")
    Parser.add_argument("--combined", action="store_true")
    Parser.add_argument("--frame-profiles", action="store_true",
                        help="stage fixed 30/60 Hz and overproduction profiles")
    Parser.add_argument("--presentation-profiles", action="store_true",
                        help="stage desktop-marked frame profiles and the sustained 720p60 pair")
    Parser.add_argument("--matched-throughput", action="store_true",
                        help="stage paired 720p30/60 CPU profiles")
    Parser.add_argument("--matched-duration", type=int, default=38,
                        help="seconds to run each matched fixture, including warmup")
    Parser.add_argument("--display-smoke", action="store_true",
                        help="stage a short 720p60 profile with encoded frame sequences")
    Options = Parser.parse_args()
    if Options.display_smoke:
        Prepare(Options.Output / "Frame720p60Display", "FrameOnly", 0, Frames=True,
                FrameProfile=(1280, 720, 60, 8), DesktopMarker=True)
        return
    if Options.matched_throughput:
        if not 8 <= Options.matched_duration <= 55:
            Parser.error("--matched-duration must be between 8 and 55 seconds")
        for Rate in (30, 60):
            Prepare(Options.Output / f"Frame720p{Rate}Matched", "FrameOnly", 0, Frames=True,
                    FrameProfile=(1280, 720, Rate, Options.matched_duration), MatchedCpu=True)
            Prepare(Options.Output / f"Combined720p{Rate}Matched", "Combined", 256, Frames=True,
                    FrameProfile=(1280, 720, Rate, Options.matched_duration), MatchedCpu=True)
        return
    if Options.presentation_profiles:
        for Height, Width, Rate, Seconds, Suffix in (
            (720, 1280, 30, 20, ""), (720, 1280, 60, 20, ""), (720, 1280, 90, 20, ""),
            (1080, 1920, 60, 20, ""), (720, 1280, 60, 65, "Sustained"),
        ):
            Prepare(Options.Output / f"Frame{Height}p{Rate}{Suffix}", "FrameOnly", 0, Frames=True,
                    FrameProfile=(Width, Height, Rate, Seconds), DesktopMarker=True)
        Prepare(Options.Output / "Combined720p60", "Combined", 256, Frames=True,
                FrameProfile=(1280, 720, 60, 20), DesktopMarker=True)
        Prepare(Options.Output / "Combined720p60Sustained", "Combined", 256, Frames=True,
                FrameProfile=(1280, 720, 60, 65), DesktopMarker=True)
        return
    if Options.frame_profiles:
        for Width, Height in ((640, 360), (1280, 720), (1920, 1080)):
            for Rate in (30, 60):
                Prepare(Options.Output / f"Frame{Height}p{Rate}", "FrameOnly", 0,
                        Frames=True, FrameProfile=(Width, Height, Rate, 20))
        Prepare(Options.Output / "Frame720p90", "FrameOnly", 0,
                Frames=True, FrameProfile=(1280, 720, 90, 8))
        Prepare(Options.Output / "Frame720p60Gated", "FrameOnly", 0,
                Frames=True, Gated=True, FrameProfile=(1280, 720, 60, 20))
        for Rate in (30, 60):
            Prepare(Options.Output / f"Combined720p{Rate}", "Combined", 256,
                    Frames=True, FrameProfile=(1280, 720, Rate, 20))
        return
    if Options.combined:
        Prepare(Options.Output / "Combined", "Combined", 256, Options.frames or Options.gated_frames, Options.gated_frames)
        return
    if Options.runtime_thumbnails:
        Prepare(Options.Output / "RuntimeThumbnails", "RuntimeThumbnails", 0, Options.frames)
        return
    if Options.runtime_sources:
        Prepare(Options.Output / "RuntimeSources", "RuntimeSources", 0)
        return
    if Options.lifecycle:
        Prepare(Options.Output / "Lifecycle", "Lifecycle", 4)
        return
    if Options.gated_frames:
        for Case, Count in (("FrameOnly", 0), ("Explorer", 64), ("Static", 64), ("Thumbnails", 64)):
            Prepare(Options.Output / ("Static64" if Case == "Static" else Case), Case, Count, True, True)
        return
    for Count in (16, 32, 64, 128, 256, 1024):
        Prepare(Options.Output / f"Static{Count}", "Static", Count, Options.frames)
    for Case in ("Explorer", "ClippedAtlas", "Atlas", "Thumbnails"):
        Prepare(Options.Output / Case, Case, 64, Options.frames)
    if Options.frames:
        Prepare(Options.Output / "FrameOnly", "FrameOnly", 0, True)


if __name__ == "__main__":
    Main()
