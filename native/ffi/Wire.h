#pragma once
#include "Core.h"
#include "Packets.h"
#include <string_view>
namespace Lui::Ffi {
inline std::string BinaryFailure(const char* Reason) {
    return std::string(1,'\1')+Reason;
}
inline std::string Exchange(Engine& Service,std::string_view Packet) {
    if(IsBinaryPacket(Packet)){
        try {
            Require(Packet.size()<=131072,"PacketLimit");
            return Service.DispatchBinary(Packet.substr(BinaryTag.size()));
        } catch(const Failure& Problem){return BinaryFailure(Problem.what());}
        catch(const std::bad_alloc&){return BinaryFailure("MemoryLimit");}
        catch(...){return BinaryFailure("InvalidNativeRequest");}
    }
    const auto Checkpoint=Service.HandleCheckpoint();
    try {
        Require(Packet.size()<=131072,"PacketLimit");
        auto Data=Json::parse(Packet.begin(),Packet.end(),[](int Depth,Json::parse_event_t,Json&){
            Require(Depth<=24,"PacketDepth");return true;
        });
        auto Output=Json{{"Ok",true},{"Result",Service.Dispatch(Data)}}.dump();
        Require(Output.size()<=524288,"ResultLimit");return Output;
    } catch(const Failure& Problem){Service.RollbackHandles(Checkpoint);return Json{{"Ok",false},{"Error",Problem.what()}}.dump();}
    catch(const std::bad_alloc&){Service.RollbackHandles(Checkpoint);return R"({"Ok":false,"Error":"MemoryLimit"})";}
    catch(...){Service.RollbackHandles(Checkpoint);return R"({"Ok":false,"Error":"InvalidNativeRequest"})";}
}
}
