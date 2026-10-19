using Lui.WinUI;

namespace Lui.Packaging;

internal static class PackageBuilder
{
    // Project content stays beneath Application to reserve the platform host
    // namespace. The native index governs every declared application input.
    public static void StageProject(string Manifest,string HostDirectory,string Output,string Tool)
    {
        Output=Path.GetFullPath(Output); HostDirectory=Path.GetFullPath(HostDirectory);
        if(Directory.Exists(Output) || File.Exists(Output)) throw new InvalidDataException("[LUI:Package] OutputExists");
        bool Linux=OperatingSystem.IsLinux();
        if((System.IO.File.GetAttributes(HostDirectory)&FileAttributes.ReparsePoint)!=0 ||
            !System.IO.File.Exists(Path.Combine(HostDirectory,Linux?"bin/lui-gtk":"Lui.WinUI.exe")) ||
            !System.IO.File.Exists(Path.Combine(HostDirectory,Linux?"lib/libLuiRuntime.so":"LuiRuntime.dll"))) throw new InvalidDataException("[LUI:Package] InvalidHost");
        var Files=GetHostFiles(HostDirectory)
            .Where(File=>!File.EndsWith(".pdb",StringComparison.OrdinalIgnoreCase)).OrderBy(File=>File,StringComparer.Ordinal).ToArray();
        HashSet<string> Names=new(StringComparer.OrdinalIgnoreCase);
        foreach(string File in Files) {
            string Relative=Path.GetRelativePath(HostDirectory,File);
            if(!Names.Add(Relative) || Relative.Equals("Start-LUI.cmd",StringComparison.OrdinalIgnoreCase) || Relative.Equals("Start-LUI.sh",StringComparison.OrdinalIgnoreCase) || Relative.StartsWith("Application",StringComparison.OrdinalIgnoreCase) ||
                (System.IO.File.GetAttributes(File)&FileAttributes.ReparsePoint)!=0) throw new InvalidDataException("[LUI:Package] HostCollision");
            string Current=Path.GetDirectoryName(File)!;
            while(Current!=HostDirectory) {if((System.IO.File.GetAttributes(Current)&FileAttributes.ReparsePoint)!=0) throw new InvalidDataException("[LUI:Package] LinkedPath"); Current=Path.GetDirectoryName(Current)!;}
        }
        Directory.CreateDirectory(Output);
        // A native staging child performs captured-content creation/validation.
        System.Diagnostics.ProcessStartInfo Start=new(Tool){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        foreach(string Arg in new[]{"--stage",Path.GetFullPath(Manifest),Path.Combine(Output,"Application")}) Start.ArgumentList.Add(Arg);
        using var Child=System.Diagnostics.Process.Start(Start)??throw new IOException("[LUI:Package] ToolFailed");
        static async Task ReadBounded(StreamReader Reader) {
            char[] Buffer=new char[1024]; int Total=0, Count;
            while((Count=await Reader.ReadAsync(Buffer))!=0) {Total+=Count; if(Total>4096) throw new InvalidDataException("[LUI:Package] ToolFailed");}
        }
        try {
            Task.WhenAll(ReadBounded(Child.StandardOutput),ReadBounded(Child.StandardError),Child.WaitForExitAsync())
                .WaitAsync(TimeSpan.FromSeconds(60)).GetAwaiter().GetResult();
            if(Child.ExitCode!=0) throw new InvalidDataException("[LUI:Package] PreparationFailed");
        } catch(TimeoutException) {throw new InvalidDataException("[LUI:Package] LimitExceeded");}
        finally {if(!Child.HasExited) Child.Kill(true); Child.WaitForExit();}
        foreach(string File in Files) {string Target=Path.Combine(Output,Path.GetRelativePath(HostDirectory,File)); Directory.CreateDirectory(Path.GetDirectoryName(Target)!); System.IO.File.Copy(File,Target,false);}
        if(Linux) {
            string Launcher=Path.Combine(Output,"Start-LUI.sh");
            System.IO.File.WriteAllText(Launcher,"#!/bin/sh\nset -eu\nRoot=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nexec \"$Root/bin/lui-gtk\" --project \"$Root/Application/lui.json\" \"$@\"\n");
            System.IO.File.SetUnixFileMode(Launcher,UnixFileMode.UserRead|UnixFileMode.UserWrite|UnixFileMode.UserExecute|UnixFileMode.GroupRead|UnixFileMode.GroupExecute|UnixFileMode.OtherRead|UnixFileMode.OtherExecute);
        } else System.IO.File.WriteAllText(Path.Combine(Output,"Start-LUI.cmd"),"@echo off\r\n\"%~dp0Lui.WinUI.exe\" --project \"%~dp0Application\\lui.json\"\r\n");
    }
    private static IEnumerable<string> GetHostFiles(string Root)
    {
        Stack<string> Pending=new(); Pending.Push(Root);
        while(Pending.Count!=0) {
            string DirectoryPath=Pending.Pop();
            foreach(string Entry in Directory.EnumerateFileSystemEntries(DirectoryPath)) {
                FileAttributes Attributes=System.IO.File.GetAttributes(Entry);
                // Inspect links before recursion, including links to directories.
                if((Attributes&FileAttributes.ReparsePoint)!=0) throw new InvalidDataException("[LUI:Package] LinkedPath");
                if((Attributes&FileAttributes.Directory)!=0) Pending.Push(Entry); else yield return Entry;
            }
        }
    }
    public static void Stage(string ManifestPathValue, string HostDirectoryValue,
        string OutputDirectoryValue, string? ExtensionDirectoryValue)
    {
        AppManifest Manifest = AppManifest.Load(ManifestPathValue);
        string ManifestPath = Path.GetFullPath(ManifestPathValue);
        string HostDirectory = Path.GetFullPath(HostDirectoryValue);
        string OutputDirectory = Path.GetFullPath(OutputDirectoryValue);
        string ExtensionDirectory = ExtensionDirectoryValue is null
            ? Path.GetDirectoryName(ManifestPath)!
            : Path.GetFullPath(ExtensionDirectoryValue);
        if (!File.Exists(Path.Combine(HostDirectory, "Lui.WinUI.exe")) ||
            !File.Exists(Path.Combine(HostDirectory, "LuiRuntime.dll")))
            throw new InvalidDataException("Published Windows host and native runtime are required");
        if (Directory.Exists(OutputDirectory) && Directory.EnumerateFileSystemEntries(OutputDirectory).Any())
            throw new InvalidDataException("Package output directory must be empty");

        List<(string Source, string Relative)> Files = new();
        foreach (string HostFile in Directory.EnumerateFiles(HostDirectory, "*", SearchOption.AllDirectories))
        {
            if (Path.GetExtension(HostFile).Equals(".pdb", StringComparison.OrdinalIgnoreCase)) continue;
            Files.Add((HostFile, Path.GetRelativePath(HostDirectory, HostFile)));
        }
        Files.Add((ManifestPath, Path.GetFileName(ManifestPath)));
        Files.Add((Manifest.ScriptPath, Path.GetRelativePath(Path.GetDirectoryName(ManifestPath)!, Manifest.ScriptPath)));
        foreach (KeyValuePair<string, string> Asset in Manifest.AssetPaths)
            Files.Add((Asset.Value, Asset.Key));
        foreach (string ExtensionPath in Manifest.ExtensionPaths)
        {
            string ExtensionFile = Path.Combine(ExtensionDirectory, Path.GetFileName(ExtensionPath));
            if (!File.Exists(ExtensionFile) ||
                (File.GetAttributes(ExtensionFile) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Declared extension is missing or linked: " + Path.GetFileName(ExtensionPath));
            Files.Add((ExtensionFile, Path.GetFileName(ExtensionPath)));
        }
        HashSet<string> Names = new(StringComparer.OrdinalIgnoreCase);
        foreach ((string Source, string Relative) in Files)
        {
            if (!Names.Add(Relative) || Path.IsPathRooted(Relative) || Relative == ".." ||
                Relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal))
                throw new InvalidDataException("Package file collision or path escape: " + Relative);
        }
        Directory.CreateDirectory(OutputDirectory);
        foreach ((string Source, string Relative) in Files)
        {
            string Target = Path.Combine(OutputDirectory, Relative);
            Directory.CreateDirectory(Path.GetDirectoryName(Target)!);
            File.Copy(Source, Target, overwrite: false);
        }
        Console.WriteLine($"[LUI:Package] Staged {Files.Count} files at {OutputDirectory}");
    }
}
