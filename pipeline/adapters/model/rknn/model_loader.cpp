#include "model_loader.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <dlfcn.h>

// The target SDK may ship libcrypto without OpenSSL development headers.
struct EVP_CIPHER_CTX;
using CtxNew = EVP_CIPHER_CTX* (*)();
using CtxFree = void (*)(EVP_CIPHER_CTX*);
using CipherFn = const void* (*)();
using InitFn = int (*)(EVP_CIPHER_CTX*, const void*, void*, const unsigned char*, const unsigned char*);
using PaddingFn = int (*)(EVP_CIPHER_CTX*, int);
using UpdateFn = int (*)(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int);

struct CryptoApi {
    void* handle = nullptr;
    CtxNew newContext = nullptr;
    CtxFree freeContext = nullptr;
    CipherFn aes = nullptr;
    InitFn init = nullptr;
    PaddingFn padding = nullptr;
    UpdateFn update = nullptr;
    ~CryptoApi() {
        if (handle) dlclose(handle);
    }
    bool load() {
        handle = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!handle) handle = dlopen("libcrypto.so", RTLD_NOW | RTLD_LOCAL);
        if (!handle) return false;
        newContext = reinterpret_cast<CtxNew>(dlsym(handle, "EVP_CIPHER_CTX_new"));
        freeContext = reinterpret_cast<CtxFree>(dlsym(handle, "EVP_CIPHER_CTX_free"));
        aes = reinterpret_cast<CipherFn>(dlsym(handle, "EVP_aes_128_cbc"));
        init = reinterpret_cast<InitFn>(dlsym(handle, "EVP_DecryptInit_ex"));
        padding = reinterpret_cast<PaddingFn>(dlsym(handle, "EVP_CIPHER_CTX_set_padding"));
        update = reinterpret_cast<UpdateFn>(dlsym(handle, "EVP_DecryptUpdate"));
        return newContext && freeContext && aes && init && padding && update;
    }
};

namespace {
bool rawRknn(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && std::memcmp(data.data(), "RKNN", 4) == 0;
}
void loadHex(const char* name, uint8_t* output, const uint8_t* fallback) {
    std::memcpy(output, fallback, 16);
    const char* value = std::getenv(name);
    if (!value || std::strlen(value) < 32) return;
    for (int i = 0; i < 16; ++i) {
        unsigned int byte = 0;
        if (std::sscanf(value + i * 2, "%2x", &byte) == 1)
            output[i] = static_cast<uint8_t>(byte);
    }
}
}

bool loadRknnModel(const char* path, std::vector<uint8_t>& data,
                  std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { error = "cannot open model: " + std::string(path); return false; }
    const auto size = file.tellg();
    if (size <= 0) { error = "empty model: " + std::string(path); return false; }
    data.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) { error = "short model read: " + std::string(path); return false; }
    if (rawRknn(data)) return true;
    const uint8_t defaultKey[16] = {'!','A','k','2',']','u','V','0',0,0,0,0,0,0,0,0};
    const uint8_t defaultIv[16] = {'k','{','^','4','g','S','9','n',0,0,0,0,0,0,0,0};
    uint8_t key[16], iv[16];
    loadHex("MLFW_MODEL_KEY", key, defaultKey);
    loadHex("MLFW_MODEL_IV", iv, defaultIv);
    const size_t encryptedSize = std::min<size_t>(data.size(), 128 * 1024);
    CryptoApi crypto;
    if (!crypto.load()) { error = "libcrypto EVP API unavailable"; return false; }
    for (size_t offset = 0; offset < encryptedSize; offset += 1024) {
        const int chunk = static_cast<int>(std::min<size_t>(1024, encryptedSize - offset));
        EVP_CIPHER_CTX* context = crypto.newContext();
        if (!context) { error = "EVP context allocation failed"; return false; }
        int outputSize = 0;
        const bool ok = crypto.init(context, crypto.aes(), nullptr, key, iv) == 1 &&
            crypto.padding(context, 0) == 1 &&
            crypto.update(context, data.data() + offset, &outputSize,
                              data.data() + offset, chunk) == 1;
        crypto.freeContext(context);
        if (!ok) { error = "model decrypt failed at offset " + std::to_string(offset); return false; }
    }
    if (data.size() < 4 || std::memcmp(data.data(), "RKNN", 4) != 0) {
        error = "decrypted model is not a raw RKNN container";
        return false;
    }
    return true;
}
