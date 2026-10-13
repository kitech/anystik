#pragma once
#include <cstdint>
#include <string>
#include <vector>

void initCacheFsDirs(const char* dataDir);
std::string makeCacheFsPath(const char* key);
bool writeCacheFile(const std::string& fullPath, const void* data, size_t size);
std::vector<uint8_t> readCacheFile(const std::string& fullPath);
bool removeCacheFile(const std::string& fullPath);
