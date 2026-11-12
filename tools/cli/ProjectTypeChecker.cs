using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Lui.Cli;

internal static class ProjectTypeChecker
{
    internal static JsonElement Check(string Checker, string Definitions, string Manifest, string Diagnostics)
    {
        if (Diagnostics != "development") throw new ArgumentException("Project checking requires --diagnostics development");
        ProcessStartInfo Start = new(Checker)
        {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = new UTF8Encoding(false, true), StandardErrorEncoding = new UTF8Encoding(false, true),
        };
        foreach (string Argument in new[] { Definitions, "--project", Manifest, "--diagnostics", "development" }) Start.ArgumentList.Add(Argument);
        using Process Child = Process.Start(Start) ?? throw new IOException("Unable to start project checker");
        try
        {
            Task<string> Output = Read(Child.StandardOutput, 1024 * 1024);
            Task<string> Error = Read(Child.StandardError, 64 * 1024);
            Task.WhenAll(Output, Error, Child.WaitForExitAsync()).WaitAsync(TimeSpan.FromSeconds(60)).GetAwaiter().GetResult();
            if (Child.ExitCode is not (0 or 1))
            {
                string Message = Error.Result.Trim();
                // A failed/mismatched child must not disclose arbitrary stderr.
                Match Code = Regex.Match(Message, @"^\[LUI:(Project|TypeCheck)\] ([A-Za-z]{1,64})$", RegexOptions.CultureInvariant);
                string[] Known = ["InvalidInput", "InvalidOptions", "InvalidJson", "InvalidManifest", "UnknownCapability", "DuplicateCapability", "InvalidDeclaration", "InvalidPath", "InvalidLock", "UnsupportedFeature",
                    "UnsupportedSchema", "PolicyNotSupported", "DefinitionsInvalid", "AnalysisFailed", "LimitExceeded", "Canceled", "Cycle", "MissingModule",
                    "MissingFile", "WrongCase", "LinkNotAllowed", "DependencyMismatch", "ForbiddenImport", "EntryImportDenied", "FileChanged", "BinaryMismatch", "CapabilityMismatch", "UnsupportedPlatform"];
                string Safe = Code.Success && Known.Contains(Code.Groups[2].Value, StringComparer.Ordinal) ? Message : "[LUI:TypeCheck] CheckerFailed";
                throw new InvalidDataException(Safe);
            }
            if (Encoding.UTF8.GetByteCount(Output.Result) > 1024 * 1024) throw new InvalidDataException("Project checker result exceeds 1 MiB");
            using JsonDocument Document = JsonDocument.Parse(Output.Result, new JsonDocumentOptions { MaxDepth = 16 });
            Validate(Document.RootElement, Child.ExitCode);
            return Document.RootElement.Clone();
        }
        catch (TimeoutException) { throw new InvalidDataException("Project checker exceeded the 60 second process limit"); }
        finally { if (!Child.HasExited) Child.Kill(entireProcessTree: true); Child.WaitForExit(); }
    }

    private static async Task<string> Read(StreamReader Reader, int Maximum)
    {
        StringBuilder Value = new(); char[] Buffer = new char[4096]; int Count;
        while ((Count = await Reader.ReadAsync(Buffer)) != 0)
        {
            if (Value.Length + Count > Maximum) throw new InvalidDataException("Project checker output exceeds its bound");
            Value.Append(Buffer, 0, Count);
        }
        return Value.ToString();
    }

