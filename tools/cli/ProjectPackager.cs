using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Lui.Cli;

internal static class ProjectPackager
{
    internal static JsonElement Run(string Tool, params string[] Arguments)
    {
        ProcessStartInfo Start = new(Tool)
        {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = new UTF8Encoding(false, true), StandardErrorEncoding = new UTF8Encoding(false, true),
        };
        foreach (string Argument in Arguments) Start.ArgumentList.Add(Argument);
        using Process Child = Process.Start(Start) ?? throw new IOException("[LUI:Project] ToolFailed");
        try
        {
            Task<string> Output = Read(Child.StandardOutput), Error = Read(Child.StandardError);
            Task.WhenAll(Output, Error, Child.WaitForExitAsync()).WaitAsync(TimeSpan.FromSeconds(60)).GetAwaiter().GetResult();
            if (Child.ExitCode != 0)
            {
                string Message = Error.Result.Trim();
                string[] Codes = ["InvalidInput", "InvalidOptions", "InvalidJson", "InvalidManifest", "UnknownCapability", "DuplicateCapability", "InvalidDeclaration", "InvalidSchema", "InvalidPath", "InvalidLock",
                    "UnsupportedFeature", "LimitExceeded", "Canceled", "Cycle", "MissingModule", "MissingFile", "CaseMismatch", "LinkedPath",
                    "NotRegularFile", "FileChanged", "IoError", "DependencyMismatch", "DuplicateIdentity", "UnknownAlias", "ForbiddenImport",
                    "EntryImportDenied", "InvalidSource", "CompileFailed", "OutputExists", "PackageMismatch", "ProviderUnavailable", "BinaryMismatch", "CapabilityMismatch", "UnsupportedPlatform"];
                Match Code = Regex.Match(Message, @"^\[LUI:Project\] ([A-Za-z]{1,64})$", RegexOptions.CultureInvariant);
                throw new InvalidDataException(Code.Success && Codes.Contains(Code.Groups[1].Value, StringComparer.Ordinal)
                    ? Message : "[LUI:Project] ToolFailed");
            }
            if (Error.Result.Length != 0 || Encoding.UTF8.GetByteCount(Output.Result) > 4096) throw new InvalidDataException("[LUI:Project] ToolFailed");
            using JsonDocument Document = JsonDocument.Parse(Output.Result, new JsonDocumentOptions { MaxDepth = 4 });
            Validate(Document.RootElement); return Document.RootElement.Clone();
        }
        catch (TimeoutException) { throw new InvalidDataException("[LUI:Project] LimitExceeded"); }
        catch (JsonException) { throw new InvalidDataException("[LUI:Project] ToolFailed"); }
        catch (DecoderFallbackException) { throw new InvalidDataException("[LUI:Project] ToolFailed"); }
        finally { if (!Child.HasExited) Child.Kill(entireProcessTree: true); Child.WaitForExit(); }
    }
    private static async Task<string> Read(StreamReader Reader)
    {
        StringBuilder Value = new(); char[] Buffer = new char[1024]; int Count;
        while ((Count = await Reader.ReadAsync(Buffer)) != 0)
        {
            if (Value.Length + Count > 4096) throw new InvalidDataException("[LUI:Project] ToolFailed");
            Value.Append(Buffer, 0, Count);
        }
        return Value.ToString();
    }
    internal static void Validate(JsonElement Result)
    {
        static void Require(bool Value) { if (!Value) throw new InvalidDataException("[LUI:Project] ToolFailed"); }
        string[] Fields = ["SchemaVersion", "Profile", "PackageDigest", "SourceDigest", "CompiledDigest", "SourceCount", "AssetCount"];
        Require(Result.ValueKind == JsonValueKind.Object);
        HashSet<string> Seen = new(StringComparer.Ordinal);
        foreach (JsonProperty Item in Result.EnumerateObject()) Require(Fields.Contains(Item.Name) && Seen.Add(Item.Name));
        Require(Seen.Count == Fields.Length);
        Require(Result.GetProperty("SchemaVersion").ValueKind == JsonValueKind.Number);
        Require(Result.GetProperty("SchemaVersion").TryGetInt32(out int Version) && Version is 1 or 2);
        Require(Result.GetProperty("Profile").ValueKind == JsonValueKind.String &&
            Result.GetProperty("Profile").GetString() == (Version == 1 ? "DeclaredContentV1" : "DeclaredContentV2"));
        foreach (string Name in new[] { "PackageDigest", "SourceDigest", "CompiledDigest" })
        {
            JsonElement Hash = Result.GetProperty(Name); Require(Hash.ValueKind == JsonValueKind.String &&
                Regex.IsMatch(Hash.GetString()!, "^[a-f0-9]{64}$", RegexOptions.CultureInvariant));
        }
        Require(Result.GetProperty("SourceCount").ValueKind == JsonValueKind.Number && Result.GetProperty("AssetCount").ValueKind == JsonValueKind.Number);
        Require(Result.GetProperty("SourceCount").TryGetInt32(out int Sources) && Sources is > 0 and <= 256);
        Require(Result.GetProperty("AssetCount").TryGetInt32(out int Assets) && Assets is >= 0 and <= 256);
    }
}
