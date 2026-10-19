using System.Diagnostics;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace Lui.WinUI;

internal sealed partial class WinUIBackend
{
    public async Task<bool> RunConversationQualificationAsync(string? ReportPath = null)
    {
        try {
            static void Require(bool Value, string Message) {if (!Value) throw new InvalidOperationException(Message);}
            var Began = Stopwatch.StartNew();
            bool Ready = false;
            for (int Step = 0; Step < 200; Step++) {
                Pump();
                if (Views.Values.Any(View => (View.Content ?? View.Element) is TextBlock Text && Text.IsLoaded && Text.Text.Contains("10000 messages"))) {Ready = true; break;}
                await Task.Delay(10);
            }
            double Startup = Began.Elapsed.TotalSeconds;
            Require(Ready && Startup <= 10, "StartupBudget");
            var Window = Views.Values.Single(View => View.Window is not null).Window!;
            var Scroll = Views.Single(Pair => Pair.Value.ClassName == "ScrollingFrame");
            Button Action(string Label) => Views.Values.Select(View => View.Element).OfType<Button>().Single(Button => Button.Content as string == Label);
            void Click(string Label) {
                var Peer = FrameworkElementAutomationPeer.CreatePeerForElement(Action(Label));
                Require(Peer?.GetPattern(PatternInterface.Invoke) is IInvokeProvider, "InvokeMapping");
                ((IInvokeProvider)Peer!.GetPattern(PatternInterface.Invoke)).Invoke();
            }
            TextBox Composer = Views.Values.Select(View => View.Element).OfType<TextBox>().Single(Input => Input.AcceptsReturn);
            Require(Composer.PlaceholderText.Contains("synthetic") && Composer.TextWrapping == TextWrapping.Wrap, "ComposerMapping");
            Composer.Text = "A\U0001F642B\nC"; Composer.Select(1, 2); await Task.Delay(40); Pump();
            Require(Composer.SelectedText == "\U0001F642", "SurrogateSelection");
            // Keep the user's clipboard in memory and restore every captured format.
            var Previous = Clipboard.GetContent();
            var Restore = new DataPackage();
            foreach (string Format in Previous.AvailableFormats) Restore.SetData(Format, await Previous.GetDataAsync(Format));
            try {
                static string Lines(string Text) => Text.Replace("\r\n", "\n").Replace("\r", "\n");
                Composer.Focus(FocusState.Programmatic);
                Composer.SelectAll(); Composer.CopySelectionToClipboard();
                Require(Lines(await Clipboard.GetContent().GetTextAsync()) == "A\U0001F642B\nC", "ClipboardMapping");
                Composer.Select(Composer.Text.Length, 0); Composer.PasteFromClipboard();
                await Task.Delay(40); Pump();
                Require(Lines(Composer.Text) == "A\U0001F642B\nCA\U0001F642B\nC", "ClipboardMapping");
            } finally {if (Previous.AvailableFormats.Count == 0) Clipboard.Clear(); else Clipboard.SetContent(Restore);}
            var Secret = Views.Values.Select(View => View.Element).OfType<PasswordBox>().Single();
            Secret.Password = "SECRET_WIDGET_CANARY";
            var SecretPeer = FrameworkElementAutomationPeer.CreatePeerForElement(Secret);
            Require(SecretPeer is not null && SecretPeer.IsPassword(), "PasswordAccessibility");
            Require(!System.Runtime.InteropServices.Marshal.PtrToStringUTF8(Native.Lui_GetPreviewTreeJson(Runtime))!.Contains("SECRET_WIDGET_CANARY"), "SecretRedaction");
            long Baseline = Process.GetCurrentProcess().WorkingSet64, Peak = Baseline;
            for (int Index = 0; Index < 200; Index++) {
                long Started = Stopwatch.GetTimestamp();
                switch (Index % 6) {
                    case 0: Require(Native.Lui_ScrollInput(Runtime, Scroll.Key, 0, 120) == 1, "NativeScroll"); Native.Lui_RecordPerformance(Runtime, 1, Stopwatch.GetElapsedTime(Started).TotalMilliseconds); break;
                    case 1: Click("Insert above"); break;
                    case 2: Click("Edit visible"); break;
                    case 3: Click("Delete visible"); break;
                    case 4: Window.AppWindow.Resize(new Windows.Graphics.SizeInt32(Index % 12 == 4 ? 1000 : 1100, 760)); break;
                    case 5:
                        Click("Settings"); await WaitForPresentationAsync();
                        Click(Index % 12 == 5 ? "18 px" : "14 px"); await WaitForPresentationAsync();
                        Click("History"); break;
                }
                await Task.Delay(32); Pump();
                // A transition ends at native submission. Do not start three synchronous
                // font/navigation activations while the previous resize is awaiting a frame.
                await WaitForPresentationAsync();
                Peak = Math.Max(Peak, Process.GetCurrentProcess().WorkingSet64);
                Require(Views.Count <= 256 && ImageCacheBytes <= 32*1024*1024 && ImageRequests.Count <= 32, "ResourceBudget");
            }
            using var Document = JsonDocument.Parse(System.Runtime.InteropServices.Marshal.PtrToStringUTF8(Native.Lui_GetPerformanceJson(Runtime))!);
            var Metrics = Document.RootElement;
            Require(Peak <= 512*1024*1024 && Metrics.GetProperty("LiveInstances").GetInt32() <= 256 && Metrics.GetProperty("Connections").GetInt32() <= 192, "MemoryBudget");
            Require(Metrics.GetProperty("Input").GetProperty("Samples").GetInt32() >= 100 && Metrics.GetProperty("Input").GetProperty("P95Ms").GetDouble() <= 100 && Metrics.GetProperty("Input").GetProperty("P99Ms").GetDouble() <= 250, "InputBudget");
            Require(Metrics.GetProperty("Layout").GetProperty("P95Ms").GetDouble() <= 50 && Metrics.GetProperty("Layout").GetProperty("P99Ms").GetDouble() <= 100 && Metrics.GetProperty("Render").GetProperty("Samples").GetInt32() >= 100 && Metrics.GetProperty("Render").GetProperty("P95Ms").GetDouble() <= 50, "PresentationBudget");
            await QualifyImageOwnershipAsync();
            LuiDiagnostics.Log("ConversationQualification", Metrics.GetRawText());
            LuiDiagnostics.Log("ConversationQualification", $"StartupSeconds={Startup}; WorkingSetBaselineBytes={Baseline}; WorkingSetPeakBytes={Peak}");
            if (ReportPath is not null) {
                string Full = Path.GetFullPath(ReportPath); Directory.CreateDirectory(Path.GetDirectoryName(Full)!);
                File.WriteAllText(Full, JsonSerializer.Serialize(new {Metrics = Metrics.Clone(), StartupSeconds = Startup, WorkingSetBaselineBytes = Baseline, WorkingSetPeakBytes = Peak}));
            }
            return true;
        } catch (Exception Error) {
            string[] Codes = {"StartupBudget","InvokeMapping","ComposerMapping","SurrogateSelection","ClipboardMapping","PasswordAccessibility","SecretRedaction","NativeScroll","ResourceBudget","MemoryBudget","InputBudget","PresentationBudget","ImageOwnership"};
            string Code = Codes.Contains(Error.Message) ? Error.Message : "QualificationFailed";
            LuiDiagnostics.Error("ConversationQualification", Code);
            if (ReportPath is not null) {
                try {
                    string Full = Path.GetFullPath(ReportPath); Directory.CreateDirectory(Path.GetDirectoryName(Full)!);
                    using var Metrics = JsonDocument.Parse(System.Runtime.InteropServices.Marshal.PtrToStringUTF8(Native.Lui_GetPerformanceJson(Runtime))!);
                    File.WriteAllText(Full, JsonSerializer.Serialize(new {Success = false, Code, ExceptionType = Error.GetType().Name, Error.HResult, Metrics = Metrics.RootElement.Clone()}));
                }
                catch { }
            }
            return false;
        }
    }
    private static async Task QualifyImageOwnershipAsync()
    {
        using var Stream = new InMemoryRandomAccessStream();
        var Encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, Stream);
        byte[] Pixels = new byte[1024*1024*4]; Array.Fill(Pixels, (byte)127);
        Encoder.SetPixelData(BitmapPixelFormat.Rgba8, BitmapAlphaMode.Straight, 1024, 1024, 96, 96, Pixels);
        await Encoder.FlushAsync(); Stream.Seek(0);
        using var Reader = new DataReader(Stream); await Reader.LoadAsync((uint)Stream.Size);
        byte[] Encoded = new byte[(int)Stream.Size]; Reader.ReadBytes(Encoded);
        using var Host = new WinUIBackend();
        for (int Index = 1; Index <= 9; Index++) {
            string Name = $"Image{Index}.png";
            if (Native.Lui_RegisterAsset(Host.Runtime, Name) != 1) throw new InvalidOperationException("ImageOwnership");
            Host.ImageBytes.Add(Name, Encoded);
        }
        if (Native.Lui_RegisterAsset(Host.Runtime, "Invalid.png") != 1) throw new InvalidOperationException("ImageOwnership");
        Host.ImageBytes.Add("Invalid.png", Array.Empty<byte>());
        void Run(string Source) {if (Native.Lui_RunScript(Host.Runtime, Source, "ImageOwnership") != 1) throw new InvalidOperationException("ImageOwnership");}
        Run("Images={}; for Index=1,9 do Images[Index]=Instance.new('ImageLabel',{Source='Image'..Index..'.png'}) end; Invalid=Instance.new('ImageLabel',{Source='Invalid.png'})");
        for (int Step = 0; Step < 500 && Host.ImageRequests.Count != 0; Step++) {await Task.Delay(10); Host.Pump();}
        if (Host.ImageRequests.Count != 0 || Host.ImageCacheBytes > MaxImageCacheBytes) throw new InvalidOperationException("ImageOwnership");
        Run("local Ready,Failed=0,0; for _,Image in Images do if Image.LoadState=='Ready' then Ready+=1 else Failed+=1 end end; assert(Ready==8 and Failed==1 and Invalid.LoadState=='Failed'); for _,Image in Images do Image:Destroy() end; Invalid:Destroy(); Pending=Instance.new('ImageLabel',{Source='Image1.png'}); Pending:Destroy(); Pending:Destroy()");
        await Task.Delay(40); Host.Pump();
        if (Host.ImageRequests.Count != 0 || Host.Views.Count != 0) throw new InvalidOperationException("ImageOwnership");
        Host.Dispose();
        if (Host.ImageCacheBytes != 0) throw new InvalidOperationException("ImageOwnership");
    }
    private async Task WaitForPresentationAsync()
    {
        if (PresentationAt == 0) return;
        var Submitted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        EventHandler<object> Paint = (_, _) => Submitted.TrySetResult();
        Microsoft.UI.Xaml.Media.CompositionTarget.Rendering += Paint;
        try {await Submitted.Task.WaitAsync(TimeSpan.FromSeconds(2));}
        finally {Microsoft.UI.Xaml.Media.CompositionTarget.Rendering -= Paint;}
    }
}
