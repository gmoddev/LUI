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
            Require(Roots[0].GetProperty("schema").GetProperty("classes").GetArrayLength() > 0,
                "hello omitted runtime reflection schema");
            JsonElement[] Trees = Roots.Where(Root => Root.GetProperty("type").GetString() == "fullTree").ToArray();
            Require(Trees.Length >= 4, "missing tree updates");
            Require(Trees[0].GetProperty("generation").GetInt32() == 1, "wrong initial generation");
            JsonElement InitialNodes = Trees[0].GetProperty("nodes");
            Require(InitialNodes.GetArrayLength() == 2 && InitialNodes[1].GetProperty("parentId").GetInt32() == 1,
                "initial tree was not rooted");
            Require(InitialNodes[0].GetProperty("createdAt").GetProperty("line").GetInt32() == 1 &&
                InitialNodes[1].GetProperty("createdAt").GetProperty("line").GetInt32() == 2,
                "preview omitted Luau creation lines");
            Require(InitialNodes[0].GetProperty("bounds").GetProperty("width").GetDouble() == 400,
                "preview did not use resolved runtime layout");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 1 &&
                Tree.GetProperty("nodes")[1].GetProperty("text").GetString() == "Clicked"), "activation did not update tree");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 1 &&
                Tree.GetProperty("nodes")[1].GetProperty("lastChangedAt").GetProperty("property").GetString() == "Text"),
                "preview omitted Luau mutation property");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 2 &&
                Tree.GetProperty("nodes")[1].GetProperty("text").GetString() == "Go"), "reload did not reset runtime");
            Require(Trees.Any(Tree => Tree.GetProperty("generation").GetInt32() == 2 &&
                Tree.GetProperty("nodes")[0].GetProperty("bounds").GetProperty("width").GetDouble() == 500),
                "viewport resize did not update layout");
            Require(Roots.Count(Root => Root.GetProperty("type").GetString() == "diagnostic") == 2,
                "stale generation or unsupported version was accepted");
            Require(!Output.Contains("old callback", StringComparison.Ordinal), "old scheduled callback survived reload");
            CheckInput(ScriptPath, Start);
            Console.WriteLine("[LUI:PreviewTests] Protocol, layout, activation, reload and generation checks passed");
            TestModular(Arguments[0],Arguments[1],DirectoryPath);
            return 0;
        }
        finally { Directory.Delete(DirectoryPath, true); }
    }

    private static void Require(bool Condition, string Message)
    {
        if (!Condition) throw new Exception(Message);
    }

    private static void CheckInput(string ScriptPath, ProcessStartInfo Start)
    {
        File.WriteAllText(ScriptPath, """
            local W = Instance.new("Window", {Visible=true})
            local B = Instance.new("TextButton", {Parent=W})
            for Index=1,32 do Instance.new("TextButton", {Parent=W}) end
            for _, Name in {"InputBegan", "InputChanged", "InputEnded"} do
                B[Name]:Connect(function(E)
                    print("Input:" .. Name .. ":" .. E.Device .. ":" ..
                        (E.Device == "Keyboard" and E.Key .. ":" .. tostring(E.IsRepeat) or
                        tostring(E.PointerId) .. ":" .. tostring(E.IsCanceled) .. ":" .. tostring(E.Position.X)))
                end)
            end
            B.FocusLost:Connect(function() print("FocusLost") end)
            """);
        using Process Child = Process.Start(Start) ?? throw new Exception("Input preview did not start");
        string Hello = ReadLine(Child);
        Require(Hello.Contains("pointerInput"), "input capabilities absent");
        ReadLine(Child);
        void Send(object Command) => Child.StandardInput.WriteLine(JsonSerializer.Serialize(Command));
        Send(new { version=1, type="keyInput", generation=1, id=2, phase="Began", key="A" }); // Unfocused.
        Send(new { version=1, type="focus", generation=1, id=2, focused=true });
        Send(new { version=1, type="keyInput", generation=1, id=2, phase="Began", key="A" });
        Send(new { version=1, type="keyInput", generation=1, id=2, phase="Changed", key="A" });
        Send(new { version=1, type="pointerInput", generation=1, id=2, phase="Began", device=0, pointerId=1, x=12, y=-2 });
        Send(new { version=1, type="pointerInput", generation=1, id=2, phase="Changed", device=0, pointerId=1, x=13, y=-2 });
        Send(new { version=1, type="resetInput", generation=1 });
        Send(new { version=1, type="keyInput", generation=1, id=2, phase="Ended", key="A" }); // Already ended by reset.
        Send(new { version=1, type="pointerInput", generation=1, id=2, phase="Began", device=0, pointerId=32, x=0, y=0 });
        Send(new { version=1, type="pointerInput", generation=1, id=2, phase="Began", device=0, pointerId=1, x="bad", y=0 });
        Send(new { version=1, type="focus", generation=1, id=2, focused=1 });
        Send(new { version=1, type="pointerInput", generation=1, id=1, phase="Began", device=0, pointerId=1, x=0, y=0 });
        Send(new { version=1, type="focus", generation=1, id=2, focused=true });
        Send(new { version=1, type="keyInput", generation=1, id=2, phase="Began", key="Enter\0bad" });
        Send(new { version=1, type="pointerInput", generation=1, id=2, phase="Began", device=0, pointerId=1, x=0, y=0 });
        Send(new { version=1, type="keyInput", generation=1, id=2, phase="Began", key="A" });
        Send(new { version=1, type="reload", generation=1 });
        Send(new { version=1, type="pointerInput", generation=1, id=2, phase="Ended", device=0, pointerId=1, x=0, y=0 });
        Send(new { version=1, type="pointerInput", generation=2, id=2, phase="Ended", device=0, pointerId=1, x=0, y=0 });
        Send(new { version=1, type="keyInput", generation=2, id=2, phase="Ended", key="A" });
        Send(new { version=1, type="focus", generation=2, id=2, focused=true });
        Send(new { version=1, type="keyInput", generation=2, id=2, phase="Began", key="Enter" });
        Send(new { version=1, type="keyInput", generation=2, id=2, phase="Ended", key="Enter" });
        for (int Id = 3; Id <= 34; ++Id)
            Send(new { version=1, type="pointerInput", generation=2, id=Id, phase="Began", device=0, pointerId=0, x=0, y=0 });
        Send(new { version=1, type="pointerInput", generation=2, id=2, phase="Began", device=0, pointerId=1, x=0, y=0 });
        Send(new { version=1, type="resetInput", generation=2 });
        Send(new { version=1, type="pointerInput", generation=2, id=2, phase="Began", device=0, pointerId=1, x=20, y=0 });
        Send(new { version=1, type="pointerInput", generation=2, id=2, phase="Ended", device=0, pointerId=1, x=21, y=0 });
        Send(new { version=1, type="shutdown" });
        Child.StandardInput.Close();
        string Output = Child.StandardOutput.ReadToEndAsync().WaitAsync(TimeSpan.FromSeconds(10)).GetAwaiter().GetResult();
        if (!Child.WaitForExit(5000)) { Child.Kill(); throw new Exception("Input preview hung"); }
        Require(Child.ExitCode == 0, "input preview crashed");
        using JsonDocument Messages = JsonDocument.Parse("[" + string.Join(',', Output.Split('\n', StringSplitOptions.RemoveEmptyEntries)) + "]");
        var All = Messages.RootElement.EnumerateArray().ToArray();
        Require(All.Count(M => M.GetProperty("type").GetString() == "diagnostic") == 11, "invalid input, quota, or stale generation accepted");
        var Prints = All.Where(M => M.GetProperty("type").GetString() == "consoleMessage")
            .Select(M => M.GetProperty("message").GetString()!).ToArray();
        Require(Prints.Count(P => P.Contains("Input:InputBegan:Keyboard:A:false")) == 2, "key press pairing changed");
        Require(Prints.Count(P => P.Contains("Input:InputEnded:Keyboard:A:false")) == 1, "reset/reload key cleanup changed");
        Require(Prints.Any(P => P.Contains("Input:InputChanged:Keyboard:A:true")), "key repeat missing");
        Require(Prints.Any(P => P.Contains("Input:InputChanged:Mouse:1:false:13")), "local pointer position changed");
        Require(Prints.Count(P => P.Contains("Input:InputEnded:Mouse:1:true:13")) == 1, "pointer cancellation missing or duplicated");
        Require(Prints.Count(P => P.Contains("Input:InputEnded:Keyboard:Enter:false")) == 1, "new generation input failed");
        Require(Prints.Any(P => P.Contains("Input:InputEnded:Mouse:1:false:21")), "quota reset did not restore input");
        Console.WriteLine("[LUI:PreviewTests] Input validation, focus/repeat, cancellation, and stale generation checks passed");
    }

    private static void TestModular(string Host,string Runtime,string Root)
    {
        string Manifest=Path.Combine(Root,"Modular.json"), Source=Path.Combine(Root,"Src"); Directory.CreateDirectory(Source);
        File.WriteAllText(Manifest,"{\"SchemaVersion\":2,\"Script\":\"Src/Main.luau\",\"Capabilities\":[],\"Extensions\":[],\"Assets\":[],\"ExtensionDeclarations\":[],\"Diagnostics\":{\"Mode\":\"Development\"},\"Modules\":{\"Root\":\"Src\",\"Files\":[\"Main.luau\",\"Component.luau\"],\"Requires\":[],\"Dependencies\":[]}}");
        File.WriteAllText(Path.Combine(Source,"Main.luau"),"local F=require('./Component'); F(); task.delay(1,function() print('OLD_GENERATION_CANARY') end)");
        File.WriteAllText(Path.Combine(Source,"Component.luau"),"return function() local W=Instance.new('Window'); Instance.new('TextBox',{Parent=W,Sensitive=true,Name='PRIVATE_NODE_CANARY',Text='PRIVATE_PAYLOAD_CANARY'}); W.Visible=true end");
        ProcessStartInfo Start=new("dotnet"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true};
        foreach(string Arg in new[]{Host,"--project",Manifest,"--runtime",Runtime}) Start.ArgumentList.Add(Arg);
        using Process Child=Process.Start(Start)??throw new Exception("modular preview failed");
        List<string> Lines=[ReadLine(Child),ReadLine(Child),ReadLine(Child)];
        Require(Lines[0].Contains("\"version\":2") && Lines[1].Contains("\"type\":\"project\""),"modular protocol missing");
        Require(!string.Join(string.Empty,Lines).Contains("PRIVATE_"),"private preview content leaked");
        File.WriteAllText(Path.Combine(Source,"Main.luau"),"local F=require('./Component'); F()");
        Child.StandardInput.WriteLine("{\"version\":2,\"type\":\"reload\",\"generation\":1}");
        Lines.Add(ReadLine(Child)); Lines.Add(ReadLine(Child));
        Child.StandardInput.WriteLine("{\"version\":2,\"type\":\"snapshot\",\"generation\":1}");
        Lines.Add(ReadLine(Child)); Require(Lines[^1].Contains("InvalidCommand"),"stale generation accepted");
        Child.StandardInput.WriteLine("{\"version\":2,\"type\":\"shutdown\"}"); Child.StandardInput.Close();
        string Rest=Child.StandardOutput.ReadToEndAsync().WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
        Require(Child.WaitForExit(5000) && Child.ExitCode==0 && !Rest.Contains("OLD_GENERATION_CANARY"),"retired callback delivered");
        File.WriteAllText(Manifest,File.ReadAllText(Manifest).Replace("Development","Restricted"));
        File.WriteAllText(Path.Combine(Source,"Main.luau"),"local F=require('./Component'); F(); print('RESTRICTED_PRINT_CANARY')");
        using Process Restricted=Process.Start(Start)??throw new Exception("Restricted preview failed");
        string Hello=ReadLine(Restricted), Prepared=ReadLine(Restricted), Printed=ReadLine(Restricted), Protected=ReadLine(Restricted);
        Require(Hello.Contains("Restricted") && Prepared.Contains("\"sources\":[]") && Protected.Contains("\"type\":\"fullTree\""),"Restricted projection protocol failed");
        Require(!(Hello+Prepared+Printed+Protected).Contains("CANARY"),"Restricted IPC canary leaked");
        Restricted.StandardInput.WriteLine("{\"version\":2,\"type\":\"shutdown\"}"); Restricted.StandardInput.Close();
        Require(Restricted.WaitForExit(5000) && Restricted.ExitCode==0,"Restricted preview shutdown failed");
        Console.WriteLine("[LUI:PreviewTests] Modular identity, redaction and generation retirement passed");
    }
    private static string ReadLine(Process Child) => Child.StandardOutput.ReadLineAsync()
        .WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult() ?? throw new Exception("Preview host closed output early");
}
