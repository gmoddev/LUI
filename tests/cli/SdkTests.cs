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
        string Unavailable=Path.Combine(TempRoot,"UnavailableTemplate");
        Require(Invoke("new",Unavailable,"--template","workbench","--sdk",Root).Code==1 && !Directory.Exists(Unavailable),"unavailable template left a partial project");
        Require(Invoke("new",Unavailable,"--template","../escape","--sdk",Root).Code==1 && !Directory.Exists(Unavailable),"unknown template accepted");
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
        // A complete synthetic template proves the SDK-only copy and exact pin.
        Dictionary<string,string> Example=new()
        {
            ["lui.json"]="{\"SchemaVersion\":2,\"Script\":\"Src/Main.luau\",\"Capabilities\":[],\"Extensions\":[],\"Assets\":[],\"ExtensionDeclarations\":[],\"Diagnostics\":{\"Mode\":\"Development\"},\"Modules\":{\"Root\":\"Src\",\"Files\":[\"Main.luau\"],\"Requires\":[],\"Dependencies\":[]}}",
            ["Src/Main.luau"]="local Value = 1\n", ["Modules.lock.json"]="{\"SchemaVersion\":1,\"Packages\":[]}\n",
        };
        JsonNode Next=JsonNode.Parse(Inventory)!; Next["SdkVersion"]="0.1.0-preview.2";
        foreach(var Item in Example)
        {
            string Relative="Examples/workbench/"+Item.Key, FilePath=Path.Combine(Root,Relative);
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!); File.WriteAllText(FilePath,Item.Value);
            Next["Files"]![Relative]=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(FilePath))).ToLowerInvariant();
        }
        File.WriteAllText(Path.Combine(Root,"Examples/workbench/Unlisted.txt"),"must not copy");
        string NextInventory=Next.ToJsonString(); File.WriteAllText(Metadata,NextInventory);
        string TemplateProject=Path.Combine(TempRoot,"TemplateProject");
        Require(Invoke("new",TemplateProject,"--template","workbench","--sdk",Root).Code==0,"verified template scaffold failed");
        foreach(var Item in Example) Require(File.ReadAllText(Path.Combine(TemplateProject,Item.Key))==Item.Value,"template/lock bytes changed");
        Require(!File.Exists(Path.Combine(TemplateProject,"Unlisted.txt")),"unlisted SDK file copied");
        JsonNode TemplatePin=JsonNode.Parse(File.ReadAllText(Path.Combine(TemplateProject,"LuiToolchain.json")))!;
        Require(TemplatePin["SdkVersion"]!.GetValue<string>()=="0.1.0-preview.2" && TemplatePin["SdkManifestSha256"]!.GetValue<string>()==Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Metadata))).ToLowerInvariant(),"template pin not exact");
        Require(Invoke("new",TemplateProject,"--template","workbench","--sdk",Root).Code==1,"existing template project overwritten");
        Require(Invoke("check",Path.Combine(TemplateProject,"lui.json"),"--sdk",Root).Code==0,"template failed type check");
        File.WriteAllText(Metadata, Inventory.Replace("\"SchemaVersion\":1", "\"SchemaVersion\":1,\"Extra\":true"));
        Require(Invoke("doctor", "--sdk", Root).Code == 1, "unknown metadata field accepted");
        File.WriteAllText(Metadata, Inventory.Replace("\"SchemaVersion\":1", "\"SchemaVersion\":1,\"SchemaVersion\":1"));
        Require(Invoke("doctor", "--sdk", Root).Code == 1, "duplicate metadata field accepted");
        File.WriteAllText(Metadata, Inventory);
        File.AppendAllText(Path.Combine(Root, Components["Definitions"]), "tampered");
        Require(Invoke("doctor", "--sdk", Root).Code == 1, "modified SDK component accepted");
    }
}
