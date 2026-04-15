#include "Engine.h"

#include "EngineScope.h"
#include "Exception.h"
#include "InstancePayload.h"
#include "MetaInfo.h"
#include "Reference.h"
#include "TrackedHandle.h"
#include "Value.h"
#include "jspp/core/Trampoline.h"

#include <cassert>
#include <fstream>
#include <utility>


namespace jspp {

Engine::~Engine() {
    if (isDestroying()) return;
    isDestroying_ = true;

    if (userData_) userData_.reset();

    {
        EngineScope scope(this);

        while (trackedHead_) {
            ITrackedHandle* current = trackedHead_;
            trackedHead_            = current->next_;
            if (trackedHead_) trackedHead_->prev_ = nullptr;
            current->prev_ = current->next_ = nullptr;
            current->onEngineDestroy();
        }

        registeredClasses_.clear();
        registeredEnums_.clear();
    }
    auto tryDispose = []<typename T>(T* t) {
        if constexpr (requires { t->dispose(); }) {
            t->dispose();
        }
    };
    tryDispose(this);
}

void Engine::setData(std::shared_ptr<void> data) { userData_ = std::move(data); }

bool Engine::isDestroying() const { return isDestroying_; }

Local<Value> Engine::evalScript(Local<String> const& code) { return evalScript(code, String::newString("<eval>")); }

// TODO: move to backend
Local<Value> Engine::loadFile(std::filesystem::path const& path) {
    if (isDestroying()) return {};
    if (!std::filesystem::exists(path)) {
        throw Exception("File not found: " + path.string());
    }
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw Exception("Failed to open file: " + path.string());
    }
    std::string code((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return evalScript(String::newString(code), String::newString(path.string())); // todo: replace to evalModule
}

ClassMeta const* Engine::getClassMeta(std::type_index typeId) const {
    auto iter = instanceClassMapping.find(typeId);
    if (iter == instanceClassMapping.end()) return nullptr;
    return iter->second;
}

void Engine::initClassTrampoline(InstancePayload const* payload, Local<Object> thiz) {
    if (auto trampoline = payload->getHolder().get_trampoline()) {
        auto weak           = TrackedWeak<Object>::create(std::move(thiz));
        trampoline->engine_ = payload->engine_;
        trampoline->object_ = std::move(weak);
    }
}

void Engine::addTrackedHandle(ITrackedHandle* handle) {
    if (isDestroying_) return;
    handle->next_ = trackedHead_;
    if (trackedHead_) trackedHead_->prev_ = handle;
    trackedHead_ = handle;
}
void Engine::removeTrackedHandle(ITrackedHandle* handle) {
    if (handle->prev_) handle->prev_->next_ = handle->next_;
    if (handle->next_) handle->next_->prev_ = handle->prev_;
    if (handle == trackedHead_) trackedHead_ = handle->next_;
    handle->prev_ = handle->next_ = nullptr;
}


} // namespace jspp