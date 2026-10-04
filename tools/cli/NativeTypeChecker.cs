using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace Lui.Cli;

internal static class NativeTypeChecker
{
    private const int MaxOutputCharacters = 4 * 1024 * 1024;

    internal static JsonElement Check(string Checker, string Definitions, string Source)
    {
        ProcessStartInfo Start = new(Checker)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardInputEncoding = new UTF8Encoding(false),
            StandardOutputEncoding = new UTF8Encoding(false, true),
            StandardErrorEncoding = new UTF8Encoding(false, true),
        };
        Start.ArgumentList.Add(Definitions);
        using Process Child = Process.Start(Start) ?? throw new IOException("Unable to start Luau type checker");
        try
        {
            Task<string> Output = ReadBounded(Child.StandardOutput, MaxOutputCharacters);
            Task<string> Errors = ReadBounded(Child.StandardError, 64 * 1024);
            Task Write = WriteSource(Child, Source);
            Task Work = Task.WhenAll(Output, Errors, Write, Child.WaitForExitAsync());
            // Bound the whole transaction, including stdin, parsing, and definition loading.
            Work.WaitAsync(TimeSpan.FromSeconds(30)).GetAwaiter().GetResult();
            if (Child.ExitCode is not (0 or 1))
                throw new InvalidDataException("Type checker failed: " + Errors.Result.Trim());
            using JsonDocument Document = JsonDocument.Parse(Output.Result);
            JsonElement Result = Document.RootElement;
            Validate(Result, Source, Child.ExitCode);
            return Result.Clone();
        }
        catch (TimeoutException)
        {
            throw new InvalidDataException("Luau type checker exceeded the 30 second process limit");
        }
        finally
        {
            if (!Child.HasExited) Child.Kill(entireProcessTree: true);
            Child.WaitForExit();
        }
    }

    private static async Task WriteSource(Process Child, string Source)
    {
        await Child.StandardInput.WriteAsync(Source);
        Child.StandardInput.Close();
    }

    private static async Task<string> ReadBounded(StreamReader Reader, int Maximum)
    {
        StringBuilder Text = new();
        char[] Block = new char[4096];
        int Count;
        while ((Count = await Reader.ReadAsync(Block)) != 0)
        {
            if (Text.Length + Count > Maximum) throw new InvalidDataException("Type checker output exceeded its limit");
            Text.Append(Block, 0, Count);
        }
        return Text.ToString();
    }

    private static void Validate(JsonElement Result, string Source, int Status)
    {
        static JsonElement Field(JsonElement Object, string Name, JsonValueKind Kind)
        {
            if (Object.ValueKind != JsonValueKind.Object || !Object.TryGetProperty(Name, out JsonElement Value) ||
                Value.ValueKind != Kind) throw new InvalidDataException("Malformed type checker result: " + Name);
            return Value;
        }
        if (!Field(Result, "version", JsonValueKind.Number).TryGetInt32(out int Version) || Version != 1)
            throw new InvalidDataException("Unsupported type checker protocol");
        if (!Result.TryGetProperty("truncated", out JsonElement Truncated) ||
            Truncated.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            throw new InvalidDataException("Malformed type checker truncation flag");
        JsonElement Diagnostics = Field(Result, "diagnostics", JsonValueKind.Array);
        if (Diagnostics.GetArrayLength() > 256 || (Status == 0) != (Diagnostics.GetArrayLength() == 0) ||
            (Status == 0 && Truncated.GetBoolean()))
            throw new InvalidDataException("Type checker status disagrees with diagnostics");
        string[] Lines = Source.Split('\n');
        (int Line, int Column) Position(JsonElement Value)
        {
            if (!Field(Value, "line", JsonValueKind.Number).TryGetInt32(out int Line) ||
                !Field(Value, "column", JsonValueKind.Number).TryGetInt32(out int Column) ||
                Line < 0 || Line >= Lines.Length || Column < 0 || Column > Lines[Line].Length)
                throw new InvalidDataException("Type checker location left the entry script");
            return (Line, Column);
        }
        foreach (JsonElement Diagnostic in Diagnostics.EnumerateArray())
        {
            string Kind = Field(Diagnostic, "kind", JsonValueKind.String).GetString()!;
            string Message = Field(Diagnostic, "message", JsonValueKind.String).GetString()!;
            if (Kind is not ("type" or "syntax") || Message.Length is < 1 or > 2048)
                throw new InvalidDataException("Malformed type checker diagnostic");
            JsonElement Range = Field(Diagnostic, "range", JsonValueKind.Object);
            var Start = Position(Field(Range, "start", JsonValueKind.Object));
            var End = Position(Field(Range, "end", JsonValueKind.Object));
            if (End.Line < Start.Line || (End.Line == Start.Line && End.Column < Start.Column))
                throw new InvalidDataException("Type checker range is reversed");
        }
    }
}