    internal static void Validate(JsonElement Result, int Status)
    {
        static void Require(bool Value) { if (!Value) throw new InvalidDataException("Malformed project checker protocol"); }
        static JsonElement Field(JsonElement Value, string Name, JsonValueKind Kind)
        {
            Require(Value.ValueKind == JsonValueKind.Object && Value.TryGetProperty(Name, out _));
            JsonElement Item = Value.GetProperty(Name); Require(Item.ValueKind == Kind); return Item;
        }
        static void Shape(JsonElement Value, params string[] Names)
        {
            Require(Value.ValueKind == JsonValueKind.Object); HashSet<string> Seen = new(StringComparer.Ordinal);
            foreach (JsonProperty Property in Value.EnumerateObject()) Require(Names.Contains(Property.Name) && Seen.Add(Property.Name));
            Require(Seen.Count == Names.Length);
        }
        static string Text(JsonElement Value, string Name, int Maximum = 640)
        {
            string Item = Field(Value, Name, JsonValueKind.String).GetString()!; Require(Item.Length is > 0 && Item.Length <= Maximum); return Item;
        }
        static bool Hash(string Value) => Regex.IsMatch(Value, "^[a-f0-9]{64}$", RegexOptions.CultureInvariant);
        static bool PathValue(string Value) => Value.Length <= 512 && Value.Split('/').All(Part =>
            Part.Length > 0 && Part is not ("." or "..") && !Part.EndsWith('.') &&
            Part.All(Character => char.IsAsciiLetterOrDigit(Character) || Character is '_' or '-' or '.'));
        static bool Identity(string Value)
        {
            if (Value.StartsWith("app:", StringComparison.Ordinal)) return PathValue(Value[4..]);
            if (!Value.StartsWith("dep:", StringComparison.Ordinal)) return false;
            int Slash = Value.IndexOf('/'); if (Slash < 5) return false;
            return Regex.IsMatch(Value[4..Slash], "^[A-Za-z_][A-Za-z0-9_]{0,63}$", RegexOptions.CultureInvariant) && PathValue(Value[(Slash + 1)..]);
        }
        Shape(Result, "version", "generation", "profile", "digest", "sourceDigest", "definitionsDigest", "entry", "diagnosticsMode", "redacted", "truncated", "sources", "diagnostics");
        Require(Field(Result, "version", JsonValueKind.Number).TryGetInt32(out int Version) && Version == 2);
        Require(Field(Result, "generation", JsonValueKind.Number).TryGetInt64(out long Generation) && Generation is >= 0 and <= 9007199254740991L);
        Require(Text(Result, "profile") == "ProjectTypesV1" && Text(Result, "diagnosticsMode") == "Development" &&
            Field(Result, "redacted", JsonValueKind.False).ValueKind == JsonValueKind.False);
        foreach (string Name in new[] { "digest", "sourceDigest", "definitionsDigest" }) Require(Hash(Text(Result, Name)));
        Require(Result.TryGetProperty("truncated", out JsonElement Truncated) && Truncated.ValueKind is JsonValueKind.True or JsonValueKind.False);
        JsonElement Sources = Field(Result, "sources", JsonValueKind.Array); Require(Sources.GetArrayLength() is > 0 and <= 256);
        Dictionary<string, (int Bytes, int Lines)> Map = new(StringComparer.Ordinal);
        HashSet<string> Paths = new(StringComparer.OrdinalIgnoreCase); long TotalBytes = 0;
        string? Previous = null;
        foreach (JsonElement Source in Sources.EnumerateArray())
        {
            Shape(Source, "id", "path", "hash", "bytes", "lines"); string Id = Text(Source, "id"), Path = Text(Source, "path", 512);
            Require(Identity(Id) && PathValue(Path) && Id.EndsWith(".luau", StringComparison.Ordinal) && Path.EndsWith(".luau", StringComparison.Ordinal) &&
                Paths.Add(Path) && Hash(Text(Source, "hash")) && (Previous is null || string.CompareOrdinal(Previous, Id) < 0));
            Require(Field(Source, "bytes", JsonValueKind.Number).TryGetInt32(out int Bytes) && Bytes is >= 0 and <= 8 * 1024 * 1024);
            Require(Field(Source, "lines", JsonValueKind.Number).TryGetInt32(out int Lines) && Lines > 0 && Lines <= Bytes + 1);
            Require(Map.TryAdd(Id, (Bytes, Lines))); TotalBytes += Bytes; Previous = Id;
        }
        Require(TotalBytes <= 32 * 1024 * 1024 && Map.ContainsKey(Text(Result, "entry")));
        JsonElement Diagnostics = Field(Result, "diagnostics", JsonValueKind.Array); Require(Diagnostics.GetArrayLength() <= 256);
        Require(Status is 0 or 1 && (Status == 0) == (Diagnostics.GetArrayLength() == 0 && !Truncated.GetBoolean()));
        foreach (JsonElement Issue in Diagnostics.EnumerateArray())
        {
            Shape(Issue, "source", "kind", "code", "message", "range"); string Kind = Text(Issue, "kind"), Code = Text(Issue, "code", 64);
            Require(Kind is "syntax" or "type" or "import" or "module" or "project");
            Require(Regex.IsMatch(Code, "^[A-Za-z]{1,64}$", RegexOptions.CultureInvariant) && Encoding.UTF8.GetByteCount(Text(Issue, "message", 2048)) <= 2048);
            JsonElement Id = Issue.GetProperty("source"), Range = Issue.GetProperty("range");
            if (Kind == "project") { Require(Id.ValueKind == JsonValueKind.Null && Range.ValueKind == JsonValueKind.Null); continue; }
            Require(Id.ValueKind == JsonValueKind.String && Map.ContainsKey(Id.GetString()!)); var Bounds = Map[Id.GetString()!];
            Shape(Range, "start", "end");
            (int Line, int Column) Position(JsonElement Value)
            {
                Shape(Value, "line", "column"); Require(Field(Value, "line", JsonValueKind.Number).TryGetInt32(out int Line) && Line >= 0 && Line < Bounds.Lines);
                Require(Field(Value, "column", JsonValueKind.Number).TryGetInt32(out int Column) && Column >= 0 && Column <= Bounds.Bytes); return (Line, Column);
            }
            var Start = Position(Range.GetProperty("start")); var End = Position(Range.GetProperty("end"));
            Require(End.Line > Start.Line || (End.Line == Start.Line && End.Column >= Start.Column));
        }
    }
}
