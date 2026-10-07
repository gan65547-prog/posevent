#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool loadRknnModel(const char* path, std::vector<uint8_t>& data,
                  std::string& error);
