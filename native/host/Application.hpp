#pragma once

#include "../abi/LuiRuntime.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace lui {

namespace detail {
template<typename Type> using Clean = std::remove_cv_t<std::remove_reference_t<Type>>;

template<typename Type> struct Signature;
template<typename Result, typename... Arguments>
struct Signature<Result(*)(Arguments...)> {
    using Return = Result;
    using Args = std::tuple<Arguments...>;
    static constexpr size_t Count = sizeof...(Arguments);
};
template<typename Owner, typename Result, typename... Arguments>
struct Signature<Result(Owner::*)(Arguments...) const> : Signature<Result(*)(Arguments...)> {};
template<typename Owner, typename Result, typename... Arguments>
struct Signature<Result(Owner::*)(Arguments...)> : Signature<Result(*)(Arguments...)> {};
template<typename Callable> struct Signature : Signature<decltype(&Callable::operator())> {};

template<typename Type> const char* TypeName() {
    using Value = Clean<Type>;
    if constexpr (std::is_same_v<Value, void>) return "()";
    else if constexpr (std::is_same_v<Value, bool>) return "boolean";
    else if constexpr (std::is_arithmetic_v<Value>) return "number";
    else if constexpr (std::is_same_v<Value, std::string>) return "string";
    else { static_assert(!sizeof(Value), "unsupported LUI host method type"); }
}

template<typename Type> Clean<Type> Decode(const LuiValueV1& Value) {
    using Target = Clean<Type>;
    if constexpr (std::is_same_v<Target, bool>) {
        if (Value.Type != LUI_VALUE_BOOLEAN) throw std::invalid_argument("expected boolean");
        return Value.Boolean != 0;
    } else if constexpr (std::is_arithmetic_v<Target>) {
        if (Value.Type != LUI_VALUE_NUMBER || !std::isfinite(Value.Number))
            throw std::invalid_argument("expected finite number");
        if constexpr (std::is_integral_v<Target>) {
            if (std::floor(Value.Number) != Value.Number ||
                std::abs(Value.Number) > 9007199254740991.0 ||
                Value.Number < static_cast<double>(std::numeric_limits<Target>::lowest()) ||
                Value.Number > static_cast<double>(std::numeric_limits<Target>::max()))
                throw std::out_of_range("integer argument is outside native range");
        }
        return static_cast<Target>(Value.Number);
    } else if constexpr (std::is_same_v<Target, std::string>) {
        if (Value.Type != LUI_VALUE_STRING || (!Value.Text && Value.TextLength))
            throw std::invalid_argument("expected string");
        return std::string(Value.Text ? Value.Text : "", static_cast<size_t>(Value.TextLength));
    } else { static_assert(!sizeof(Target), "unsupported LUI host method argument"); }
}

struct Binding {
    LuiRuntime* Runtime;
    explicit Binding(LuiRuntime* RuntimeValue) : Runtime(RuntimeValue) {}
    virtual ~Binding() = default;
    virtual void Call(const LuiValueV1* Arguments, uint32_t Count, LuiValueV1* Result) = 0;
    static int LUI_EXTENSION_CALL Invoke(void* Context, const LuiValueV1* Arguments,
        uint32_t Count, LuiValueV1* Result) {
        auto* Value = static_cast<Binding*>(Context);
        try { Value->Call(Arguments, Count, Result); return 1; }
        catch (const std::exception& Error) { Lui_SetHostError(Value->Runtime, Error.what()); return 0; }
        catch (...) { Lui_SetHostError(Value->Runtime, "host method threw an unknown exception"); return 0; }
    }
};

template<typename Callable> struct FunctionBinding final : Binding {
    using Shape = Signature<Callable>;
    Callable Function;
    std::string ResultString;
    FunctionBinding(LuiRuntime* RuntimeValue, Callable FunctionValue)
        : Binding(RuntimeValue), Function(std::move(FunctionValue)) {}

    template<size_t... Index>
    void Apply(const LuiValueV1* Arguments, LuiValueV1* Result, std::index_sequence<Index...>) {
        if constexpr (std::is_void_v<typename Shape::Return>) {
            std::invoke(Function, Decode<std::tuple_element_t<Index, typename Shape::Args>>(Arguments[Index])...);
            Result->Type = LUI_VALUE_NIL;
        } else {
            auto Value = std::invoke(Function,
                Decode<std::tuple_element_t<Index, typename Shape::Args>>(Arguments[Index])...);
            using Return = Clean<typename Shape::Return>;
            if constexpr (std::is_same_v<Return, bool>) {
                Result->Type = LUI_VALUE_BOOLEAN;
                Result->Boolean = Value ? 1 : 0;
            } else if constexpr (std::is_arithmetic_v<Return>) {
                Result->Type = LUI_VALUE_NUMBER;
                Result->Number = static_cast<double>(Value);
                if constexpr (std::is_integral_v<Return>) {
                    if (static_cast<long double>(Value) > 9007199254740991.0L ||
                        static_cast<long double>(Value) < -9007199254740991.0L)
                        throw std::out_of_range("integer return exceeds exact Luau number range");
                }
                if (!std::isfinite(Result->Number)) throw std::runtime_error("host returned a non-finite number");
            } else if constexpr (std::is_same_v<Return, std::string>) {
                ResultString = std::move(Value);
                Result->Type = LUI_VALUE_STRING;
                Result->Text = ResultString.c_str();
                Result->TextLength = ResultString.size();
            } else { static_assert(!sizeof(Return), "unsupported LUI host method return type"); }
        }
    }
    void Call(const LuiValueV1* Arguments, uint32_t Count, LuiValueV1* Result) override {
        if (Count != Shape::Count) throw std::invalid_argument("wrong number of host method arguments");
        Result->StructSize = sizeof(*Result);
        Apply(Arguments, Result, std::make_index_sequence<Shape::Count>{});
    }
};

template<typename Shape, size_t... Index>
std::string MethodType(const std::string& ServiceName, std::index_sequence<Index...>) {
    std::string Type = "(Self: " + ServiceName;
    ((Type += ", Arg" + std::to_string(Index + 1) + ": " +
        TypeName<std::tuple_element_t<Index, typename Shape::Args>>()), ...);
    return Type + ") -> " + TypeName<typename Shape::Return>();
}
} // namespace detail

