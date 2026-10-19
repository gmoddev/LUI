#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace LuiImages {
constexpr size_t MaxEncoded = 1024*1024, MaxPixels = 1024*1024, MaxCache = 32*1024*1024;
inline bool Dimensions(std::string_view Bytes,uint32_t& Width,uint32_t& Height) {
    if(Bytes.empty() || Bytes.size()>MaxEncoded) return false;
    auto Byte=[&](size_t Index) {return static_cast<unsigned char>(Bytes[Index]);};
    if(Bytes.size()>=24 && Bytes.substr(0,8)==std::string_view("\x89PNG\r\n\x1a\n",8) && Bytes.substr(12,4)=="IHDR") {
        Width=(uint32_t(Byte(16))<<24)|(uint32_t(Byte(17))<<16)|(uint32_t(Byte(18))<<8)|Byte(19);
        Height=(uint32_t(Byte(20))<<24)|(uint32_t(Byte(21))<<16)|(uint32_t(Byte(22))<<8)|Byte(23);
    } else if(Bytes.size()>=4 && Byte(0)==0xff && Byte(1)==0xd8) {
        bool Found=false; size_t Index=2;
        while(Index+3<Bytes.size()) {
            if(Byte(Index++)!=0xff) return false;
            while(Index<Bytes.size() && Byte(Index)==0xff) ++Index;
            if(Index>=Bytes.size()) return false;
            const auto Marker=Byte(Index++);
            if(Marker==0xda || Marker==0xd9) break;
            if(Marker==0x01 || (Marker>=0xd0 && Marker<=0xd7)) continue;
            if(Index+2>Bytes.size()) return false;
            const size_t Length=(size_t(Byte(Index))<<8)|Byte(Index+1);
            if(Length<2 || Length>Bytes.size()-Index) return false;
            if(Marker==0xc0 || Marker==0xc1 || Marker==0xc2) {
                if(Length<8) return false;
                Height=(uint32_t(Byte(Index+3))<<8)|Byte(Index+4); Width=(uint32_t(Byte(Index+5))<<8)|Byte(Index+6); Found=true; break;
            }
            Index+=Length;
        }
        if(!Found) return false;
    } else return false;
    return Width && Height && Width<=4096 && Height<=4096 && uint64_t(Width)*Height<=MaxPixels;
}
}
