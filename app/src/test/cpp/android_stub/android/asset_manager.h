#pragma once
// Host stand-in for the NDK header: assets are files under the directories given to the
// AAssetManager (see asset_manager_stub.cpp).

#include <sys/types.h>

#include <cstddef>
#include <string>
#include <vector>

#define AASSET_MODE_BUFFER 3

struct AAssetManager {
    std::vector<std::string> roots;
};
struct AAsset;
struct AAssetDir;

AAsset *AAssetManager_open(AAssetManager *manager, const char *path, int mode);
off_t AAsset_getLength(AAsset *asset);
int AAsset_read(AAsset *asset, void *buffer, size_t count);
void AAsset_close(AAsset *asset);
AAssetDir *AAssetManager_openDir(AAssetManager *manager, const char *path);
const char *AAssetDir_getNextFileName(AAssetDir *dir);
void AAssetDir_close(AAssetDir *dir);
