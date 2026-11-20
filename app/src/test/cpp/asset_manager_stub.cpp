#include <android/asset_manager.h>

#include <dirent.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

struct AAsset {
    std::string data;
};

struct AAssetDir {
    std::vector<std::string> names;
    size_t next = 0;
};

AAsset *AAssetManager_open(AAssetManager *manager, const char *path, int) {
    for (const std::string &root : manager->roots) {
        std::ifstream in(root + "/" + path, std::ios::binary);
        if (!in.is_open()) continue;
        std::ostringstream data;
        data << in.rdbuf();
        return new AAsset{data.str()};
    }
    return nullptr;
}

off_t AAsset_getLength(AAsset *asset) {
    return static_cast<off_t>(asset->data.size());
}

int AAsset_read(AAsset *asset, void *buffer, size_t count) {
    const size_t size = std::min(count, asset->data.size());
    std::memcpy(buffer, asset->data.data(), size);
    return static_cast<int>(size);
}

void AAsset_close(AAsset *asset) {
    delete asset;
}

AAssetDir *AAssetManager_openDir(AAssetManager *manager, const char *path) {
    auto *result = new AAssetDir;
    for (const std::string &root : manager->roots) {
        const std::string full = path[0] ? root + "/" + path : root;
        if (DIR *dir = opendir(full.c_str())) {
            while (const dirent *entry = readdir(dir)) {
                if (entry->d_type == DT_REG) result->names.emplace_back(entry->d_name);
            }
            closedir(dir);
        }
    }
    return result;
}

const char *AAssetDir_getNextFileName(AAssetDir *dir) {
    return dir->next < dir->names.size() ? dir->names[dir->next++].c_str() : nullptr;
}

void AAssetDir_close(AAssetDir *dir) {
    delete dir;
}
