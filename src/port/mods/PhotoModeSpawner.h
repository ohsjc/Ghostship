#pragma once

#include <cstddef>
#include <string>

#include "PhotoModeFilters.h"

std::size_t PhotoModeSpawner_GetOptionCount();
const char* PhotoModeSpawner_GetOptionName(std::size_t index);
bool PhotoModeSpawner_IsNativeInLevel(std::size_t index);
bool PhotoModeSpawner_RequiresWater(std::size_t index);
int PhotoModeSpawner_GetSelectedOption();
void PhotoModeSpawner_SetSelectedOption(int index);
void PhotoModeSpawner_QueueSpawn(int index);
void PhotoModeSpawner_QueueSpawnSelected();
void PhotoModeSpawner_QueueMultiply(PhotoModeCategory category);
void PhotoModeSpawner_QueueRemove();
void PhotoModeSpawner_ProcessPending();
void PhotoModeSpawner_OnLevelChanged();
int PhotoModeSpawner_GetActiveObjectCount();
int PhotoModeSpawner_GetSpawnedObjectCount();
std::string PhotoModeSpawner_GetStatus();
