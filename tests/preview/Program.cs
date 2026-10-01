using System.Diagnostics;
using System.Text.Json;

namespace Lui.PreviewTests;

internal static class Program
{
    private static int Main(string[] Arguments)
    {
        if (Arguments.Length != 2) throw new ArgumentException("Expected preview host DLL and native runtime");
        string DirectoryPath = Path.Combine(Path.GetTempPath(), "LuiPreviewTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(DirectoryPath);
        try
        {
            string Manifest = Path.Combine(DirectoryPath, "lui.json");
            File.WriteAllText(Manifest, "{\"SchemaVersion\":1,\"Script\":\"main.luau\",\"Capabilities\":[],\"Extensions\":[],\"Assets\":[]}");
            string ScriptPath = Path.Combine(DirectoryPath, "main.luau");
            string Script =
                "local W = Instance.new(\"Window\", {Title=\"Preview\", Size=UDim2.fromOffset(400,300)})\n" +
                "local B = Instance.new(\"TextButton\", {Text=\"Go\", Parent=W})\n" +
                "B.Activated:Connect(function() B.Text=\"Clicked\" end)\n" +
                "task.delay(1, function() print(\"old callback\") end)\n" +
                "W.Visible=true\n";
            File.WriteAllText(ScriptPath, Script);
            ProcessStartInfo Start = new("dotnet")
            {
                UseShellExecute = false,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            Start.ArgumentList.Add(Path.GetFullPath(Arguments[0]));
            Start.ArgumentList.Add("--manifest"); Start.ArgumentList.Add(Manifest);
            Start.ArgumentList.Add("--runtime"); Start.ArgumentList.Add(Path.GetFullPath(Arguments[1]));
            using Process Child = Process.Start(Start) ?? throw new Exception("Preview host did not start");
            List<string> Lines = [ReadLine(Child), ReadLine(Child)];
            Child.StandardInput.WriteLine("{\"version\":1,\"type\":\"activate\",\"generation\":1,\"id\":2}");
            Lines.Add(ReadLine(Child));
            File.WriteAllText(ScriptPath, Script.Replace(
                "task.delay(1, function() print(\"old callback\") end)\n", string.Empty, StringComparison.Ordinal));
            Child.StandardInput.WriteLine("{\"version\":1,\"type\":\"reload\",\"generation\":1}");
            Lines.Add(ReadLine(Child));
            Child.StandardInput.WriteLine("{\"version\":1,\"type\":\"activate\",\"generation\":1,\"id\":2}");
            Child.StandardInput.WriteLine("{\"version\":99,\"type\":\"snapshot\",\"generation\":2}");
            Child.StandardInput.WriteLine("{\"version\":1,\"type\":\"resizeViewport\",\"generation\":2,\"id\":1,\"width\":500,\"height\":200}");
            Thread.Sleep(1200);
            Child.StandardInput.WriteLine("{\"version\":1,\"type\":\"shutdown\"}");
            Child.StandardInput.Close();
            string Output = string.Join('\n', Lines) + "\n" + Child.StandardOutput.ReadToEnd();
            string Errors = Child.StandardError.ReadToEnd();
            if (!Child.WaitForExit(10_000)) { Child.Kill(); throw new Exception("Preview host hung"); }
            if (Child.ExitCode != 0) throw new Exception("Preview host failed: " + Errors);
            JsonDocument[] Messages = Output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
                .Select(Line => JsonDocument.Parse(Line)).ToArray();
            JsonElement[] Roots = Messages.Select(Message => Message.RootElement).ToArray();
            Require(Roots[0].GetProperty("type").GetString() == "hello", "missing hello");
            JsonElement[] Trees = Roots.Where(Root => Root.GetProperty("type").GetString() == "fullTree").ToArray();
            Require(Trees.Length >= 4, "missing tree updates");
            Require(Trees[0].GetProperty("generation").GetInt32() == 1, "wrong initial generation");
            JsonElement InitialNodes = Trees[0].GetProperty("nodes");
            Require(InitialNodes.GetArrayLength() == 2 && InitialNodes[1].GetProperty("parentId").GetInt32() == 1,
                "initial tree was not rooted");
            Require(InitialNodes[0].GetProperty("bounds").GetProperty("width").GetDouble() == 400,
                "preview did not use resolved runtime layout");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 1 &&
                Tree.GetProperty("nodes")[1].GetProperty("text").GetString() == "Clicked"), "activation did not update tree");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 2 &&
                Tree.GetProperty("nodes")[1].GetProperty("text").GetString() == "Go"), "reload did not reset runtime");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 2 &&
                Tree.GetProperty("nodes")[0].GetProperty("bounds").GetProperty("width").GetDouble() == 500),
                "viewport resize did not update layout");
            Require(Roots.Count(Root => Root.GetProperty("type").GetString() == "diagnostic") == 2,
                "stale generation or unsupported version was accepted");
            Require(!Output.Contains("old callback", StringComparison.Ordinal), "old scheduled callback survived reload");
            Console.WriteLine("[LUI:PreviewTests] Protocol, layout, activation, reload and generation checks passed");
            return 0;
        }
        finally { Directory.Delete(DirectoryPath, true); }
    }

    private static void Require(bool Condition, string Message)
    {
        if (!Condition) throw new Exception(Message);
    }

    private static string ReadLine(Process Child) => Child.StandardOutput.ReadLineAsync()
        .WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult() ?? throw new Exception("Preview host closed output early");
}
