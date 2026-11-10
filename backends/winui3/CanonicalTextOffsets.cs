using System.Buffers;
using System.Text;

namespace Lui.WinUI;

// Maps ordered Unicode-scalar boundaries to TOM UTF-16 character positions in
// one pass. Styled source uses canonical LF; Rich Edit's external CRLF expansion
// must never be used to calculate TOM offsets.
internal static class CanonicalTextOffsets
{
    internal static bool TryMapScalarBoundaries(string Source, ReadOnlySpan<int> ScalarBoundaries,
        Span<int> TomBoundaries)
    {
        if(Source.Length>262144 || ScalarBoundaries.Length>256 ||
            TomBoundaries.Length!=ScalarBoundaries.Length) return false;
        int Previous=0;
        foreach(int Boundary in ScalarBoundaries)
        {
            if(Boundary<Previous || Boundary<0) return false;
            Previous=Boundary;
        }
        int Utf16=0, Scalar=0, Next=0;
        while(true)
        {
            while(Next<ScalarBoundaries.Length && ScalarBoundaries[Next]==Scalar)
                TomBoundaries[Next++]=Utf16;
            if(Utf16==Source.Length) break;
            OperationStatus Status=Rune.DecodeFromUtf16(Source.AsSpan(Utf16),out Rune Value,out int Units);
            if(Status!=OperationStatus.Done || Value.Value==0 || Value.Value=='\r') return false;
            Utf16+=Units;
            ++Scalar;
        }
        return Next==ScalarBoundaries.Length;
    }
}
