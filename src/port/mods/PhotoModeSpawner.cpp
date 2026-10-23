#include "PhotoModeSpawner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "PhotoMode.h"
#include "behavior_data.h"
#include "engine/geo_layout.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "game/area.h"
#include "game/memory.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "sm64.h"
#include "object_constants.h"
#include "model_ids.h"

// These headers declare their functions without extern_s.
extern "C" {
#include "engine/behavior_script.h"
#include "engine/surface_collision.h"
#include "game/spawn_object.h"
}

#include "assets/actors/bobomb/geo.h"
#include "assets/actors/bully/geo.h"
#include "assets/actors/butterfly/geo.h"
#include "assets/actors/flyguy/geo.h"
#include "assets/actors/goomba/geo.h"
#include "assets/actors/penguin/geo.h"
#include "assets/actors/piranha_plant/geo.h"
#include "assets/actors/scuttlebug/geo.h"
#include "assets/actors/snufit/geo.h"
#include "assets/actors/spindrift/geo.h"
#include "assets/actors/swoop/geo.h"
#include "assets/actors/tree/geo.h"

namespace {

constexpr int kRuntimeModelPoolSize = 128 * 1024;
constexpr int kReservedObjectSlots = 32;
constexpr float kPi = 3.14159265358979323846f;

enum class SpawnHeight {
    Ground,
    Air,
    Water,
};

struct SpawnOption {
    const char* name;
    const BehaviorScript* behavior;
    const char* geo;
    u8 defaultParams2ndByte;
    PhotoModeCategory category;
    SpawnHeight height;
    // Model ID the game's own code uses for this actor. Runtime models go here when the slot is
    // empty; options that respawn by this ID (Bob-omb) cannot use any other slot.
    s32 canonicalModel;
    bool needsCanonicalSlot;
};

const std::array<SpawnOption, 13> sOptions = {{
    { "Goomba", bhvGoomba, goomba_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Ground, MODEL_GOOMBA, false },
    { "Bob-omb", bhvBobomb, black_bobomb_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Ground, MODEL_BLACK_BOBOMB, true },
    { "Small Bully", bhvSmallBully, bully_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Ground, MODEL_BULLY, false },
    { "Spindrift", bhvSpindrift, spindrift_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Ground, MODEL_SPINDRIFT, false },
    { "Scuttlebug", bhvScuttlebug, scuttlebug_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Ground, MODEL_SCUTTLEBUG, false },
    { "Fly Guy", bhvFlyGuy, flyguy_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Air, MODEL_FLYGUY, false },
    { "Snufit", bhvSnufit, snufit_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Ground, MODEL_SNUFIT, false },
    { "Swoop", bhvSwoop, swoop_geo, 0, PhotoModeCategory::Enemies, SpawnHeight::Air, MODEL_SWOOP, false },
    { "Piranha Plant", bhvPiranhaPlant, piranha_plant_geo, 0, PhotoModeCategory::Enemies,
      SpawnHeight::Ground, MODEL_PIRANHA_PLANT, false },
    { "Small Penguin", bhvSmallPenguin, penguin_geo, 0, PhotoModeCategory::Wildlife, SpawnHeight::Ground, MODEL_PENGUIN, false },
    { "Butterfly", bhvButterfly, butterfly_geo, 0, PhotoModeCategory::Wildlife, SpawnHeight::Air, MODEL_BUTTERFLY, false },
    { "Fish School", bhvFishSpawner, nullptr, FISH_SPAWNER_BP_FEW_BLUE, PhotoModeCategory::Wildlife,
      SpawnHeight::Water, MODEL_NONE, false },
    { "Bubbly Tree", bhvTree, bubbly_tree_geo, 0, PhotoModeCategory::Scenery,
      SpawnHeight::Ground, MODEL_BOB_BUBBLY_TREE, false },
}};

struct RuntimeModel {
    std::string geo;
    GraphNode* root = nullptr;
    s32 slot = -1;
};

struct SpawnedObject {
    Object* object;
    const BehaviorScript* behavior;
    u32 uid;
};

enum class PendingAction {
    None,
    Spawn,
    Multiply,
    Remove,
};

std::atomic<int> sPendingRequest{ 0 };
std::atomic<int> sSelectedOption{ 0 };
std::array<std::atomic_bool, sOptions.size()> sNativeInLevel{};
std::atomic<int> sActiveObjectCount{ 0 };
std::atomic<int> sSpawnedObjectCount{ 0 };

std::vector<RuntimeModel> sRuntimeModels;
std::vector<SpawnedObject> sSpawnedObjects;
AllocOnlyPool* sRuntimeModelPool = nullptr;
std::mutex sStatusMutex;
std::string sStatus;

void SetStatus(std::string status) {
    std::lock_guard<std::mutex> lock(sStatusMutex);
    sStatus = std::move(status);
}

bool IsActive(const Object* object) {
    return object != nullptr && (object->activeFlags & ACTIVE_FLAG_ACTIVE) != 0;
}

int CountActiveObjects() {
    int count = 0;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        count += IsActive(&gObjectPool[i]) ? 1 : 0;
    }
    return count;
}

