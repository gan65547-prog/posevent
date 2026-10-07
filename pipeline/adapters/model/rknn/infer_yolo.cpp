#include "infer_yolo.h"
#include "model_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-heap.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------
namespace {

inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

inline int clamp_i(float v, int lo, int hi) {
    if (v <= lo) return lo;
    if (v >= hi) return hi;
    return static_cast<int>(v);
}

// 量化 <-> 浮点 转换
inline int8_t  qnt_f32_to_i8(float f32, int32_t zp, float scale) {
    float v = f32 / scale + zp;
    return static_cast<int8_t>(clamp_i(v, -128, 127));
}
inline float deqnt_i8_to_f32(int8_t qnt, int32_t zp, float scale) {
    return (static_cast<float>(qnt) - zp) * scale;
}

// RKNN io_mem uses the model-declared native element type.  The board's FP16
// detector expects normalized RGB half values, not a UINT8 byte stream.
uint16_t normalizedU8ToFp16(uint8_t value) {
    const float source = static_cast<float>(value) / 255.0F;
    uint32_t bits = 0;
    std::memcpy(&bits, &source, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000U;
    const int exponent = static_cast<int>((bits >> 23) & 0xffU) - 127;
    const uint32_t mantissa = (bits >> 13) & 0x3ffU;
    if (exponent > 15) return static_cast<uint16_t>(sign | 0x7c00U);
    if (exponent >= -14)
        return static_cast<uint16_t>(sign | ((exponent + 15) << 10) | mantissa);
    if (exponent >= -24)
        return static_cast<uint16_t>(sign | ((mantissa | 0x400U) >> (1 - exponent)));
    return static_cast<uint16_t>(sign);
}

// YOLOv5 COCO anchor
const int kAnchors[3][6] = {
    {10, 13,  16, 30,  33, 23},
    {30, 61,  62, 45,  59, 119},
    {116, 90, 156, 198, 373, 326}
};

// Four-level anchors used by the deployed MultiPose export.  Keypoints are
// intentionally not surfaced by DetectResult yet; its person boxes share the
// exact same region-event contract as the detector adapter.
const float kPoseAnchors[4][6] = {
    {19, 27, 44, 40, 38, 94},
    {96, 68, 86, 152, 180, 137},
    {140, 301, 303, 264, 238, 542},
    {436, 615, 739, 380, 925, 792},
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 图片读写 (OpenCV)
// ---------------------------------------------------------------------------
ImageBuffer load_image(const char* path) {
    cv::Mat bgr = cv::imread(path, cv::IMREAD_COLOR);
    if (bgr.empty()) return {};

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    ImageBuffer img;
    img.width    = rgb.cols;
    img.height   = rgb.rows;
    img.channels = 3;
    img.data.assign(rgb.data, rgb.data + rgb.total() * 3);
    return img;
}

// ---------------------------------------------------------------------------
// letterbox: 保持宽高比缩放，短边补齐
// ---------------------------------------------------------------------------
ImageBuffer letterbox(const ImageBuffer& src, int dst_w, int dst_h,
                      LetterBox& lb, uint8_t pad_color) {
    float scale = std::min(
        static_cast<float>(dst_w) / src.width,
        static_cast<float>(dst_h) / src.height);
    int new_w = static_cast<int>(src.width  * scale);
    int new_h = static_cast<int>(src.height * scale);

    int x_pad = (dst_w - new_w) / 2;
    int y_pad = (dst_h - new_h) / 2;

    lb.scale = scale;
    lb.x_pad = x_pad;
    lb.y_pad = y_pad;

    // 用 OpenCV resize
    cv::Mat src_mat(src.height, src.width, CV_8UC3,
                    const_cast<uint8_t*>(src.data.data()));
    cv::Mat resized, dst_mat;
    cv::resize(src_mat, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
    cv::copyMakeBorder(resized, dst_mat, y_pad, dst_h - new_h - y_pad,
                       x_pad, dst_w - new_w - x_pad,
                       cv::BORDER_CONSTANT,
                       cv::Scalar(pad_color, pad_color, pad_color));

    ImageBuffer img;
    img.width    = dst_w;
    img.height   = dst_h;
    img.channels = 3;
    img.data.assign(dst_mat.data, dst_mat.data + dst_mat.total() * 3);
    return img;
}

// ---------------------------------------------------------------------------
// ModelContext 实现
// ---------------------------------------------------------------------------
ModelContext::~ModelContext() {
    if (ctx_ != 0) {
        for (auto* mem : output_mems_) {
            if (mem != nullptr) rknn_destroy_mem(ctx_, mem);
        }
        output_mems_.clear();
    }
    if (input_mem_ != nullptr && ctx_ != 0) {
        rknn_destroy_mem(ctx_, input_mem_);
        input_mem_ = nullptr;
    }
    if (input_map_ != nullptr && input_map_size_ != 0) {
        munmap(input_map_, input_map_size_);
        input_map_ = nullptr;
    }
    if (input_fd_ >= 0) {
        close(input_fd_);
        input_fd_ = -1;
    }
    if (ctx_ != 0) {
        rknn_destroy(ctx_);
        ctx_ = 0;
    }
}

static void dump_tensor_attr(const rknn_tensor_attr& attr) {
    printf("  idx=%d name=%s dims=[%d,%d,%d,%d] elems=%d size=%d fmt=%d type=%d qnt=%d zp=%d scale=%f\
",
           attr.index, attr.name,
           attr.dims[0], attr.dims[1], attr.dims[2], attr.dims[3],
           attr.n_elems, attr.size, attr.fmt, attr.type,
           attr.qnt_type, attr.zp, attr.scale);
}

bool ModelContext::init(const char* model_path) {
    std::string model_error;
    if (!loadRknnModel(model_path, model_data_, model_error)) {
        fprintf(stderr, "model load failed: %s\n", model_error.c_str());
        return false;
    }

    int ret = rknn_init(&ctx_, model_data_.data(), model_data_.size(), 0, nullptr);
    if (ret < 0) {
        fprintf(stderr, "rknn_init fail ret=%d\
", ret);
        return false;
    }

    // Match the board production default: let the runtime select the core.
    ret = rknn_set_core_mask(ctx_, RKNN_NPU_CORE_AUTO);
    if (ret < 0) {
        fprintf(stderr, "rknn_set_core_mask(all) fail ret=%d\n", ret);
        return false;
    }

    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret < 0) {
        fprintf(stderr, "rknn_query IN_OUT_NUM fail ret=%d\
", ret);
        return false;
    }
    printf("input num=%d, output num=%d\
", io_num_.n_input, io_num_.n_output);

    in_attrs_.resize(io_num_.n_input);
    printf("input tensors:\
");
    for (int i = 0; i < io_num_.n_input; i++) {
        in_attrs_[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &in_attrs_[i],
                         sizeof(rknn_tensor_attr));
        if (ret < 0) {
            fprintf(stderr, "rknn_query INPUT_ATTR[%d] fail ret=%d\
", i, ret);
            return false;
        }
        dump_tensor_attr(in_attrs_[i]);
    }

    out_attrs_.resize(io_num_.n_output);
    printf("output tensors:\
");
    for (int i = 0; i < io_num_.n_output; i++) {
        out_attrs_[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &out_attrs_[i],
                         sizeof(rknn_tensor_attr));
        if (ret < 0) {
            fprintf(stderr, "rknn_query OUTPUT_ATTR[%d] fail ret=%d\
", i, ret);
            return false;
        }
        dump_tensor_attr(out_attrs_[i]);
    }

    if (out_attrs_[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
        out_attrs_[0].type != RKNN_TENSOR_FLOAT16) {
        is_quant_ = true;
    }

    if (in_attrs_[0].fmt == RKNN_TENSOR_NCHW) {
        in_c_ = in_attrs_[0].dims[1];
        in_h_ = in_attrs_[0].dims[2];
        in_w_ = in_attrs_[0].dims[3];
    } else {
        in_h_ = in_attrs_[0].dims[1];
        in_w_ = in_attrs_[0].dims[2];
        in_c_ = in_attrs_[0].dims[3];
    }
    printf("model: %dx%dx%d, is_quant=%d\
", in_w_, in_h_, in_c_, is_quant_);
    // The deployed board pipeline proves that this RKNN 1.6 FP16 detector
    // needs a persistent native *input* buffer.  It still retrieves outputs
    // through rknn_outputs_get rather than binding output io_mem leases.
    const size_t input_size = std::max<size_t>(
        in_attrs_[0].size_with_stride, in_attrs_[0].size);
    // Match the proven board pipeline.  Importing a generic dma-heap fd can
    // register successfully yet be rejected by this driver's IOMMU at submit.
    input_mem_ = rknn_create_mem(ctx_, static_cast<uint32_t>(input_size));
    if (input_mem_ == nullptr) {
        fprintf(stderr, "rknn_create_mem(input) failed\n");
        return false;
    }
    rknn_tensor_attr input_attr = in_attrs_[0];
    input_attr.index = 0;
    input_attr.pass_through = 1;
    if (rknn_set_io_mem(ctx_, input_mem_, &input_attr) < 0) {
        fprintf(stderr, "rknn_set_io_mem(input) failed\n");
        return false;
    }
    if (out_attrs_[0].type == RKNN_TENSOR_FLOAT16) return true;
    output_mems_.resize(out_attrs_.size(), nullptr);
    for (size_t i = 0; i < out_attrs_.size(); ++i) {
        output_mems_[i] = rknn_create_mem(ctx_, out_attrs_[i].size);
        if (output_mems_[i] == nullptr) {
            fprintf(stderr, "rknn_create_mem(output[%zu]) failed\n", i);
            return false;
        }
        rknn_tensor_attr output_attr = out_attrs_[i];
        output_attr.index = static_cast<uint32_t>(i);
        output_attr.pass_through = 1;
        if (rknn_set_io_mem(ctx_, output_mems_[i], &output_attr) < 0) {
            fprintf(stderr, "rknn_set_io_mem(output[%zu]) failed\n", i);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 推理
// ---------------------------------------------------------------------------
bool ModelContext::infer(const ImageBuffer& image, DetectResult& result,
                         float conf_threshold, float nms_threshold) {
    if (ctx_ == 0) return false;

    // 预处理: letterbox
    LetterBox lb;
    ImageBuffer pre = letterbox(image, in_w_, in_h_, lb);

    // 设置输入
    int ret = 0;
    if (input_mem_ != nullptr && input_mem_->virt_addr != nullptr) {
        const size_t input_bytes = pre.data.size();
        if (in_attrs_[0].type == RKNN_TENSOR_FLOAT16) {
            const size_t native_bytes = input_bytes * sizeof(uint16_t);
            if (input_mem_->size < native_bytes) return false;
            auto* native = static_cast<uint16_t*>(input_mem_->virt_addr);
            for (size_t i = 0; i < input_bytes; ++i)
                native[i] = normalizedU8ToFp16(pre.data[i]);
        } else {
            if (input_mem_->size < input_bytes) return false;
            std::memcpy(input_mem_->virt_addr, pre.data.data(), input_bytes);
        }
        ret = rknn_mem_sync(ctx_, input_mem_, RKNN_MEMORY_SYNC_TO_DEVICE);
        if (ret < 0) {
            fprintf(stderr, "rknn_mem_sync(input) fail ret=%d\n", ret);
            return false;
        }
    } else {
        rknn_input input{};
        input.index = 0;
        // Match the reference RKNN 1.6 FP16 detector contract: submit the
        // camera tensor as UINT8/NHWC and let the runtime perform conversion.
        input.type = RKNN_TENSOR_UINT8;
        input.fmt = RKNN_TENSOR_NHWC;
        input.pass_through = 0;
        input.size = static_cast<uint32_t>(pre.data.size());
        input.buf = pre.data.data();
        ret = rknn_inputs_set(ctx_, 1, &input);
        if (ret < 0) return false;
    }

    ret = rknn_run(ctx_, nullptr);
    if (ret < 0) {
        fprintf(stderr, "rknn_run fail ret=%d\n", ret);
        return false;
    }

    // 获取输出
    std::vector<rknn_output> outputs(io_num_.n_output);
    for (int i = 0; i < io_num_.n_output; i++) {
        outputs[i].index      = i;
        outputs[i].want_float = is_quant_ ? 0 : 1;
    }
    ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
    if (ret < 0) {
        fprintf(stderr, "rknn_outputs_get fail ret=%d\
", ret);
        return false;
    }

    // 后处理
    postprocess(outputs.data(), lb, conf_threshold, nms_threshold, result);

    rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());
    return true;
}

// ---------------------------------------------------------------------------
// FP32 网格解码
// ---------------------------------------------------------------------------
int ModelContext::decode_grid_fp32(const float* input, int grid_h, int grid_w,
                                    int stride, const int* anchors,
                                    int class_num, float threshold,
                                    std::vector<float>& boxes,
                                    std::vector<float>& scores,
                                    std::vector<int>& class_ids) {
    int valid = 0;
    int grid_len = grid_h * grid_w;

    for (int a = 0; a < 3; a++) {
        for (int y = 0; y < grid_h; y++) {
            for (int x = 0; x < grid_w; x++) {
                int offset = (kPropBoxLen * a + 4) * grid_len + y * grid_w + x;
                float obj_conf = input[offset];
                if (obj_conf < threshold) continue;

                const float* ptr = input + (kPropBoxLen * a) * grid_len + y * grid_w + x;

                float bx = (ptr[0] * 2.0f - 0.5f + x) * stride;
                float by = (ptr[1] * 2.0f - 0.5f + y) * stride;
                float bw = std::pow(ptr[2] * 2.0f, 2) * anchors[a * 2];
                float bh = std::pow(ptr[3] * 2.0f, 2) * anchors[a * 2 + 1];

                float max_cls = ptr[5 * grid_len];
                int   max_id  = 0;
                for (int k = 1; k < class_num; k++) {
                    float v = ptr[(5 + k) * grid_len];
                    if (v > max_cls) { max_cls = v; max_id = k; }
                }
                float score = obj_conf * max_cls;
                if (score < threshold) continue;

                boxes.push_back(bx - bw / 2.0f);
                boxes.push_back(by - bh / 2.0f);
                boxes.push_back(bw);
                boxes.push_back(bh);
                scores.push_back(score);
                class_ids.push_back(max_id);
                valid++;
            }
        }
    }
    return valid;
}

// ---------------------------------------------------------------------------
// INT8 网格解码
// ---------------------------------------------------------------------------
int ModelContext::decode_grid_i8(const int8_t* input, int grid_h, int grid_w,
                                  int stride, const int* anchors,
                                  int class_num, float threshold,
                                  int32_t zp, float scale,
                                  std::vector<float>& boxes,
                                  std::vector<float>& scores,
                                  std::vector<int>& class_ids) {
    int valid = 0;
    int grid_len = grid_h * grid_w;
    int8_t thres_i8 = qnt_f32_to_i8(threshold, zp, scale);

    for (int a = 0; a < 3; a++) {
        for (int y = 0; y < grid_h; y++) {
            for (int x = 0; x < grid_w; x++) {
                int offset = (kPropBoxLen * a + 4) * grid_len + y * grid_w + x;
                int8_t obj_conf = input[offset];
                if (obj_conf < thres_i8) continue;

                const int8_t* ptr = input + (kPropBoxLen * a) * grid_len + y * grid_w + x;

                float bx = (deqnt_i8_to_f32(ptr[0], zp, scale) * 2.0f - 0.5f + x) * stride;
                float by = (deqnt_i8_to_f32(ptr[1], zp, scale) * 2.0f - 0.5f + y) * stride;
                float bw = std::pow(deqnt_i8_to_f32(ptr[2], zp, scale) * 2.0f, 2) * anchors[a * 2];
                float bh = std::pow(deqnt_i8_to_f32(ptr[3], zp, scale) * 2.0f, 2) * anchors[a * 2 + 1];

                int8_t max_cls = ptr[5 * grid_len];
                int    max_id  = 0;
                for (int k = 1; k < class_num; k++) {
                    int8_t v = ptr[(5 + k) * grid_len];
                    if (v > max_cls) { max_cls = v; max_id = k; }
                }
                float score = deqnt_i8_to_f32(obj_conf, zp, scale) *
                              deqnt_i8_to_f32(max_cls, zp, scale);
                if (score < threshold) continue;

                boxes.push_back(bx - bw / 2.0f);
                boxes.push_back(by - bh / 2.0f);
                boxes.push_back(bw);
                boxes.push_back(bh);
                scores.push_back(score);
                class_ids.push_back(max_id);
                valid++;
            }
        }
    }
    return valid;
}

// ---------------------------------------------------------------------------
// NMS
// ---------------------------------------------------------------------------
float ModelContext::nms(std::vector<float>& boxes,
                        std::vector<float>& scores,
                        std::vector<int>& class_ids,
                        float threshold, std::vector<int>& keep) {
    keep.clear();
    int n = static_cast<int>(scores.size());

    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return scores[a] > scores[b];
    });

    std::vector<bool> suppressed(n, false);
    for (int i = 0; i < n; i++) {
        int ii = idx[i];
        if (suppressed[ii]) continue;
        keep.push_back(ii);

        float x1_i = boxes[ii * 4 + 0];
        float y1_i = boxes[ii * 4 + 1];
        float x2_i = x1_i + boxes[ii * 4 + 2];
        float y2_i = y1_i + boxes[ii * 4 + 3];
        float area_i = boxes[ii * 4 + 2] * boxes[ii * 4 + 3];

        for (int j = i + 1; j < n; j++) {
            int jj = idx[j];
            if (suppressed[jj] || class_ids[ii] != class_ids[jj]) continue;

            float x1_j = boxes[jj * 4 + 0];
            float y1_j = boxes[jj * 4 + 1];
            float x2_j = x1_j + boxes[jj * 4 + 2];
            float y2_j = y1_j + boxes[jj * 4 + 3];
            float area_j = boxes[jj * 4 + 2] * boxes[jj * 4 + 3];

            float iw = std::max(0.0f, std::min(x2_i, x2_j) - std::max(x1_i, x1_j));
            float ih = std::max(0.0f, std::min(y2_i, y2_j) - std::max(y1_i, y1_j));
            float iou = (iw * ih) / (area_i + area_j - iw * ih);

            if (iou > threshold) suppressed[jj] = true;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 后处理主流程
// ---------------------------------------------------------------------------
int ModelContext::postprocess(const rknn_output* outputs, const LetterBox& lb,
                              float conf_thres, float nms_thres,
                              DetectResult& result) {
    result.boxes.clear();

    // Current deployed YOLO11 exports a single [1, 84, 8400] FP16 tensor.
    // The previous implementation unconditionally indexed three tensors,
    // which aborted after a successful RKNN submission on this model.
    if (io_num_.n_output == 1 && out_attrs_.size() == 1 &&
        out_attrs_[0].n_dims == 3 && out_attrs_[0].dims[1] >= 5) {
        return postprocessYoloV8(outputs[0], lb, conf_thres, nms_thres, result);
    }
    if (io_num_.n_output == 4 && out_attrs_.size() == 4 &&
        out_attrs_[0].n_dims == 4 && out_attrs_[0].dims[1] == 171) {
        return postprocessYoloPose(outputs, lb, conf_thres, nms_thres, result);
    }
    if (io_num_.n_output < 3 || out_attrs_.size() < 3) {
        fprintf(stderr, "unsupported RKNN output layout: outputs=%u dims=%u\\n",
                io_num_.n_output, out_attrs_.empty() ? 0U : out_attrs_[0].n_dims);
        return -1;
    }

    std::vector<float> all_boxes, all_scores;
    std::vector<int>   all_class_ids;

    for (int i = 0; i < 3; i++) {
        int grid_h = out_attrs_[i].dims[2];
        int grid_w = out_attrs_[i].dims[3];
        int stride = in_h_ / grid_h;

        const int class_num = static_cast<int>(out_attrs_[i].dims[1] / 3) - 5;
        if (class_num <= 0) return -1;
        int n = 0;
        if (is_quant_) {
            n = decode_grid_i8(
                static_cast<const int8_t*>(outputs[i].buf),
                grid_h, grid_w, stride,
                const_cast<int*>(kAnchors[i]), class_num, conf_thres,
                out_attrs_[i].zp, out_attrs_[i].scale,
                all_boxes, all_scores, all_class_ids);
        } else {
            n = decode_grid_fp32(
                static_cast<const float*>(outputs[i].buf),
                grid_h, grid_w, stride,
                const_cast<int*>(kAnchors[i]), class_num, conf_thres,
                all_boxes, all_scores, all_class_ids);
        }
        if (n < 0) return -1;
    }

    if (all_scores.empty()) return 0;

    std::vector<int> keep;
    nms(all_boxes, all_scores, all_class_ids, nms_thres, keep);

    result.boxes.reserve(keep.size());
    for (int idx : keep) {
        float x1 = all_boxes[idx * 4 + 0];
        float y1 = all_boxes[idx * 4 + 1];
        float x2 = x1 + all_boxes[idx * 4 + 2];
        float y2 = y1 + all_boxes[idx * 4 + 3];

        result.boxes.push_back({
            clamp_i((x1 - lb.x_pad) / lb.scale, 0, 99999),
            clamp_i((y1 - lb.y_pad) / lb.scale, 0, 99999),
            clamp_i((x2 - lb.x_pad) / lb.scale, 0, 99999),
            clamp_i((y2 - lb.y_pad) / lb.scale, 0, 99999),
            all_scores[idx], all_class_ids[idx]
        });
    }
    return static_cast<int>(result.boxes.size());
}

int ModelContext::postprocessYoloV8(const rknn_output& output,
                                    const LetterBox& lb,
                                    float conf_thres, float nms_thres,
                                    DetectResult& result) {
    const rknn_tensor_attr& attr = out_attrs_[0];
    const int channels = static_cast<int>(attr.dims[1]);
    const int candidate_count = static_cast<int>(attr.dims[2]);
    const int class_num = channels - 4;
    if (output.buf == nullptr || class_num <= 0 || candidate_count <= 0) {
        return -1;
    }

    // infer() requests floating-point output for non-quantized models, so the
    // SDK expands this FP16 tensor to float32 before returning it.
    if (is_quant_) {
        fprintf(stderr, "quantized single-output YOLO layout is not supported\\n");
        return -1;
    }
    const auto* values = static_cast<const float*>(output.buf);
    std::vector<float> all_boxes;
    std::vector<float> all_scores;
    std::vector<int> all_class_ids;
    all_boxes.reserve(static_cast<size_t>(candidate_count) * 4U);

    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        float best_score = 0.0F;
        int best_class = -1;
        for (int class_id = 0; class_id < class_num; ++class_id) {
            const float score = values[(4 + class_id) * candidate_count + candidate];
            if (score > best_score) {
                best_score = score;
                best_class = class_id;
            }
        }
        if (best_score < conf_thres) continue;

        const float width = values[2 * candidate_count + candidate];
        const float height = values[3 * candidate_count + candidate];
        if (width <= 0.0F || height <= 0.0F) continue;
        const float center_x = values[candidate];
        const float center_y = values[candidate_count + candidate];
        all_boxes.push_back(center_x - width * 0.5F);
        all_boxes.push_back(center_y - height * 0.5F);
        all_boxes.push_back(width);
        all_boxes.push_back(height);
        all_scores.push_back(best_score);
        all_class_ids.push_back(best_class);
    }

    if (all_scores.empty()) return 0;
    std::vector<int> keep;
    nms(all_boxes, all_scores, all_class_ids, nms_thres, keep);
    result.boxes.reserve(keep.size());
    for (const int index : keep) {
        const float x1 = all_boxes[index * 4];
        const float y1 = all_boxes[index * 4 + 1];
        const float x2 = x1 + all_boxes[index * 4 + 2];
        const float y2 = y1 + all_boxes[index * 4 + 3];
        result.boxes.push_back({
            clamp_i((x1 - lb.x_pad) / lb.scale, 0, 99999),
            clamp_i((y1 - lb.y_pad) / lb.scale, 0, 99999),
            clamp_i((x2 - lb.x_pad) / lb.scale, 0, 99999),
            clamp_i((y2 - lb.y_pad) / lb.scale, 0, 99999),
            all_scores[index], all_class_ids[index]
        });
    }
    return static_cast<int>(result.boxes.size());
}

int ModelContext::postprocessYoloPose(const rknn_output* outputs,
                                      const LetterBox& lb,
                                      float conf_thres, float nms_thres,
                                      DetectResult& result) {
    if (is_quant_) {
        fprintf(stderr, "quantized YOLOPose layout is not supported\\n");
        return -1;
    }
    constexpr int kAnchorsPerLayer = 3;
    constexpr int kProperties = 57;  // xywh, objectness, one class, 17*xyz
    std::vector<float> all_boxes, all_scores;
    std::vector<int> all_class_ids;
    for (int layer = 0; layer < 4; ++layer) {
        const rknn_tensor_attr& attr = out_attrs_[layer];
        const int grid_h = static_cast<int>(attr.dims[2]);
        const int grid_w = static_cast<int>(attr.dims[3]);
        const int grid_len = grid_h * grid_w;
        if (outputs[layer].buf == nullptr || grid_len <= 0 ||
            attr.dims[1] != kAnchorsPerLayer * kProperties) return -1;
        const int stride = in_h_ / grid_h;
        const auto* values = static_cast<const float*>(outputs[layer].buf);
        for (int anchor = 0; anchor < kAnchorsPerLayer; ++anchor) {
            const float* feature = values + anchor * kProperties * grid_len;
            for (int position = 0; position < grid_len; ++position) {
                const float objectness = sigmoid(feature[4 * grid_len + position]);
                if (objectness < conf_thres) continue;
                const float class_score = sigmoid(feature[5 * grid_len + position]);
                const float score = std::max(objectness, objectness * class_score);
                if (score < conf_thres) continue;
                const int grid_y = position / grid_w;
                const int grid_x = position % grid_w;
                const float center_x = (sigmoid(feature[position]) * 2.0F - 0.5F + grid_x) * stride;
                const float center_y = (sigmoid(feature[grid_len + position]) * 2.0F - 0.5F + grid_y) * stride;
                const float width = std::pow(sigmoid(feature[2 * grid_len + position]) * 2.0F, 2.0F) * kPoseAnchors[layer][anchor * 2];
                const float height = std::pow(sigmoid(feature[3 * grid_len + position]) * 2.0F, 2.0F) * kPoseAnchors[layer][anchor * 2 + 1];
                all_boxes.insert(all_boxes.end(), {center_x - width * 0.5F, center_y - height * 0.5F, width, height});
                all_scores.push_back(score);
                all_class_ids.push_back(0);
            }
        }
    }
    if (all_scores.empty()) return 0;
    std::vector<int> keep;
    nms(all_boxes, all_scores, all_class_ids, nms_thres, keep);
    result.boxes.reserve(keep.size());
    for (const int index : keep) {
        const float x1 = all_boxes[index * 4];
        const float y1 = all_boxes[index * 4 + 1];
        const float x2 = x1 + all_boxes[index * 4 + 2];
        const float y2 = y1 + all_boxes[index * 4 + 3];
        result.boxes.push_back({clamp_i((x1 - lb.x_pad) / lb.scale, 0, 99999),
                                clamp_i((y1 - lb.y_pad) / lb.scale, 0, 99999),
                                clamp_i((x2 - lb.x_pad) / lb.scale, 0, 99999),
                                clamp_i((y2 - lb.y_pad) / lb.scale, 0, 99999),
                                all_scores[index], 0});
    }
    return static_cast<int>(result.boxes.size());
}