class Application {
public:
    Application() : Runtime(Lui_Create()) {
        if (!Runtime) throw std::runtime_error("[LUI:Host] runtime creation failed");
    }
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application() { Lui_Destroy(Runtime); }

    class ServiceBuilder {
    public:
        ServiceBuilder(Application& ParentValue, std::string NameValue)
            : Parent(ParentValue), Name(std::move(NameValue)) {}

        template<typename Callable>
        ServiceBuilder& Function(const std::string& MethodName, Callable Method) {
            using Shape = detail::Signature<Callable>;
            auto Binding = std::make_unique<detail::FunctionBinding<Callable>>(Parent.Runtime, std::move(Method));
            const std::string Type = detail::MethodType<Shape>(Name,
                std::make_index_sequence<Shape::Count>{});
            if (!Lui_RegisterHostMethod(Parent.Runtime, Name.c_str(), MethodName.c_str(),
                Type.c_str(), &detail::Binding::Invoke, Binding.get()))
                throw std::runtime_error(Lui_GetLastError(Parent.Runtime));
            Parent.Bindings.push_back(std::move(Binding));
            return *this;
        }
        template<typename Value = void>
        ServiceBuilder& Signal(const std::string& SignalName) {
            const std::string Type = std::is_void_v<Value> ? "Signal" :
                "Signal<" + std::string(detail::TypeName<Value>()) + ">";
            if (!Lui_RegisterHostSignal(Parent.Runtime, Name.c_str(), SignalName.c_str(), Type.c_str()))
                throw std::runtime_error(Lui_GetLastError(Parent.Runtime));
            return *this;
        }
    private:
        Application& Parent;
        std::string Name;
    };

    ServiceBuilder Service(const std::string& Name) { return ServiceBuilder(*this, Name); }
    void RunSource(const std::string& Source, const std::string& ChunkName = "LUI") {
        if (!Lui_RunScript(Runtime, Source.c_str(), ChunkName.c_str()))
            throw std::runtime_error(Lui_GetLastError(Runtime));
    }
    void RunFile(const std::string& Path) {
        std::ifstream Input(Path, std::ios::binary);
        if (!Input) throw std::runtime_error("[LUI:Host] could not read " + Path);
        RunSource(std::string(std::istreambuf_iterator<char>(Input), {}), Path);
    }
    int Pump() { return Lui_Pump(Runtime); }
    void Emit(const std::string& ServiceName, const std::string& SignalName) {
        if (!Lui_EmitHostSignal(Runtime, ServiceName.c_str(), SignalName.c_str(), nullptr, 0))
            throw std::runtime_error("[LUI:Host] signal emission failed");
    }
    template<typename Value>
    void Emit(const std::string& ServiceName, const std::string& SignalName, Value Input) {
        using Type = detail::Clean<Value>;
        LuiValueV1 Argument{};
        Argument.StructSize = sizeof(Argument);
        std::string Text;
        if constexpr (std::is_same_v<Type, bool>) {
            Argument.Type = LUI_VALUE_BOOLEAN;
            Argument.Boolean = Input ? 1 : 0;
        } else if constexpr (std::is_arithmetic_v<Type>) {
            Argument.Type = LUI_VALUE_NUMBER;
            Argument.Number = static_cast<double>(Input);
        } else if constexpr (std::is_same_v<Type, std::string>) {
            Text = std::move(Input);
            Argument.Type = LUI_VALUE_STRING;
            Argument.Text = Text.c_str();
            Argument.TextLength = Text.size();
        } else { static_assert(!sizeof(Type), "unsupported LUI host signal type"); }
        if (!Lui_EmitHostSignal(Runtime, ServiceName.c_str(), SignalName.c_str(), &Argument, 1))
            throw std::runtime_error("[LUI:Host] signal emission failed");
    }
    LuiRuntime* NativeRuntime() const { return Runtime; }

private:
    LuiRuntime* Runtime;
    std::vector<std::unique_ptr<detail::Binding>> Bindings;
};
} // namespace lui
