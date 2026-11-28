using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Foundation;
using Windows.Storage.Streams;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    private readonly record struct ImageRegion(int X, int Y, int Width, int Height) {
        internal static ImageRegion Parse(string Value) {
            var Parts = Value.Split(',');
            return new ImageRegion(int.Parse(Parts[0], CultureInfo.InvariantCulture),
                int.Parse(Parts[1], CultureInfo.InvariantCulture), int.Parse(Parts[2], CultureInfo.InvariantCulture),
                int.Parse(Parts[3], CultureInfo.InvariantCulture));
        }
    }
    private sealed class ImageEntry {
        internal required string Key;
        internal required BitmapSource Bitmap;
        internal required long Bytes;
        internal int Pins;
        internal LinkedListNode<ImageEntry>? Inactive;
    }
    private sealed class ImageRequest {
        internal required int Id;
        internal ImageEntry? Cached;
        internal ImageDecode? Decode;
    }
    private sealed class ImageDecode {
        internal required string Key;
        internal byte[]? Bytes;
        internal ImageSourceLease? Source;
        internal Native.ImageSourceView View;
        internal readonly HashSet<ulong> Tokens = new();
        internal readonly CancellationTokenSource Cancellation = new();
        internal bool Dispatched, Started;
    }
    private sealed class ImageSourceLease : SafeHandle {
        internal ImageSourceLease(IntPtr Source) : base(IntPtr.Zero, true) {
            Native.Lui_RetainImageSourceV1(Source);
            SetHandle(Source);
        }
        public override bool IsInvalid => handle == IntPtr.Zero;
        protected override bool ReleaseHandle() {
            Native.Lui_ReleaseImageSourceV1(handle);
            return true;
        }
    }
    private sealed class InactiveImages {
        internal readonly LinkedList<ImageEntry> Entries = new();
        internal long Bytes;
    }
    private readonly Dictionary<string, byte[]> ImageBytes = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ImageEntry> ImageCache = new(StringComparer.Ordinal);
    private readonly Dictionary<ulong, ImageRequest> ImageRequests = new();
    private readonly Dictionary<string, ImageDecode> ImageFlights = new(StringComparer.Ordinal);
    private readonly InactiveImages InactiveIcons = new(), InactiveThumbnails = new();
    private readonly SemaphoreSlim ImageDecodes = new(2, 2);
    private Native.LoadImageCallback? LoadImageCallback;
    private Native.LoadImageSourceCallback? LoadImageSourceCallback;
    private Native.CancelImageCallback? CancelImageCallback;
    private long ImageCacheBytes, EncodedImageBytes;
    private const long MaxImageCacheBytes = 32*1024*1024;
    private const long MaxInactiveBytesPerTier = 8*1024*1024;
    private const int MaxImageEntries = 2048, MaxInactiveEntriesPerTier = 1024;
    private const long IconBytes = 64*64*4, MaxReusableImageBytes = 1024*1024;
    private long ImageCacheHits, ImageCacheMisses, ImageCoalesced, ImageCanceled, ImageEvictions, ImagesRetired;
    private long ImageDecodeCount, ImageDecodeTicks, PeakImageCacheBytes;
    private bool ImageDispatchQueued;
    private int ImageJobs;
    private long ImageDecodingBytes;
    private TaskCompletionSource? ImageJobsRetired;

    private void ApplyImageRegion(View Target) {
        if (Target.Content is not Image Picture || Target.Element is not Grid Shell) return;
        var Region = Target.ImageRegion;
        if (Region.Width == 0) {
            if (Target.ImageViewport is not { } Previous) return;
            Previous.Children.Remove(Picture);
            Shell.Children.Remove(Previous);
            Target.ImageViewport = null;
            Picture.Visibility = Visibility.Visible;
            Picture.Stretch = Stretch.Uniform;
            Picture.Width = double.NaN;
            Picture.Height = double.NaN;
            Shell.Children.Add(Picture);
            return;
        }
        if (Target.ImageViewport is null) {
            var Canvas = new Canvas { IsHitTestVisible = false };
            Shell.Children.Remove(Picture);
            Canvas.Children.Add(Picture);
            Shell.Children.Add(Canvas);
            Target.ImageViewport = Canvas;
        }
        var Viewport = Target.ImageViewport;
        double Width = double.IsFinite(Shell.Width) ? Shell.Width : 0;
        double Height = double.IsFinite(Shell.Height) ? Shell.Height : 0;
        if (Picture.Source is BitmapSource Bitmap &&
            Region.X + Region.Width <= Bitmap.PixelWidth && Region.Y + Region.Height <= Bitmap.PixelHeight) {
            double Scale = Math.Min(Width / Region.Width, Height / Region.Height);
            double Left = (Width - Region.Width * Scale) / 2;
            double Top = (Height - Region.Height * Scale) / 2;
            var Clip = Viewport.Clip ?? new RectangleGeometry();
            Clip.Rect = new Rect(Left, Top, Region.Width * Scale, Region.Height * Scale);
            Viewport.Clip = Clip;
            Picture.Visibility = Visibility.Visible;
            Picture.Stretch = Stretch.Fill;
            Picture.Width = Bitmap.PixelWidth * Scale;
            Picture.Height = Bitmap.PixelHeight * Scale;
            Canvas.SetLeft(Picture, Left - Region.X * Scale);
            Canvas.SetTop(Picture, Top - Region.Y * Scale);
        } else {
            Picture.Visibility = Visibility.Collapsed;
        }
    }

    private void StartImages() {
        LoadImageCallback = OnLoadImage; CancelImageCallback = OnCancelImage;
        Native.ImageCallbacks Callbacks = new() {StructSize = (uint)Marshal.SizeOf<Native.ImageCallbacks>(), AbiVersion = 1,
            Load = LoadImageCallback, Cancel = CancelImageCallback};
        if (Native.Lui_SetImageCallbacks(Runtime, ref Callbacks) != 1) throw new InvalidOperationException("Image bridge failed");
        LoadImageSourceCallback = OnLoadImageSource;
        Native.ImageSourceCallbacks Sources = new() {StructSize = (uint)Marshal.SizeOf<Native.ImageSourceCallbacks>(),
            AbiVersion = 1, Load = LoadImageSourceCallback};
        if (Native.Lui_SetImageSourceCallbacksV1(Runtime, ref Sources) != 1) throw new InvalidOperationException("Image source bridge failed");
    }
    private void CaptureAsset(string Name, string Path) {
        if (ImageBytes.Count >= 1024 || ImageBytes.ContainsKey(Name)) throw new InvalidOperationException("AssetLimit");
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
        return Width is > 0 and <= 4096 && Height is > 0 and <= 4096 && (long)Width*Height <= 1024*1024;
    }
    private void ReleaseImage(View Target) {
        if (Target.ClassName == "FrameSurface") return; // Frame leases have their own retirement boundary.
        if ((Target.Content ?? Target.Element) is Image Picture && Picture.Source is not null) {MarkPresentation(); Picture.Source = null;}
        if (Target.ImageResource is { } Entry) {
            Target.ImageResource = null;
            UnpinImage(Entry);
        }
    }
    private InactiveImages InactiveTier(ImageEntry Entry) => Entry.Bytes <= IconBytes ? InactiveIcons : InactiveThumbnails;
    private void PinImage(ImageEntry Entry) {
        if (Entry.Inactive is { } Node) {
            var Tier = InactiveTier(Entry);
            Tier.Entries.Remove(Node);
            Tier.Bytes -= Entry.Bytes;
            Entry.Inactive = null;
        }
        Entry.Pins++;
    }
    private void ForgetImage(ImageEntry Entry, bool Evicted = true) {
        if (Entry.Inactive is { } Node) {
            var Tier = InactiveTier(Entry);
            Tier.Entries.Remove(Node);
            Tier.Bytes -= Entry.Bytes;
            Entry.Inactive = null;
        }
        ImageCache.Remove(Entry.Key);
        ImageCacheBytes -= Entry.Bytes;
        if (Evicted) ImageEvictions++;
        ImagesRetired++;
    }
    private void UnpinImage(ImageEntry Entry) {
        if (--Entry.Pins != 0) return;
        if (Entry.Bytes > MaxReusableImageBytes) {
            ForgetImage(Entry, Evicted: false);
            return;
        }
        var Tier = InactiveTier(Entry);
        try { Entry.Inactive = Tier.Entries.AddLast(Entry); }
        catch (OutOfMemoryException) { ForgetImage(Entry, Evicted: false); return; }
        Tier.Bytes += Entry.Bytes;
        while (Tier.Bytes > MaxInactiveBytesPerTier || Tier.Entries.Count > MaxInactiveEntriesPerTier)
            ForgetImage(Tier.Entries.First!.Value);
    }
    private bool CanFitActiveImage(long Bytes) =>
        ImageCacheBytes - InactiveIcons.Bytes - InactiveThumbnails.Bytes + Bytes <= MaxImageCacheBytes &&
        ImageCache.Count - InactiveIcons.Entries.Count - InactiveThumbnails.Entries.Count < MaxImageEntries;
    private bool MakeImageRoom(long Bytes) {
        if (!CanFitActiveImage(Bytes)) return false;
        // Keep hot icons resident through thumbnail churn whenever inactive
        // thumbnail eviction alone can make room. Active entries never move.
        while (ImageCache.Count >= MaxImageEntries || ImageCacheBytes + Bytes > MaxImageCacheBytes) {
            var Victim = InactiveThumbnails.Entries.First ?? InactiveIcons.Entries.First;
            if (Victim is null) return false;
            ForgetImage(Victim.Value);
        }
        return true;
    }
    private int OnLoadImage(IntPtr Context, int Id, ulong Token, string Source) {
        if (!ImageBytes.TryGetValue(Source, out var Bytes)) return 0;
        try { return AcceptImage(Id, Token, Source, Bytes, IntPtr.Zero, default); }
        catch { OnCancelImage(Context, Token); return 0; }
    }
    private int OnLoadImageSource(IntPtr Context, int Id, ulong Token, IntPtr Source) {
        var View = new Native.ImageSourceView { StructSize = (uint)Marshal.SizeOf<Native.ImageSourceView>(), AbiVersion = 1 };
        if (Native.Lui_ImageSourceViewV1(Source, ref View) != 1) return 0;
        try { return AcceptImage(Id, Token, "\0" + View.Identity.ToString(CultureInfo.InvariantCulture), null, Source, View); }
        catch { OnCancelImage(Context, Token); return 0; }
    }
    private int AcceptImage(int Id, ulong Token, string Key, byte[]? Bytes, IntPtr Source, Native.ImageSourceView View) {
        if (IsDisposed || IsDisposing || !Views.TryGetValue(Id, out var Target)) return 0;
        if (ImageRequests.Count >= 32) return 0;
        if (!ImageDispatchQueued) {
            if (!Target.Element!.DispatcherQueue.TryEnqueue(DispatchImages)) return 0;
            ImageDispatchQueued = true;
        }
        var Request = new ImageRequest {Id = Id};
        ImageRequests.Add(Token, Request);
        Target.ImageToken = Token;
        if (ImageCache.TryGetValue(Key, out var Cached)) {
            ImageCacheHits++;
            PinImage(Cached);
            Request.Cached = Cached;
            return 1;
        }
        ImageCacheMisses++;
        if (ImageFlights.TryGetValue(Key, out var Pending)) {
            ImageCoalesced++;
            Pending.Tokens.Add(Token);
            Request.Decode = Pending;
            return 1;
        }
        var Decode = new ImageDecode {Key = Key, Bytes = Bytes, View = View};
        Request.Decode = Decode;
        if (Source != IntPtr.Zero) Decode.Source = new ImageSourceLease(Source);
        Decode.Tokens.Add(Token);
        ImageFlights.Add(Key, Decode);
        return 1;
    }
    private void DispatchImages() {
        ImageDispatchQueued = false;
        if (IsDisposed || IsDisposing) return;
        // Snapshot tokens instead of resources. Callbacks may retire any later
        // request or queue new requests; their work belongs to the next turn.
        var Warm = ImageRequests.Where(Pair => Pair.Value.Cached is not null).Select(Pair => Pair.Key).ToArray();
        var Cold = ImageFlights.Values.Where(Flight => !Flight.Dispatched).ToArray();
        foreach (ulong Token in Warm)
            if (ImageRequests.TryGetValue(Token, out var Request)) CompleteImage(Token, Request.Cached);
        foreach (var Flight in Cold) {
            if (Flight.Tokens.Count == 0) continue;
            Flight.Dispatched = true;
            ImageJobs++;
            _ = DecodeImage(Flight);
        }
    }
    private void OnCancelImage(IntPtr Context, ulong Token) {
        if (!ImageRequests.Remove(Token, out var Request)) return;
        ImageCanceled++;
        if (Request.Cached is { } Cached) UnpinImage(Cached);
        if (Request.Decode is { } Decode) {
            Decode.Tokens.Remove(Token);
            if (Decode.Tokens.Count == 0 && !Decode.Started) {
                ImageFlights.Remove(Decode.Key);
                if (Decode.Dispatched) Decode.Cancellation.Cancel();
                else {
                    Decode.Source?.Dispose();
                    Decode.Cancellation.Dispose();
                }
            }
        }
        if (Views.TryGetValue(Request.Id, out var Target) && Target.ImageToken == Token) Target.ImageToken = 0;
    }
    private void CompleteImage(ulong Token, ImageEntry? Entry) {
        if (!ImageRequests.Remove(Token, out var Request)) return;
        bool Success = false;
        try {
            if (IsDisposed || IsDisposing || !Views.TryGetValue(Request.Id, out var Target) ||
                !Target.IsAlive || Target.ImageToken != Token) return;
            Target.ImageToken = 0;
            if (Entry is not null && (Target.Content ?? Target.Element) is Image Picture) {
                try {
                    Picture.Source = Entry.Bitmap;
                    ApplyImageRegion(Target);
                    MarkPresentation();
                    PinImage(Entry);
                    Target.ImageResource = Entry;
                    Success = true;
                } catch {
                    Picture.Source = null;
                    LuiDiagnostics.Error("Asset", "ImagePresentationFailed");
                }
            }
        } finally {
            if (Request.Cached is { } Cached) UnpinImage(Cached);
        }
        Native.Lui_ImageCompleted(Runtime, Request.Id, Token, Success ? 1 : 0);
    }
    private static unsafe WriteableBitmap CreatePixelImage(Native.ImageSourceView Source) {
        var Bitmap = new WriteableBitmap((int)Source.Width, (int)Source.Height);
        using (var Pixels = Bitmap.PixelBuffer.AsStream())
            Pixels.Write(new ReadOnlySpan<byte>(Source.Data.ToPointer(), (int)Source.ByteCount));
        Bitmap.Invalidate();
        return Bitmap;
    }
    private async Task<BitmapImage> DecodeImageBytes(byte[] Bytes, CancellationToken Cancellation) {
        using var Stream = new InMemoryRandomAccessStream();
        using (var Writer = new DataWriter(Stream)) {
            Writer.WriteBytes(Bytes);
            await Writer.StoreAsync().AsTask(Cancellation);
            Writer.DetachStream();
        }
        Stream.Seek(0);
        var Bitmap = new BitmapImage();
        await Bitmap.SetSourceAsync(Stream).AsTask(Cancellation);
        return Bitmap;
    }
    private async Task DecodeImage(ImageDecode Decode) {
        bool Entered = false;
        long Staged = 0;
        ImageEntry? Entry = null;
        try {
            var Cancellation = Decode.Cancellation.Token;
            await ImageDecodes.WaitAsync(Cancellation);
            Entered = true;
            if (IsDisposed || IsDisposing || Decode.Tokens.Count == 0) return;
            Decode.Started = true;
            int Width, Height;
            if (Decode.Source is not null) {
                Width = (int)Decode.View.Width;
                Height = (int)Decode.View.Height;
            } else if (!ImageDimensions(Decode.Bytes!, out Width, out Height)) return;
            long Size = (long)Width*Height*4;
            if (!CanFitActiveImage(Size)) return;
            ImageDecodingBytes += Size;
            Staged = Size;
            long Started = Stopwatch.GetTimestamp();
            BitmapSource Bitmap;
            if (Decode.View.Kind == 2) Bitmap = CreatePixelImage(Decode.View);
            else {
                ImageDecodeCount++;
                try {
                    byte[] Bytes = Decode.Bytes ?? new byte[(int)Decode.View.ByteCount];
                    if (Decode.Bytes is null) Marshal.Copy(Decode.View.Data, Bytes, 0, Bytes.Length);
                    Bitmap = await DecodeImageBytes(Bytes, Cancellation);
                } finally {
                    ImageDecodeTicks += Stopwatch.GetTimestamp() - Started;
                }
            }
            if (IsDisposed || IsDisposing || Decode.Tokens.Count == 0 ||
                Bitmap.PixelWidth != Width || Bitmap.PixelHeight != Height || !MakeImageRoom(Size)) return;
            var Admitted = new ImageEntry {Key = Decode.Key, Bitmap = Bitmap, Bytes = Size, Pins = 1};
            ImageCache.Add(Decode.Key, Admitted);
            ImageCacheBytes += Size;
            Entry = Admitted;
            PeakImageCacheBytes = Math.Max(PeakImageCacheBytes, ImageCacheBytes);
        } catch {
            // Malformed data and retired decodes settle through the image contract.
        } finally {
            if (ImageFlights.TryGetValue(Decode.Key, out var Current) && ReferenceEquals(Current, Decode))
                ImageFlights.Remove(Decode.Key);
            ImageDecodingBytes -= Staged;
            Decode.Source?.Dispose();
            Decode.Cancellation.Dispose();
            try {
                foreach (ulong Token in Decode.Tokens.ToArray()) CompleteImage(Token, Entry);
            } finally {
                if (Entry is not null && !IsDisposed && !IsDisposing) UnpinImage(Entry);
                if (Entered) ImageDecodes.Release();
                if (--ImageJobs == 0) ImageJobsRetired?.TrySetResult();
            }
        }
    }
    private void StopImages() {
        foreach (ulong Token in ImageRequests.Keys.ToArray()) OnCancelImage(IntPtr.Zero, Token);
        var Flights = ImageFlights.Values.ToArray();
        ImageFlights.Clear();
        foreach (var Flight in Flights) Flight.Cancellation.Cancel();
        foreach (var Target in Views.Values) ReleaseImage(Target);
        ObserveImages("BeforeStop");
        ImagesRetired += ImageCache.Count;
        ImageCache.Clear(); ImageCacheBytes = 0; ImageBytes.Clear(); EncodedImageBytes = 0;
        InactiveIcons.Entries.Clear(); InactiveIcons.Bytes = 0;
        InactiveThumbnails.Entries.Clear(); InactiveThumbnails.Bytes = 0;
        ObserveImages("AfterStop");
    }
    private void ObserveImages(string Stage) {
        int Inactive = InactiveIcons.Entries.Count + InactiveThumbnails.Entries.Count;
        LuiDiagnostics.Log("ImageResources", FormattableString.Invariant(
            $"{Stage} decodes={ImageDecodeCount} decodeMs={Stopwatch.GetElapsedTime(0, ImageDecodeTicks).TotalMilliseconds:F3} hits={ImageCacheHits} misses={ImageCacheMisses} coalesced={ImageCoalesced} canceled={ImageCanceled} evictions={ImageEvictions} active={ImageCache.Count - Inactive} inactive={Inactive} retainedBytes={ImageCacheBytes} peakBytes={PeakImageCacheBytes} encodedBytes={EncodedImageBytes} retired={ImagesRetired} pending={ImageRequests.Count} jobs={ImageJobs} stagingBytes={ImageDecodingBytes}"));
    }
    internal async Task WaitForImageRetirementAsync() {
        if (ImageJobs > 0) {
            ImageJobsRetired ??= new(TaskCreationOptions.RunContinuationsAsynchronously);
            try { await ImageJobsRetired.Task.WaitAsync(TimeSpan.FromSeconds(2)); }
            catch (TimeoutException) { ObserveImages("RetirementDeadline"); return; }
        }
        ObserveImages("Settled");
    }
}
