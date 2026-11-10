#include "../mobile/Host.h"
#include <jni.h>
#include <memory>
#include <mutex>
#include <limits>

namespace {
struct Registry {
    std::mutex Mutex;
    std::unique_ptr<Lui::Mobile::Host> Current;
    int Token = 0;
};
// Bounded process lifetime registry. An uncooperative owner must retain its
// callback storage until process death; no static destructor on a foreign thread.
Registry& GetRegistry() { static auto* Value = new Registry; return *Value; }
template<class Function> bool Call(jint Token, Function Operation) noexcept {
    try {
        auto& State = GetRegistry();
        std::lock_guard<std::mutex> Lock(State.Mutex);
        if (!State.Current || Token != State.Token || !State.Current->IsOwner()) return false;
        try {
            const auto Begin = std::chrono::steady_clock::now();
            const bool Result = Operation(*State.Current);
            State.Current->RecordOwner(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count());
            return Result;
        }
        catch (...) { State.Current->Retire(); return false; }
    } catch (...) { return false; }
}
}
extern "C" JNIEXPORT jint JNICALL Java_dev_lui_feasibility_NativeBridge_Start(JNIEnv* Env, jobject, jbyteArray Input, jbyteArray Root) {
    try {
        auto& State = GetRegistry();
        std::lock_guard<std::mutex> Lock(State.Mutex);
        if (!Input || State.Current || State.Token == std::numeric_limits<int>::max()) return 0;
        const auto Size = Env->GetArrayLength(Input);
        if (Size <= 0 || Size > 10 * 1024 * 1024) return 0;
        std::string Bytes(static_cast<size_t>(Size), '\0');
        Env->GetByteArrayRegion(Input, 0, Size, reinterpret_cast<jbyte*>(Bytes.data()));
        if (Env->ExceptionCheck()) return 0;
        if (!Root) return 0;
        const auto RootSize = Env->GetArrayLength(Root);
        if (RootSize <= 0 || RootSize > 4096) return 0;
        std::string StorageRoot(static_cast<size_t>(RootSize), '\0');
        Env->GetByteArrayRegion(Root, 0, RootSize, reinterpret_cast<jbyte*>(StorageRoot.data()));
        if (Env->ExceptionCheck()) return 0;
        auto Host = std::make_unique<Lui::Mobile::Host>(++State.Token);
        const auto Begin = std::chrono::steady_clock::now();
        Host->Start(Bytes, StorageRoot);
        Host->RecordOwner(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count());
        State.Current = std::move(Host);
        // A failed entry still has a retirement token. Session's first attach
        // refuses it and queues normal owner retirement; never drop a live worker.
        return State.Token;
    } catch (...) { return 0; }
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Attach(JNIEnv*, jobject, jint Token, jint Epoch, jdouble W, jdouble H) {
    return Call(Token, [&](auto& Host) { return Epoch > 0 && Host.Attach(Epoch, W, H); });
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Detach(JNIEnv*, jobject, jint Token, jint Epoch) {
    return Call(Token, [&](auto& Host) { return Epoch > 0 && Host.Detach(Epoch); });
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Acknowledge(JNIEnv*, jobject, jint Token, jint Epoch, jint Revision, jboolean Success) {
    return Call(Token, [&](auto& Host) { return Epoch > 0 && Revision > 0 && Host.Acknowledge(Epoch, Revision, Success); });
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Activate(JNIEnv*, jobject, jint Token, jint Epoch, jint Revision, jint Sequence, jint Id) {
    return Call(Token, [&](auto& Host) { return Epoch > 0 && Revision > 0 && Sequence > 0 && Host.Activate(Epoch, Revision, Sequence, Id); });
}
extern "C" JNIEXPORT jint JNICALL Java_dev_lui_feasibility_NativeBridge_Event(JNIEnv* Env, jobject, jint Token, jint Epoch, jint Revision, jint Sequence, jint Id, jbyteArray Input) {
    int Result = -1;
    Call(Token, [&](auto& Host) {
        if (!Input || Epoch <= 0 || Revision < 0 || Sequence < 0) return false;
        const auto Size = Env->GetArrayLength(Input);
        if (Size <= 0 || Size > 128 * 1024) return false;
        std::string Bytes(static_cast<size_t>(Size), '\0');
        Env->GetByteArrayRegion(Input, 0, Size, reinterpret_cast<jbyte*>(Bytes.data()));
        if (Env->ExceptionCheck()) return false;
        Result = Host.Event(Epoch, Revision, Sequence, Id, Bytes); return Result >= 0;
    });
    return Result;
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Pump(JNIEnv*, jobject, jint Token) {
    return Call(Token, [&](auto& Host) { return Host.Pump(); });
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Theme(JNIEnv*, jobject, jint Token, jint Mode) {
    return Call(Token, [&](auto& Host) {
        static const char* Themes[]{"Light", "Dark", "HighContrast"};
        return Mode >= 0 && Mode <= 2 && Host.Theme(Themes[Mode]);
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_lui_feasibility_NativeBridge_Asset(JNIEnv* Env, jobject, jint Token, jbyteArray Input) {
    jbyteArray Result = nullptr;
    Call(Token, [&](auto& Host) {
        if (!Input) return false;
        const auto Size = Env->GetArrayLength(Input);
        if (Size <= 0 || Size > 4096) return false;
        std::string Path(static_cast<size_t>(Size), '\0');
        Env->GetByteArrayRegion(Input, 0, Size, reinterpret_cast<jbyte*>(Path.data()));
        if (Env->ExceptionCheck()) return false;
        const auto Bytes = Host.Asset(Path);
        if (Bytes.empty() || Bytes.size() > 1024 * 1024) return false;
        Result = Env->NewByteArray(static_cast<jsize>(Bytes.size()));
        if (!Result) return false;
        Env->SetByteArrayRegion(Result, 0, static_cast<jsize>(Bytes.size()), reinterpret_cast<const jbyte*>(Bytes.data()));
        return !Env->ExceptionCheck();
    });
    return Result;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_lui_feasibility_NativeBridge_Take(JNIEnv* Env, jobject, jint Token) {
    jbyteArray Result = nullptr;
    const bool Success = Call(Token, [&](auto& Host) {
        const auto Bytes = Host.Take();
        if (Bytes.empty()) return true;
        Result = Env->NewByteArray(static_cast<jsize>(Bytes.size()));
        if (!Result) return false;
        Env->SetByteArrayRegion(Result, 0, static_cast<jsize>(Bytes.size()), reinterpret_cast<const jbyte*>(Bytes.data()));
        return !Env->ExceptionCheck();
    });
    if (!Success && !Env->ExceptionCheck()) {
        const auto Class = Env->FindClass("java/lang/IllegalStateException");
        if (Class) Env->ThrowNew(Class, "[LUI:Mobile] Failed");
    }
    return Result;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_lui_feasibility_NativeBridge_Metrics(JNIEnv* Env, jobject, jint Token) {
    jbyteArray Result = nullptr;
    Call(Token, [&](auto& Host) {
        const auto Bytes = Host.Metrics();
        if (Bytes.empty() || Bytes.size() > 16384) return false;
        Result = Env->NewByteArray(static_cast<jsize>(Bytes.size()));
        if (!Result) return false;
        Env->SetByteArrayRegion(Result, 0, static_cast<jsize>(Bytes.size()), reinterpret_cast<const jbyte*>(Bytes.data()));
        return !Env->ExceptionCheck();
    });
    return Result;
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_lui_feasibility_NativeBridge_Close(JNIEnv*, jobject, jint Token) {
    try {
        auto& State = GetRegistry();
        std::lock_guard<std::mutex> Lock(State.Mutex);
        if (!State.Current || Token != State.Token || !State.Current->IsOwner()) return false;
        if (!State.Current->Retire()) return false;
        State.Current.reset(); return true;
    } catch (...) { return false; }
}
