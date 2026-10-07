#include <rknn_api.h>
#include "model_loader.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static void printDims(const rknn_tensor_attr& attr) {
    std::cout << "index=" << attr.index << " name=" << attr.name
              << " n_dims=" << attr.n_dims << " dims=";
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        if (i) std::cout << 'x';
        std::cout << attr.dims[i];
    }
    std::cout << " type=" << attr.type << " fmt=" << attr.fmt << '\n';
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string path = argv[1];
    std::vector<uint8_t> model;
    std::string error;
    if (!loadRknnModel(path.c_str(), model, error)) {
        std::cerr << error << '\n'; return 7;
    }

    rknn_context context = 0;
    int ret = rknn_init(&context, model.data(), model.size(), 0, nullptr);
    if (ret < 0) { std::cerr << "rknn_init=" << ret << '\n'; return 5; }
    rknn_sdk_version sdk{};
    if (rknn_query(context, RKNN_QUERY_SDK_VERSION, &sdk, sizeof(sdk)) >= 0) {
        std::cout << "runtime_api=" << sdk.api_version
                  << " driver=" << sdk.drv_version << '\n';
    } else {
        std::cout << "runtime_version=unavailable\n";
    }
    rknn_input_output_num io{};
    ret = rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io));
    if (ret < 0) return 6;
    std::cout << "inputs=" << io.n_input << " outputs=" << io.n_output << '\n';
    for (uint32_t i = 0; i < io.n_input; ++i) {
        rknn_tensor_attr attr{}; attr.index = i;
        if (rknn_query(context, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr)) >= 0)
            printDims(attr);
    }
    for (uint32_t i = 0; i < io.n_output; ++i) {
        rknn_tensor_attr attr{}; attr.index = i;
        if (rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr)) >= 0)
            printDims(attr);
    }
    rknn_destroy(context);
}
