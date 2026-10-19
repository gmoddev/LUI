using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Storage.Streams;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private sealed class ImageEntry {
        internal required BitmapImage Bitmap;
        internal long Bytes;
        internal int Pins;
        internal long Used;
    }
    private sealed class ImageRequest {
        internal required CancellationTokenSource Cancellation;
        internal string? PinnedKey;
    }
    private readonly Dictionary<string, byte[]> ImageBytes = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ImageEntry> ImageCache = new(StringComparer.Ordinal);
    private readonly Dictionary<ulong, ImageRequest> ImageRequests = new();
    private readonly SemaphoreSlim ImageDecodes = new(2, 2);
    private Native.LoadImageCallback? LoadImageCallback;
    private Native.CancelImageCallback? CancelImageCallback;
    private long ImageCacheBytes, ImageClock, EncodedImageBytes;
    private const long MaxImageCacheBytes = 32*1024*1024;

    private void StartImages() {
        LoadImageCallback = OnLoadImage; CancelImageCallback = OnCancelImage;
        Native.ImageCallbacks Callbacks = new() {StructSize = (uint)Marshal.SizeOf<Native.ImageCallbacks>(), AbiVersion = 1,
            Load = LoadImageCallback, Cancel = CancelImageCallback};
        if (Native.Lui_SetImageCallbacks(Runtime, ref Callbacks) != 1) throw new InvalidOperationException("Image bridge failed");
    }
    private void CaptureAsset(string Name, string Path) {
        if (ImageBytes.Count >= 256 || ImageBytes.ContainsKey(Name)) throw new InvalidOperationException("AssetLimit");
        using var Source = new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (Source.Length > 1024*1024) {ImageBytes.Add(Name, Array.Empty<byte>()); return;}
        if (EncodedImageBytes + Source.Length > 64*1024*1024) throw new InvalidOperationException("AssetLimit");
        byte[] Bytes = new byte[(int)Source.Length]; Source.ReadExactly(Bytes); ImageBytes.Add(Name, Bytes); EncodedImageBytes += Bytes.Length;
    }
    private static bool ImageDimensions(byte[] Data, out int Width, out int Height) {
        Width = Height = 0;
        if (Data.Length == 0 || Data.Length > 1024*1024) return false;
        if (Data.Length >= 24 && Data.AsSpan(0, 8).SequenceEqual(new byte[]{137,80,78,71,13,10,26,10}) &&
            Data.AsSpan(12, 4).SequenceEqual("IHDR"u8)) {
            uint W = BinaryPrimitives.ReadUInt32BigEndian(Data.AsSpan(16, 4)), H = BinaryPrimitives.ReadUInt32BigEndian(Data.AsSpan(20, 4));
            if (W > 4096 || H > 4096) return false; Width = (int)W; Height = (int)H;
        } else if (Data.Length >= 4 && Data[0] == 255 && Data[1] == 216) {
            int Index = 2; bool Found = false;
            while (Index + 3 < Data.Length) {
                if (Data[Index++] != 255) return false;
                while (Index < Data.Length && Data[Index] == 255) Index++;
                if (Index >= Data.Length) return false;
                int Marker = Data[Index++]; if (Marker is 218 or 217) break;
                if (Marker == 1 || Marker is >=208 and <=215) continue;
                if (Index + 2 > Data.Length) return false;
                int Length = (Data[Index] << 8) | Data[Index + 1];
                if (Length < 2 || Length > Data.Length - Index) return false;
                if (Marker is 192 or 193 or 194) {
                    if (Length < 8) return false;
                    Height = (Data[Index + 3] << 8) | Data[Index + 4]; Width = (Data[Index + 5] << 8) | Data[Index + 6]; Found = true; break;
                }
                Index += Length;
            }
            if (!Found) return false;
        } else return false;
        return Width > 0 && Height > 0 && (long)Width*Height <= 1024*1024;
    }
    private void ReleaseImage(View Target) {
        if (!string.IsNullOrEmpty(Target.ImageKey) && ImageCache.TryGetValue(Target.ImageKey, out var Entry)) Entry.Pins--;
        Target.ImageKey = string.Empty;
        if ((Target.Content ?? Target.Element) is Image Picture && Picture.Source is not null) {MarkPresentation(); Picture.Source = null;}
    }
    private int OnLoadImage(IntPtr Context, int Id, ulong Token, string Source) {
        if (IsDisposed || IsDisposing || !Views.TryGetValue(Id, out var Target)) return 0;
        ReleaseImage(Target); Target.ImageToken = Token;
        if (ImageRequests.Count >= 32 || !ImageBytes.TryGetValue(Source, out var Bytes)) return 0;
        var Request = new ImageRequest {Cancellation = new()}; ImageRequests.Add(Token, Request);
        if (ImageCache.TryGetValue(Source, out var Cached)) {Cached.Pins++; Request.PinnedKey = Source;}
        if (!Target.Element!.DispatcherQueue.TryEnqueue(() => { _ = DecodeImage(Id, Token, Source, Bytes, Request); })) {
            OnCancelImage(Context, Token); Request.Cancellation.Dispose(); return 0;
        }
        return 1;
    }
    private void OnCancelImage(IntPtr Context, ulong Token) {
        if (!ImageRequests.Remove(Token, out var Request)) return;
        Request.Cancellation.Cancel();
        if (Request.PinnedKey is not null && ImageCache.TryGetValue(Request.PinnedKey, out var Entry)) Entry.Pins--;
        Request.PinnedKey = null;
        foreach (var Target in Views.Values) if (Target.ImageToken == Token) Target.ImageToken = 0;
    }
    private async Task DecodeImage(int Id, ulong Token, string Source, byte[] Bytes, ImageRequest Request) {
        bool Entered = false;
        try {
            await ImageDecodes.WaitAsync(Request.Cancellation.Token); Entered = true;
            Request.Cancellation.Token.ThrowIfCancellationRequested();
            if (IsDisposed || IsDisposing) return;
            if (!ImageCache.TryGetValue(Source, out var Cached)) {
                if (!ImageDimensions(Bytes, out int Width, out int Height)) throw new InvalidDataException("ImageDimensions");
                long Size = (long)Width*Height*4;
                while ((ImageCache.Count >= 16 || ImageCacheBytes + Size > MaxImageCacheBytes) && ImageCache.Count > 0) {
                    var Victim = ImageCache.Where(Pair => Pair.Value.Pins == 0).OrderBy(Pair => Pair.Value.Used).FirstOrDefault();
                    if (Victim.Key is null) break;
                    ImageCacheBytes -= Victim.Value.Bytes; ImageCache.Remove(Victim.Key);
                }
                if (ImageCache.Count >= 16 || ImageCacheBytes + Size > MaxImageCacheBytes) throw new InvalidDataException("ImageBudget");
                using var Stream = new InMemoryRandomAccessStream();
                using (var Writer = new DataWriter(Stream)) {Writer.WriteBytes(Bytes); await Writer.StoreAsync().AsTask(Request.Cancellation.Token); Writer.DetachStream();}
                Stream.Seek(0); BitmapImage Bitmap = new();
                await Bitmap.SetSourceAsync(Stream).AsTask(Request.Cancellation.Token);
                if (Bitmap.PixelWidth != Width || Bitmap.PixelHeight != Height) throw new InvalidDataException("ImageDimensions");
                Request.Cancellation.Token.ThrowIfCancellationRequested();
                if (IsDisposed || IsDisposing) return;
                if (!ImageCache.TryGetValue(Source, out Cached)) {
                    if (ImageCache.Count >= 16 || ImageCacheBytes + Size > MaxImageCacheBytes) throw new InvalidDataException("ImageBudget");
                    Cached = new ImageEntry {Bitmap = Bitmap, Bytes = Size, Used = ++ImageClock}; ImageCache.Add(Source, Cached); ImageCacheBytes += Size;
                }
            }
            if (!ImageRequests.ContainsKey(Token) || !Views.TryGetValue(Id, out var Target) || !Target.IsAlive || Target.ImageToken != Token) return;
            ImageRequests.Remove(Token);
            if (Request.PinnedKey is not null) {Cached.Pins--; Request.PinnedKey = null;}
            Cached.Pins++; Cached.Used = ++ImageClock; Target.ImageKey = Source;
            if ((Target.Content ?? Target.Element) is Image Picture) {MarkPresentation(); Picture.Source = Cached.Bitmap;}
            Native.Lui_ImageCompleted(Runtime, Id, Token, 1);
        } catch (OperationCanceledException) {
            // The owner already retired this token; no callback or diagnostic.
        } catch {
            if (!IsDisposed && !IsDisposing && ImageRequests.Remove(Token)) {
                if (Request.PinnedKey is not null && ImageCache.TryGetValue(Request.PinnedKey, out var Entry)) Entry.Pins--;
                Request.PinnedKey = null; Native.Lui_ImageCompleted(Runtime, Id, Token, 0);
            }
        } finally {
            if (Entered) ImageDecodes.Release(); Request.Cancellation.Dispose();
        }
    }
    private void StopImages() {
        foreach (ulong Token in ImageRequests.Keys.ToArray()) OnCancelImage(IntPtr.Zero, Token);
        foreach (var Target in Views.Values) ReleaseImage(Target);
        ImageCache.Clear(); ImageCacheBytes = 0; ImageBytes.Clear(); EncodedImageBytes = 0;
    }
}