int FindNativeModel(const SpawnOption& option) {
    if (gLoadedGraphNodes == nullptr) {
        return -1;
    }
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        const Object* object = &gObjectPool[i];
        if (!IsActive(object) || object->behavior != segmented_to_virtual(option.behavior)) {
            continue;
        }
        if (option.geo == nullptr) {
            if (option.behavior == bhvFishSpawner && gLoadedGraphNodes[MODEL_FISH] != nullptr) {
                return object->modelId;
            }
            continue;
        }
        if (object->modelId >= 0 && object->modelId < 256 &&
            object->header.gfx.sharedChild == gLoadedGraphNodes[object->modelId]) {
            return object->modelId;
        }
    }
    return -1;
}

int CountSpawnedObjects() {
    int count = 0;
    for (const SpawnedObject& spawned : sSpawnedObjects) {
        count += IsActive(spawned.object) && spawned.object->behavior == spawned.behavior &&
                         spawned.object->header.gfx.node.uid == spawned.uid
                     ? 1
                     : 0;
    }
    return count;
}

void RefreshUiState() {
    for (std::size_t i = 0; i < sOptions.size(); i++) {
        sNativeInLevel[i].store(FindNativeModel(sOptions[i]) >= 0);
    }
    sActiveObjectCount.store(CountActiveObjects());
    sSpawnedObjectCount.store(CountSpawnedObjects());
}

RuntimeModel* FindRuntimeModel(const char* geo) {
    auto it = std::find_if(sRuntimeModels.begin(), sRuntimeModels.end(),
                           [geo](const RuntimeModel& model) { return model.geo == geo; });
    return it == sRuntimeModels.end() ? nullptr : &*it;
}

GraphNode* LoadRuntimeGeo(const char* geo) {
    if (RuntimeModel* cached = FindRuntimeModel(geo)) {
        return cached->root;
    }
    if (sRuntimeModelPool == nullptr) {
        if (main_pool_available() < (u32)(kRuntimeModelPoolSize + 64 * 1024)) {
            SetStatus("Not enough free game memory to load another model.");
            return nullptr;
        }
        sRuntimeModelPool = alloc_only_pool_init(kRuntimeModelPoolSize, MEMORY_POOL_LEFT);
        if (sRuntimeModelPool == nullptr) {
            SetStatus("Could not allocate memory for cross-level models.");
            return nullptr;
        }
    }

    // Geo parsing uses global traversal state. Preserve the active level's values so this
    // runtime parse cannot disturb the next level/render traversal.
    AllocOnlyPool* oldPool = gGraphNodePool;
    GraphNode* oldRoot = gCurRootGraphNode;
    GraphNode** oldViews = gGeoViews;
    u16 oldViewCount = gGeoNumViews;
    s16 oldGraphIndex = gCurGraphNodeIndex;
    s16 oldStackIndex = gGeoLayoutStackIndex;
    s16 oldReturnIndex = gGeoLayoutReturnIndex;
    u8* oldCommand = gGeoLayoutCommand;
    std::array<uintptr_t, 16> oldStack;
    std::array<GraphNode*, 32> oldNodes;
    std::copy_n(gGeoLayoutStack, oldStack.size(), oldStack.begin());
    std::copy_n(gCurGraphNodeList, oldNodes.size(), oldNodes.begin());

    GraphNode* root = process_geo_layout(sRuntimeModelPool, (void*)geo);

    gGraphNodePool = oldPool;
    gCurRootGraphNode = oldRoot;
    gGeoViews = oldViews;
    gGeoNumViews = oldViewCount;
    gCurGraphNodeIndex = oldGraphIndex;
    gGeoLayoutStackIndex = oldStackIndex;
    gGeoLayoutReturnIndex = oldReturnIndex;
    gGeoLayoutCommand = oldCommand;
    std::copy(oldStack.begin(), oldStack.end(), gGeoLayoutStack);
    std::copy(oldNodes.begin(), oldNodes.end(), gCurGraphNodeList);

    if (root == nullptr) {
        SetStatus(std::string("Could not load model: ") + geo);
        return nullptr;
    }
    sRuntimeModels.push_back({ geo, root, -1 });
    return root;
}

