#pragma once
#include "LuiNativeServices.h"
#include <memory>
#include <string>
#include <vector>

namespace Lui::Native {
struct Domain;
std::shared_ptr<Domain> GetDomain(LuiNativeDomainV2* Domain);
std::shared_ptr<Domain> GetDomain();
LuiNativeDomainV2* WrapDomain(const std::shared_ptr<Domain>& Domain);
constexpr uint32_t SessionLimit = 4;
uint32_t Take(const std::shared_ptr<Domain>& Domain, LuiNativeSessionV2* const* Sessions,
    uint32_t Count, uint32_t* Selected, LuiNativeDeliveryV2* Delivery);
bool RequestRunning(LuiNativeSessionV2* Session, LuiNativeIdV2 Request);
void SetContext(LuiNativeSessionV2* Session, void* Context);
void SetImage(LuiNativeSessionV2* Session, std::shared_ptr<void> Image);
const std::string& Name(LuiNativeSessionV2* Session);
const std::vector<LuiNativeMethodV2>& Methods(LuiNativeSessionV2* Session);
uint32_t HandleKind(LuiNativeSessionV2* Session, LuiNativeIdV2 Handle);
bool HandleOpen(LuiNativeSessionV2* Session, LuiNativeIdV2 Handle);
uint32_t KindCount(LuiNativeSessionV2* Session);
void Pump(const std::shared_ptr<Domain>& Domain) noexcept;
}
