#include "ByteWorker.h"
#include <algorithm>
#include <array>
#include <memory>
#include <zlib.h>
#include <zstd.h>

namespace Lui::Standard {
namespace {
constexpr uint32_t MaxBytes = 1024*1024;
const LuiNativeMethodV2 Methods[] = {
    {sizeof(LuiNativeMethodV2),2,"Encode",MaxBytes+5,MaxBytes,0,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Decode",MaxBytes+5,MaxBytes,0,0,30000,0},
};
uint32_t Zstandard(bool Encode,const uint8_t* Data,size_t Size,unsigned Limit,
    const std::atomic<bool>& Canceled,std::vector<uint8_t>& Output) {
    std::unique_ptr<ZSTD_CCtx,decltype(&ZSTD_freeCCtx)> Encoder(Encode?ZSTD_createCCtx():nullptr,ZSTD_freeCCtx);
    std::unique_ptr<ZSTD_DCtx,decltype(&ZSTD_freeDCtx)> Decoder(Encode?nullptr:ZSTD_createDCtx(),ZSTD_freeDCtx);
    if (Encode) {
        if (!Encoder || ZSTD_isError(ZSTD_CCtx_setParameter(Encoder.get(),ZSTD_c_checksumFlag,1)) ||
            ZSTD_isError(ZSTD_CCtx_setParameter(Encoder.get(),ZSTD_c_windowLog,20)) ||
            ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(Encoder.get(),Size))) return LUI_NATIVE_PROVIDER_FAILURE;
    } else {
        ZSTD_FrameHeader Header{};
        if (ZSTD_getFrameHeader(&Header,Data,Size)!=0 || Header.frameType!=ZSTD_frame || Header.dictID)
            return LUI_NATIVE_INVALID_ARGUMENT;
        if (Header.windowSize>MaxBytes || (Header.frameContentSize!=ZSTD_CONTENTSIZE_UNKNOWN && Header.frameContentSize>Limit))
            return LUI_NATIVE_LIMIT_EXCEEDED;
        if (!Decoder || ZSTD_isError(ZSTD_DCtx_setParameter(Decoder.get(),ZSTD_d_windowLogMax,20))) return LUI_NATIVE_PROVIDER_FAILURE;
    }
    std::array<uint8_t,16384> Block;
    size_t Offset=0;
    while (!Canceled) {
        const auto Chunk=std::min(Block.size(),Size-Offset);
        const bool Last=Offset+Chunk==Size;
        ZSTD_inBuffer Input{Data+Offset,Chunk,0};
        ZSTD_outBuffer Dest{Block.data(),std::min<size_t>(Block.size(),Limit-Output.size()+1),0};
        const size_t Remaining=Encode ? ZSTD_compressStream2(Encoder.get(),&Dest,&Input,Last?ZSTD_e_end:ZSTD_e_continue) :
            ZSTD_decompressStream(Decoder.get(),&Dest,&Input);
        if (ZSTD_isError(Remaining)) return LUI_NATIVE_INVALID_ARGUMENT;
        if (Dest.pos>Limit-Output.size()) return LUI_NATIVE_LIMIT_EXCEEDED;
        Output.insert(Output.end(),Block.begin(),Block.begin()+Dest.pos);
        Offset+=Input.pos;
        if (Remaining==0 && (!Encode || Last)) return Offset==Size?LUI_NATIVE_OK:LUI_NATIVE_INVALID_ARGUMENT;
        if (!Input.pos && !Dest.pos) return LUI_NATIVE_INVALID_ARGUMENT;
    }
    return LUI_NATIVE_CANCELED;
}
}
const LuiNativeServiceV2 Service{sizeof(Service),2,"Compression",Methods,2,0,3,1,0};
uint32_t Process(uint32_t Method,const uint8_t* Bytes,size_t Size,const std::atomic<bool>& Canceled,std::vector<uint8_t>& Output) {
    if (Method>1 || Size<5 || Size>MaxBytes+5 || Bytes[0]>3) return LUI_NATIVE_INVALID_ARGUMENT;
    const unsigned Limit = uint32_t(Bytes[1]) | uint32_t(Bytes[2])<<8 | uint32_t(Bytes[3])<<16 | uint32_t(Bytes[4])<<24;
    if (Limit>MaxBytes) return LUI_NATIVE_INVALID_ARGUMENT;
    if (Bytes[0]==3) return Zstandard(Method==0,Bytes+5,Size-5,Limit,Canceled,Output);
    const int Window = Bytes[0]==0 ? 31 : Bytes[0]==1 ? 15 : -15;
    z_stream Stream{};
    const bool Encode = Method==0;
    const int Opened = Encode ? deflateInit2(&Stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,Window,8,Z_DEFAULT_STRATEGY) :
        inflateInit2(&Stream,Window);
    if (Opened!=Z_OK) return LUI_NATIVE_PROVIDER_FAILURE;
    struct End { z_stream* Stream; bool Encode; ~End(){ if(Encode) deflateEnd(Stream); else inflateEnd(Stream); } } Ender{&Stream,Encode};
    std::array<uint8_t,16384> Block;
    size_t Offset=5;
    while (!Canceled) {
        if (!Stream.avail_in && Offset<Size) {
            const auto Count=std::min<size_t>(Block.size(),Size-Offset);
            Stream.next_in=const_cast<Bytef*>(Bytes+Offset);
            Stream.avail_in=static_cast<uInt>(Count); Offset+=Count;
        }
        // One extra byte distinguishes an exact-size valid stream from overflow.
        const auto Capacity=std::min<size_t>(Block.size(),Limit-Output.size()+1);
        Stream.next_out=Block.data(); Stream.avail_out=static_cast<uInt>(Capacity);
        const int State=Encode ? deflate(&Stream,Offset==Size?Z_FINISH:Z_NO_FLUSH) : inflate(&Stream,Z_NO_FLUSH);
        const size_t Produced=Capacity-Stream.avail_out;
        if (Produced>Limit-Output.size()) return LUI_NATIVE_LIMIT_EXCEEDED;
        Output.insert(Output.end(),Block.begin(),Block.begin()+Produced);
        if (State==Z_STREAM_END)
            return Offset==Size && !Stream.avail_in ? LUI_NATIVE_OK : LUI_NATIVE_INVALID_ARGUMENT;
        if (State!=Z_OK) return State==Z_MEM_ERROR?LUI_NATIVE_LIMIT_EXCEEDED:LUI_NATIVE_INVALID_ARGUMENT;
    }
    return LUI_NATIVE_CANCELED;
}
}