int GetModelSlot(const SpawnOption& option) {
    if (option.geo == nullptr) {
        if (gLoadedGraphNodes == nullptr || gLoadedGraphNodes[MODEL_FISH] == nullptr) {
            SetStatus("This level has no loaded fish model; try a level with native fish.");
            return -1;
        }
        return 0; // The fish spawner is invisible and uses MODEL_NONE.
    }
    int nativeModel = FindNativeModel(option);
    if (nativeModel >= 0) {
        return nativeModel;
    }
    if (gLoadedGraphNodes == nullptr) {
        SetStatus("The level has no loaded model table.");
        return -1;
    }

    const s32 canonical = option.canonicalModel;
    const bool canonicalFree = canonical > 0 && canonical < 256 && gLoadedGraphNodes[canonical] == nullptr;
    RuntimeModel* existing = FindRuntimeModel(option.geo);
    const bool alreadyPlaced = existing != nullptr && existing->slot > 0 &&
                               gLoadedGraphNodes[existing->slot] == existing->root;
    if (option.needsCanonicalSlot && !canonicalFree && !alreadyPlaced) {
        SetStatus(std::string(option.name) + " needs its usual model slot, which this level uses for something else.");
        return -1;
    }

    RuntimeModel* cached = FindRuntimeModel(option.geo);
    GraphNode* graph = cached != nullptr ? cached->root : LoadRuntimeGeo(option.geo);
    if (graph == nullptr) {
        return -1;
    }
    for (int i = 1; i < 256; i++) {
        if (gLoadedGraphNodes[i] == graph) {
            if (cached != nullptr) {
                cached->slot = i;
            } else {
                FindRuntimeModel(option.geo)->slot = i;
            }
            return i;
        }
    }
    if (canonicalFree) {
        gLoadedGraphNodes[canonical] = graph;
        FindRuntimeModel(option.geo)->slot = canonical;
        return canonical;
    }
    for (int i = 255; i > 0; i--) {
        if (gLoadedGraphNodes[i] == nullptr) {
            gLoadedGraphNodes[i] = graph;
            RuntimeModel* model = FindRuntimeModel(option.geo);
            if (model != nullptr) {
                model->slot = i;
            }
            return i;
        }
    }
    SetStatus("No free model slot in this level; cross-level spawning is unavailable here.");
    return -1;
}

bool HasFreeObjectSlots(int wanted) {
    return CountActiveObjects() + wanted <= OBJECT_POOL_CAPACITY - kReservedObjectSlots;
}

s16 ToWorldCoord(float value) {
    return (s16)std::lround(std::clamp(value, -32767.0f, 32767.0f));
}

bool FindSpawnPosition(const SpawnOption& option, const float cameraPos[3], Vec3f out) {
    out[0] = cameraPos[0];
    out[1] = cameraPos[1];
    out[2] = cameraPos[2];
    if (option.height == SpawnHeight::Air) {
        return true;
    }
    if (option.height == SpawnHeight::Water) {
        float waterY = find_water_level(out[0], out[2]);
        if (waterY < FLOOR_LOWER_LIMIT_MISC) {
            SetStatus("No water found below the camera.");
            return false;
        }
        out[1] = waterY - 300.0f;
        return true;
    }
    Surface* floor = nullptr;
    float floorY = find_floor(out[0], out[1] + 100.0f, out[2], &floor);
    if (floor == nullptr) {
        SetStatus("No ground found below the camera.");
        return false;
    }
    out[1] = floorY;
    return true;
}

