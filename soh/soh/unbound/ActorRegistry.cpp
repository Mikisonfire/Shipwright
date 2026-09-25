// SOH [Unbound] unbound/actors.json -> DeclaredActorType -> ActorDB. The rules each reader enforces are the
// "Proposed SPEC text" of unbound-docs/actors.md.
#include "ActorRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "DeclaredActor.h"
#include "soh/ActorDB.h"
#include "soh/unbound/UnboundJson.h"
#include "soh/unbound/UnboundSchema.h"

namespace SOH::Unbound {
namespace {

namespace K = Schema;

// Registered types in id order: sTypes[i] is actor id kCustomActorIdBase + i. Filled once at startup, before any
// actor spawns, so the driver may keep pointers into it.
std::vector<DeclaredActorType> sTypes;

constexpr const char* kOtrPrefix = "__OTR__";
constexpr int64_t kSegmentMin = 8;
constexpr int64_t kSegmentMax = 12; // 13 holds flex-skeleton matrices
constexpr int64_t kMessageMax = 0xFFFE;

std::string OtrPath(const std::string& path) {
    return path.empty() ? path : kOtrPrefix + path;
}

s16 ClampS16(int64_t value) {
    return (s16)std::clamp<int64_t>(value, INT16_MIN, INT16_MAX);
}

// A number the entry may omit: false when absent or unreadable (SPEC.md §2 treats a wrong type as missing).
bool OptionalNumber(const Json& obj, const char* key, f32& out) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    double value = ToNumber(*it, NAN);
    if (std::isnan(value)) {
        return false;
    }
    out = (f32)value;
    return true;
}

// The key is the type's name: not a number (a scene's `id` would read it as one) and not already an actor's name.
bool CheckName(const std::string& key) {
    int64_t number = 0;
    if (key.empty() || ParseIntString(key, number)) {
        SPDLOG_ERROR("[Unbound] {}: '{}' is not a valid actor type name", K::kActorRegistryPath, key);
        return false;
    }
    if (ActorDB::Instance->RetrieveId(key) >= 0) {
        SPDLOG_ERROR("[Unbound] {}: '{}' already names an actor", K::kActorRegistryPath, key);
        return false;
    }
    return true;
}

// Keys this version does not define reject the entry, so a build that predates a later key (base, params, script)
// skips the type instead of spawning it without the behavior.
bool CheckKeys(const std::string& key, const Json& def) {
    static const char* const kKnown[] = { K::kName, K::kModel, K::kCollision, K::kTalk, K::kLook };
    for (const auto& [field, value] : def.items()) {
        if (field.starts_with("$") || std::find(std::begin(kKnown), std::end(kKnown), field) != std::end(kKnown)) {
            continue;
        }
        SPDLOG_ERROR("[Unbound] actor type '{}': \"{}\" is not a key this build knows", key, field);
        return false;
    }
    return true;
}

void ReadSegments(const std::string& key, const Json& segments, DeclaredActorType& type) {
    for (const auto& [segKey, value] : segments.items()) {
        int64_t segment = 0;
        if (!ParseIntString(segKey, segment) || segment < kSegmentMin || segment > kSegmentMax || !value.is_string()) {
            SPDLOG_ERROR("[Unbound] actor type '{}': segment \"{}\" ignored (a segment 8-12 naming a texture path)",
                         key, segKey);
            continue;
        }
        type.segments.emplace_back((u8)segment, OtrPath(value.get<std::string>()));
    }
}

bool ReadModel(const std::string& key, const Json& def, DeclaredActorType& type) {
    auto it = def.find(K::kModel);
    if (it == def.end() || !it->is_object()) {
        SPDLOG_ERROR("[Unbound] actor type '{}' has no \"{}\"", key, K::kModel);
        return false;
    }
    const Json& model = *it;
    type.skeleton = OtrPath(PathField(model, K::kSkeleton));
    type.displayList = OtrPath(PathField(model, K::kDisplayList));
    if (type.skeleton.empty() == type.displayList.empty()) {
        SPDLOG_ERROR("[Unbound] actor type '{}': \"{}\" needs exactly one of \"{}\" or \"{}\"", key, K::kModel,
                     K::kSkeleton, K::kDisplayList);
        return false;
    }
    if (!type.skeleton.empty()) {
        type.animation = OtrPath(PathField(model, K::kAnimation));
    }
    type.holdFrame = OptionalNumber(model, K::kFrame, type.frame);
    type.speed = (f32)NumberField(model, K::kSpeed, 1.0);
    type.translucent = Field(model, K::kTranslucent) != 0;
    type.scale = (f32)NumberField(model, K::kScale, 0.01);
    type.yOffset = (f32)NumberField(model, K::kYOffset);
    type.shadow = (f32)NumberField(model, K::kShadow);
    ReadSegments(key, Sub(model, K::kSegments), type);
    return true;
}

void ReadCollision(const Json& def, DeclaredActorType& type) {
    const Json& collision = Sub(def, K::kCollision);
    type.radius = ClampS16(Field(collision, K::kRadius));
    type.height = ClampS16(Field(collision, K::kHeight));
    type.yShift = ClampS16(Field(collision, K::kYShift));
}

// Reads after ReadCollision: the default range depends on the radius.
void ReadTalk(const std::string& key, const Json& def, DeclaredActorType& type) {
    auto it = def.find(K::kTalk);
    if (it == def.end() || !it->is_object()) {
        return;
    }
    type.talks = true;
    int64_t message = Field(*it, K::kMessage);
    if (message < 0 || message > kMessageMax) {
        SPDLOG_ERROR("[Unbound] actor type '{}': message {} is not a message id; placements must set params", key,
                     message);
        message = 0;
    }
    type.message = (u16)message;
    type.range = (f32)NumberField(*it, K::kRange, 50.0 + std::max<s16>(type.radius, 0));
}

bool ReadLook(const std::string& key, const Json& def, DeclaredActorType& type) {
    auto it = def.find(K::kLook);
    if (it == def.end() || !it->is_object()) {
        return true;
    }
    if (type.skeleton.empty()) {
        SPDLOG_ERROR("[Unbound] actor type '{}': \"{}\" needs a \"{}\"", key, K::kLook, K::kSkeleton);
        return false;
    }
    if (!it->contains(K::kLimb)) {
        SPDLOG_ERROR("[Unbound] actor type '{}': \"{}\" has no \"{}\"; the head will not turn", key, K::kLook,
                     K::kLimb);
        return true;
    }
    type.looks = true;
    type.limb = (s32)Field(*it, K::kLimb);
    type.pivot = (f32)NumberField(*it, K::kPivot);
    type.lookRange = (f32)NumberField(*it, K::kRange, 200.0);
    return true;
}

bool ReadType(const std::string& key, const Json& def, DeclaredActorType& type) {
    if (!CheckName(key) || !CheckKeys(key, def) || !ReadModel(key, def, type)) {
        return false;
    }
    ReadCollision(def, type);
    ReadTalk(key, def, type);
    if (!ReadLook(key, def, type)) {
        return false;
    }
    type.name = key;
    type.displayName = def.contains(K::kName) && def[K::kName].is_string() ? def[K::kName].get<std::string>() : key;
    return true;
}

bool RegisterType(const std::string& key, const Json& def) {
    DeclaredActorType type;
    if (!ReadType(key, def, type)) {
        return false;
    }
    int32_t id = kCustomActorIdBase + (int32_t)sTypes.size();
    if (id > INT16_MAX) {
        SPDLOG_ERROR("[Unbound] {}: '{}' does not fit; actor ids are 16-bit", K::kActorRegistryPath, key);
        return false;
    }
    if (ActorDB::Instance->RetrieveEntry(id).entry.valid) {
        SPDLOG_ERROR("[Unbound] {}: '{}' cannot take id {:#x}, which another actor already uses", K::kActorRegistryPath,
                     key, id);
        return false;
    }
    sTypes.push_back(std::move(type));
    ActorDB::Instance->AddEntry(DeclaredActor_DBInit(sTypes.back()), id);
    SPDLOG_INFO("[Unbound] actor type '{}' -> id {:#x}", key, id);
    return true;
}

} // namespace

void LoadCustomActors() {
    Json registry = LoadMergedJson(K::kActorRegistryPath);
    if (!registry.is_object()) {
        return;
    }
    size_t loaded = 0;
    for (const auto& key : ListKeys(registry)) {
        try {
            if (registry[key].is_object() && RegisterType(key, registry[key])) {
                loaded++;
            }
        } catch (const nlohmann::json::exception& e) {
            SPDLOG_ERROR("[Unbound] {}: actor type '{}': {}", K::kActorRegistryPath, key, e.what());
        }
    }
    SPDLOG_INFO("[Unbound] {}: registered {} custom actor type(s)", K::kActorRegistryPath, loaded);
}

const DeclaredActorType* GetDeclaredActorType(int32_t actorId) {
    int32_t index = actorId - kCustomActorIdBase;
    return index >= 0 && index < (int32_t)sTypes.size() ? &sTypes[index] : nullptr;
}

} // namespace SOH::Unbound
