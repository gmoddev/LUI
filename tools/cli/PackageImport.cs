using System.ComponentModel;
using System.Diagnostics;
using System.Net;
using System.Text;
using static Lui.Cli.PackageSource;

namespace Lui.Cli;

internal static class PackageImport
{
    private const int ArchiveBytes = MaxPackageBytes + 65536;
    private static readonly UTF8Encoding Utf8 = new(false, true);

    internal static string Fetch(string Address, string Expected, string Catalog)
    {
        if (Address.Length > 2048 || !Uri.TryCreate(Address, UriKind.Absolute, out var Url) ||
            Url.Scheme != "https" || Url.UserInfo.Length != 0 || Url.Fragment.Length != 0)
            throw Invalid("InvalidPackageUrl (select an HTTPS archive URL without credentials or fragment)");
        RequireSha256(Expected);
        using var Handler = new SocketsHttpHandler {
            AllowAutoRedirect = false, UseCookies = false, UseProxy = false,
            AutomaticDecompression = DecompressionMethods.None, MaxResponseHeadersLength = 16,
            ConnectTimeout = TimeSpan.FromSeconds(8)
        };
        using var Client = new HttpClient(Handler);
        using var Deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        byte[] Bytes;
        try {
            using var Request = new HttpRequestMessage(HttpMethod.Get, Url);
            using var Reply = Client.Send(Request, HttpCompletionOption.ResponseHeadersRead, Deadline.Token);
            if (Reply.StatusCode != HttpStatusCode.OK || Reply.Content.Headers.ContentEncoding.Count != 0)
                throw Invalid("PackageDownloadRejected (expected an HTTPS 200 response without redirects)");
            if (Reply.Content.Headers.ContentLength > ArchiveBytes) throw Invalid("PackageLimit");
            using Stream Body = Reply.Content.ReadAsStream(Deadline.Token);
            Bytes = ReadBounded(Body, ArchiveBytes, Deadline.Token).GetAwaiter().GetResult();
        } catch (OperationCanceledException) {
            throw Invalid("PackageDownloadTimedOut");
        } catch (HttpRequestException) {
            throw Invalid("PackageDownloadUnavailable");
        } catch (IOException) {
            throw Invalid("PackageDownloadUnavailable");
        }
        if (Hash(Bytes) != Expected) throw Invalid("PackageArchiveDigestMismatch");
        EnsureDirectory(Catalog);
        string Temporary = Path.Combine(Catalog, "." + Guid.NewGuid().ToString("N") + ".pending");
        try {
            WriteNew(Temporary, Bytes);
            return Install(Catalog, Read(Temporary));
        } finally { File.Delete(Temporary); }
    }

    internal static string FromGit(string Repository, string Revision, string? Subdirectory, string Git, string Catalog)
    {
        if (Revision.Length is not (40 or 64) || !Hex(Revision)) throw Invalid("InvalidPackageGitRevision (use the full commit ID)");
        if (Subdirectory is not null) PortablePath(Subdirectory);
        if (!Path.IsPathFullyQualified(Git) || !File.Exists(Git)) throw Invalid("PackageGitUnavailable (select an absolute Git executable)");
        string Root = Subdirectory is null ? "" : Subdirectory + "/";
        VerifyLocalPath(Repository);
        using var Deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        byte[] Execute(int Bound, params string[] Arguments) =>
            ReadGit(Repository, Git, Bound, Deadline.Token, Arguments).GetAwaiter().GetResult();
        if (Utf8.GetString(Execute(32, "cat-file", "-t", Revision)).TrimEnd('\n', '\r') != "commit")
            throw Invalid("InvalidPackageGitRevision (select a commit, not a tag or tree)");
        string Tree = Utf8.GetString(Execute(256 * 1024, "ls-tree", "--full-tree", "-rz", Revision, "--", Subdirectory ?? "."));
        var Files = new Dictionary<string, (string Mode, string Object)>(StringComparer.Ordinal);
        foreach (string Row in Tree.Split('\0', StringSplitOptions.RemoveEmptyEntries)) {
            int Split = Row.IndexOf('\t');
            if (Split < 0) throw Invalid("InvalidPackageGitTree");
            string[] Header = Row[..Split].Split(' ');
            string Name = Row[(Split + 1)..];
            if (Header.Length != 3 || Header[2].Length != Revision.Length || !Hex(Header[2]) ||
                !Name.StartsWith(Root, StringComparison.Ordinal) ||
                !Files.TryAdd(Name[Root.Length..], (Header[0], Header[2])))
                throw Invalid("InvalidPackageGitTree");
        }
        byte[] ReadBlob(string Name, int Bound)
        {
            if (!Files.TryGetValue(Name, out var File)) throw Invalid("MissingPackageFile " + Name);
            if (File.Mode is not ("100644" or "100755")) throw Invalid("PackageLinkOrSpecialEntry");
            return Execute(Bound, "cat-file", "blob", File.Object);
        }
        return Install(Catalog, ReadSnapshot(ReadBlob));
    }