Object* SpawnObject(const SpawnOption& option, s32 modelId, const float position[3], s16 yaw,
                    const Object* source = nullptr) {
    if (!HasFreeObjectSlots(1)) {
        SetStatus("Object pool nearly full; spawning stopped to leave room for the level.");
        return nullptr;
    }
    if (gMarioObject == nullptr) {
        SetStatus("Cannot spawn until the level has initialized.");
        return nullptr;
    }
    Object* object = spawn_object_abs_with_rot(gMarioObject, 0, modelId, option.behavior, ToWorldCoord(position[0]),
                                               ToWorldCoord(position[1]), ToWorldCoord(position[2]), 0, yaw, 0);
    // Goombas, bullies and scuttlebugs write to their parent (triplet spawner, boss), so never
    // let that be Mario or the source object's spawner.
    object->parentObj = object;
    if (source != nullptr) {
        object->oBehParams = source->oBehParams;
        object->oBehParams2ndByte = source->oBehParams2ndByte;
    } else {
        object->oBehParams2ndByte = option.defaultParams2ndByte;
    }
    if (option.behavior == bhvFishSpawner) {
        object->oBehParams2ndByte = FISH_SPAWNER_BP_FEW_BLUE;
    }
    sSpawnedObjects.push_back(
        { object, static_cast<const BehaviorScript*>(segmented_to_virtual(option.behavior)), object->header.gfx.node.uid });
    return object;
}

void SpawnAtCamera(int index) {
    if (index < 0 || index >= (int)sOptions.size()) {
        SetStatus("Select a creature or object first.");
        return;
    }
    if (!PhotoMode_IsActive()) {
        SetStatus("Enter Photo Mode before spawning.");
        return;
    }
    const SpawnOption& option = sOptions[(size_t)index];
    const bool native = FindNativeModel(option) >= 0;
    PhotoModePose pose = PhotoMode_GetPose();
    Vec3f position;
    if (!FindSpawnPosition(option, pose.pos, position)) {
        return;
    }
    int modelId = GetModelSlot(option);
    if (modelId < 0) {
        return;
    }
    s16 yaw = (s16)std::lround(pose.yaw * (32768.0f / kPi));
    if (SpawnObject(option, modelId, position, yaw) != nullptr) {
        const char* suffix = native || option.geo == nullptr ? "." : " (experimental cross-level model).";
        SetStatus(std::string("Spawned ") + option.name + suffix);
    }
}

const SpawnOption* FindOption(const Object* object) {
    for (const SpawnOption& option : sOptions) {
        if (object->behavior == segmented_to_virtual(option.behavior)) {
            return &option;
        }
    }
    return nullptr;
}

void MultiplyCategory(PhotoModeCategory category) {
    if (!PhotoMode_IsActive()) {
        SetStatus("Enter Photo Mode before changing density.");
        return;
    }
    std::vector<Object*> sources;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        Object* object = &gObjectPool[i];
        if (IsActive(object) && object != gMarioObject && object->behavior != nullptr &&
            PhotoModeFilters_GetCategory(object) == category && FindOption(object) != nullptr) {
            sources.push_back(object);
        }
    }
    if (sources.empty()) {
        SetStatus("No supported fauna or scenery in this category to multiply.");
        return;
    }
    int spawned = 0;
    for (Object* source : sources) {
        if (!HasFreeObjectSlots(1)) {
            break;
        }
        const SpawnOption* option = FindOption(source);
        if (option == nullptr || source->modelId < 0 || source->modelId >= 256) {
            continue;
        }
        if (option->geo != nullptr &&
            (gLoadedGraphNodes == nullptr || gLoadedGraphNodes[source->modelId] == nullptr)) {
            continue;
        }
        if (option->behavior == bhvFishSpawner &&
            (gLoadedGraphNodes == nullptr || gLoadedGraphNodes[MODEL_FISH] == nullptr)) {
            continue;
        }
        float angle = random_float() * 2.0f * kPi;
        float distance = 180.0f + random_float() * 420.0f;
        Vec3f position = { source->oPosX + cosf(angle) * distance, source->oPosY,
                           source->oPosZ + sinf(angle) * distance };
        if (option->height == SpawnHeight::Ground) {
            Surface* floor = nullptr;
            float floorY = find_floor(position[0], position[1] + 300.0f, position[2], &floor);
            if (floor == nullptr || fabsf(floorY - source->oPosY) > 300.0f) {
                continue;
            }
            position[1] = floorY;
        } else if (option->height == SpawnHeight::Water) {
            float waterY = find_water_level(position[0], position[2]);
            if (waterY < FLOOR_LOWER_LIMIT_MISC) {
                continue;
            }
            position[1] = waterY - 300.0f;
        }
        s16 yaw = (s16)random_u16();
        if (SpawnObject(*option, source->modelId, position, yaw, source) != nullptr) {
            spawned++;
        }
    }
    if (spawned > 0) {
        SetStatus("Added " + std::to_string(spawned) + " nearby copies.");
    } else if (CountActiveObjects() >= OBJECT_POOL_CAPACITY - kReservedObjectSlots) {
        SetStatus("Object pool nearly full; no copies were added.");
    } else {
        SetStatus("No safe nearby spawn positions found.");
    }
}

