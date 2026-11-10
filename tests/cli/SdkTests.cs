using Lui.Cli;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

internal static class SdkTests
{
    internal static void Run(string TempRoot, string Checker, string Runtime)
    {
        static (int Code, string Text) Invoke(params string[] Arguments)
        {
            using StringWriter Output = new(); using StringWriter Errors = new();
            int Code = CliProgram.Run(Arguments, Output, Errors);
            return (Code, Output.ToString() + Errors.ToString());
        }
        static void Require(bool Value, string Message) { if (!Value) throw new Exception("[LUI:SDKTest] " + Message); }
        string Root = Path.Combine(TempRoot, "SDK with spaces μ");
        Directory.CreateDirectory(Root);
        Dictionary<string, string> Components = new()
        {
            ["Cli"] = "Cli/lui.exe", ["Checker"] = "Tools/" + Path.GetFileName(Checker), ["Definitions"] = "Tools/LUI.d.luau",
            ["Runtime"] = "Native/" + Path.GetFileName(Runtime), ["PreviewHost"] = "Preview/preview.exe",
            ["NativeHost"] = "Native/Lui.WinUI.exe", ["ProjectTool"] = "Tools/" + (OperatingSystem.IsWindows() ? "LuiProjectTool.exe" : "LuiProjectTool"),
        };
        foreach (string File in Components.Values) { Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(Root, File))!); System.IO.File.WriteAllText(Path.Combine(Root, File), "Synthetic, not executed"); }
        File.Copy(Checker, Path.Combine(Root, Components["Checker"]), true);
        File.Copy(Path.Combine(Path.GetDirectoryName(Checker)!, "LUI.d.luau"), Path.Combine(Root, Components["Definitions"]), true);
        if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(Path.Combine(Root, Components["Checker"]), File.GetUnixFileMode(Checker));
        Dictionary<string, string> Files = Components.Values.Distinct().ToDictionary(Name => Name, Name => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(Root, Name)))).ToLowerInvariant());
        string Inventory = JsonSerializer.Serialize(new { SchemaVersion = 1, SdkVersion = "0.1.0-preview.1", Target = OperatingSystem.IsWindows() ? "WindowsX64" : "LinuxX64", Components, Files });
        string Metadata = Path.Combine(Root, "SdkManifest.json"); File.WriteAllText(Metadata, Inventory);
        Require(Invoke("doctor", "--sdk", Root, "--format", "json").Code == 0, "valid inventory refused");
        string Project = Path.Combine(TempRoot, "SdkProject");
        Require(Invoke("new", Project, "--sdk", Root).Code == 0, "SDK scaffold failed");
        string Manifest = Path.Combine(Project, "lui.json");
        Require(File.Exists(Path.Combine(Project, "Src/Main.luau")) && File.Exists(Path.Combine(Project, "LuiToolchain.json")), "scaffold omitted pin/entry");
        Require(Invoke("new", Project, "--sdk", Root).Code == 1, "existing project overwritten");
        Require(Invoke("check", Manifest, "--sdk", Root).Code == 0, "modular starter failed inferred project checking");
        Require(Invoke("run", Manifest, "--sdk", Root, "--host", "unrelated.exe").Code == 1, "mixed host accepted");
        File.WriteAllText(Path.Combine(Project, "Src/Main.luau"), "error('CHECK_MUST_NOT_EXECUTE')");
        Require(Invoke("check", Manifest, "--sdk", Root).Code == 0, "static check executed the application");
        JsonNode Restricted = JsonNode.Parse(File.ReadAllText(Manifest))!; Restricted["Diagnostics"]!["Mode"] = "Restricted";
        File.WriteAllText(Manifest, Restricted.ToJsonString());
        Require(Invoke("check", Manifest, "--sdk", Root).Code == 1, "Restricted project silently downgraded");
        string Pin = Path.Combine(Project, "LuiToolchain.json");
        File.WriteAllText(Pin, File.ReadAllText(Pin).Replace("0.1.0-preview.1", "0.1.0-preview.2"));
        Require(Invoke("doctor", Manifest, "--sdk", Root).Code == 1, "different pinned SDK accepted");
        File.WriteAllText(Metadata, Inventory.Replace("\"SchemaVersion\":1", "\"SchemaVersion\":1,\"Extra\":true"));
        Require(Invoke("doctor", "--sdk", Root).Code == 1, "unknown metadata field accepted");
        File.WriteAllText(Metadata, Inventory.Replace("\"SchemaVersion\":1", "\"SchemaVersion\":1,\"SchemaVersion\":1"));
        Require(Invoke("doctor", "--sdk", Root).Code == 1, "duplicate metadata field accepted");
        File.WriteAllText(Metadata, Inventory);
        File.AppendAllText(Path.Combine(Root, Components["Definitions"]), "tampered");
        Require(Invoke("doctor", "--sdk", Root).Code == 1, "modified SDK component accepted");
    }
}