    private static string Install(string Catalog, PackageSource Package)
    {
        string Folder = PathFor(Catalog, Package.Name);
        EnsureDirectory(Folder);
        string Version = Package.Version.ToString(3);
        string DirectoryPath = PathFor(Folder, Version);
        string Target = PathFor(Folder, Version + ".luipkg");
        if (Directory.Exists(DirectoryPath)) {
            if (File.Exists(Target) || Read(DirectoryPath).Digest != Package.Digest) throw Invalid("PackageVersionAlreadyExists " + Package.Name);
            return DirectoryPath;
        }
        if (!File.Exists(Target)) {
            // Pending files live outside the alias's indexed version directory.
            string Pending = Path.Combine(Catalog, "." + Guid.NewGuid().ToString("N") + ".luipkg");
            try {
                Package.WriteArchive(Pending);
                try { File.Move(Pending, Target, overwrite: false); }
                catch (IOException) when (File.Exists(Target)) { }
            } finally { File.Delete(Pending); }
        }
        if (Read(Target).Digest != Package.Digest) throw Invalid("PackageVersionAlreadyExists " + Package.Name);
        return Target;
    }

    private static async Task<byte[]> ReadGit(string Repository, string Git, int Bound, CancellationToken Cancellation, string[] Arguments)
    {
        var Start = new ProcessStartInfo(Git) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string Key in Start.Environment.Keys.Where(Key => Key.StartsWith("GIT_", StringComparison.OrdinalIgnoreCase)).ToArray())
            Start.Environment.Remove(Key);
        Start.Environment["GIT_CONFIG_NOSYSTEM"] = "1";
        Start.Environment["GIT_CONFIG_GLOBAL"] = OperatingSystem.IsWindows() ? "NUL" : "/dev/null";
        Start.Environment["GIT_TERMINAL_PROMPT"] = "0";
        Start.Environment["GIT_NO_LAZY_FETCH"] = "1";
        Start.Environment["GIT_OPTIONAL_LOCKS"] = "0";
        Start.ArgumentList.Add("--no-replace-objects");
        Start.ArgumentList.Add("--literal-pathspecs");
        Start.ArgumentList.Add("-C");
        Start.ArgumentList.Add(Repository);
        foreach (string Argument in Arguments) Start.ArgumentList.Add(Argument);
        try {
            using var Process = System.Diagnostics.Process.Start(Start) ?? throw Invalid("PackageGitUnavailable");
            try {
                Task Drain = Process.StandardError.BaseStream.CopyToAsync(Stream.Null, Cancellation);
                byte[] Bytes = await ReadBounded(Process.StandardOutput.BaseStream, Bound, Cancellation);
                await Drain;
                await Process.WaitForExitAsync(Cancellation);
                if (Process.ExitCode != 0) throw Invalid("PackageGitReadFailed (check the repository and commit ID)");
                return Bytes;
            } finally {
                if (!Process.HasExited) {
                    try { Process.Kill(entireProcessTree: true); }
                    catch (InvalidOperationException) { }
                }
            }
        } catch (OperationCanceledException) {
            throw Invalid("PackageGitTimedOut");
        } catch (Win32Exception) {
            throw Invalid("PackageGitUnavailable");
        }
    }

    private static async Task<byte[]> ReadBounded(Stream Input, int Maximum, CancellationToken Cancellation)
    {
        using var Output = new MemoryStream();
        var Buffer = new byte[8192];
        int Count;
        while ((Count = await Input.ReadAsync(Buffer, Cancellation)) != 0) {
            if (Output.Length + Count > Maximum) throw Invalid("PackageLimit");
            Output.Write(Buffer, 0, Count);
        }
        return Output.ToArray();
    }

    private static bool Hex(string Text) => Text.All(Byte => Byte is >= '0' and <= '9' or >= 'a' and <= 'f');
    private static void RequireSha256(string Text)
    {
        if (Text.Length != 64 || !Hex(Text)) throw Invalid("InvalidPackageDigest");
    }
}