void RemoveSpawned() {
    int removed = 0;
    for (const SpawnedObject& spawned : sSpawnedObjects) {
        if (spawned.object != nullptr && IsActive(spawned.object) && spawned.object->behavior == spawned.behavior &&
            spawned.object->header.gfx.node.uid == spawned.uid) {
            if (spawned.behavior == segmented_to_virtual(bhvFishSpawner)) {
                // Delete the school now; the spawner's slot may be reused before the fish check it.
                const void* fish = segmented_to_virtual(bhvFish);
                for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
                    Object* child = &gObjectPool[i];
                    if (IsActive(child) && child->behavior == fish && child->parentObj == spawned.object) {
                        mark_obj_for_deletion(child);
                    }
                }
            }
            mark_obj_for_deletion(spawned.object);
            removed++;
        }
    }
    sSpawnedObjects.clear();
    SetStatus("Removed " + std::to_string(removed) + " Photo Mode spawns.");
}

} // namespace

std::size_t PhotoModeSpawner_GetOptionCount() {
    return sOptions.size();
}

const char* PhotoModeSpawner_GetOptionName(std::size_t index) {
    return index < sOptions.size() ? sOptions[index].name : "";
}

bool PhotoModeSpawner_IsNativeInLevel(std::size_t index) {
    return index < sNativeInLevel.size() && sNativeInLevel[index].load();
}

bool PhotoModeSpawner_RequiresWater(std::size_t index) {
    return index < sOptions.size() && sOptions[index].height == SpawnHeight::Water;
}

int PhotoModeSpawner_GetSelectedOption() {
    return sSelectedOption.load();
}

void PhotoModeSpawner_SetSelectedOption(int index) {
    sSelectedOption.store(std::clamp(index, 0, (int)sOptions.size() - 1));
}

void PhotoModeSpawner_QueueSpawn(int index) {
    sPendingRequest.store(((int)PendingAction::Spawn << 16) | (index & 0xFFFF));
}

void PhotoModeSpawner_QueueSpawnSelected() {
    PhotoModeSpawner_QueueSpawn(PhotoModeSpawner_GetSelectedOption());
}

void PhotoModeSpawner_QueueMultiply(PhotoModeCategory category) {
    sPendingRequest.store(((int)PendingAction::Multiply << 16) | (int)category);
}

void PhotoModeSpawner_QueueRemove() {
    sPendingRequest.store((int)PendingAction::Remove << 16);
}

void PhotoModeSpawner_ProcessPending() {
    int request = sPendingRequest.exchange(0);
    PendingAction action = (PendingAction)(request >> 16);
    int argument = request & 0xFFFF;
    switch (action) {
        case PendingAction::Spawn:
            SpawnAtCamera(argument);
            break;
        case PendingAction::Multiply:
            MultiplyCategory((PhotoModeCategory)argument);
            break;
        case PendingAction::Remove:
            RemoveSpawned();
            break;
        case PendingAction::None:
            break;
    }
    RefreshUiState();
}

void PhotoModeSpawner_OnLevelChanged() {
    if (gLoadedGraphNodes != nullptr) {
        for (const RuntimeModel& model : sRuntimeModels) {
            if (model.slot > 0 && model.slot < 256 && gLoadedGraphNodes[model.slot] == model.root) {
                gLoadedGraphNodes[model.slot] = nullptr;
            }
        }
    }
    sSpawnedObjects.clear();
    sRuntimeModels.clear();
    sRuntimeModelPool = nullptr; // The level's main-pool state releases the pool during the warp.
    for (auto& native : sNativeInLevel) {
        native.store(false);
    }
    sActiveObjectCount.store(0);
    sSpawnedObjectCount.store(0);
    SetStatus("");
}

int PhotoModeSpawner_GetActiveObjectCount() {
    return sActiveObjectCount.load();
}

int PhotoModeSpawner_GetSpawnedObjectCount() {
    return sSpawnedObjectCount.load();
}

std::string PhotoModeSpawner_GetStatus() {
    std::lock_guard<std::mutex> lock(sStatusMutex);
    return sStatus;
}
