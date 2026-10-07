#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "rknn_api.h"

// ---------------------------------------------------------------------------
// 基础图像类型
// ---------------------------------------------------------------------------
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> data; // RGB888, HWC layout

    bool valid() const { return !data.empty(); }
};

ImageBuffer load_image(const char* path);

// letterbox 缩放，保持宽高比，短边补齐到 dst_w x dst_h
struct LetterBox {
    int   x_pad  = 0;
    int   y_pad  = 0;
    float scale  = 1.0f;
};
ImageBuffer letterbox(const ImageBuffer& src, int dst_w, int dst_h,
                      LetterBox& lb, uint8_t pad_color = 114);

// ---------------------------------------------------------------------------
// 检测结果
// ---------------------------------------------------------------------------
struct DetectBox {
    int   x1, y1, x2, y2;  // 原图像素坐标
    float confidence;
    int   class_id;
};

struct DetectResult {
    std::vector<DetectBox> boxes;
};

// ---------------------------------------------------------------------------
// 模型上下文 (RAII)
// ---------------------------------------------------------------------------
class ModelContext {
public:
    ModelContext() = default;
    ~ModelContext();

    ModelContext(const ModelContext&) = delete;
    ModelContext& operator=(const ModelContext&) = delete;
    ModelContext(ModelContext&&) = default;
    ModelContext& operator=(ModelContext&&) = default;

    bool init(const char* model_path);

    bool infer(const ImageBuffer& image, DetectResult& result,
               float conf_threshold = 0.25f,
               float nms_threshold  = 0.45f);

    int  input_width()  const { return in_w_; }
    int  input_height() const { return in_h_; }
    bool is_quantized() const { return is_quant_; }

private:
    // 后处理：解码 YOLOv5 三尺度输出 + NMS + 坐标还原
    int postprocess(const rknn_output* outputs, const LetterBox& lb,
                    float conf_thres, float nms_thres,
                    DetectResult& result);

    // Ultralytics YOLOv8/YOLO11 detector export: one [1, 4 + classes, N]
    // tensor containing xywh boxes plus per-class confidences.
    int postprocessYoloV8(const rknn_output& output, const LetterBox& lb,
                          float conf_thres, float nms_thres,
                          DetectResult& result);

    int postprocessYoloPose(const rknn_output* outputs, const LetterBox& lb,
                            float conf_thres, float nms_thres,
                            DetectResult& result);

    // 三个尺度的网格解码
    static int decode_grid_fp32(const float* input, int grid_h, int grid_w,
                                 int stride, const int* anchors,
                                 int class_num, float threshold,
                                 std::vector<float>& boxes,
                                 std::vector<float>& scores,
                                 std::vector<int>& class_ids);
    static int decode_grid_i8(const int8_t* input, int grid_h, int grid_w,
                               int stride, const int* anchors,
                               int class_num, float threshold,
                               int32_t zp, float scale,
                               std::vector<float>& boxes,
                               std::vector<float>& scores,
                               std::vector<int>& class_ids);

    static float nms(std::vector<float>& boxes,
                     std::vector<float>& scores,
                     std::vector<int>& class_ids,
                     float threshold, std::vector<int>& keep);

    rknn_context          ctx_ = 0;
    // RKNN 1.6 may retain the caller-owned model graph until the context is
    // destroyed.  Keep decrypted bytes alive for the whole context lifetime.
    std::vector<uint8_t> model_data_;
    rknn_input_output_num io_num_ = {0, 0};
    std::vector<rknn_tensor_attr> in_attrs_;
    std::vector<rknn_tensor_attr> out_attrs_;
    rknn_tensor_mem* input_mem_ = nullptr;
    std::vector<rknn_tensor_mem*> output_mems_;
    int input_fd_ = -1;
    void* input_map_ = nullptr;
    size_t input_map_size_ = 0;

    int  in_w_ = 0, in_h_ = 0, in_c_ = 0;
    bool is_quant_ = false;

    // YOLOv5 固定参数
    static constexpr int kClassNum   = 80;
    static constexpr int kPropBoxLen = 5 + kClassNum;
    static constexpr int kMaxDet     = 128;
};
