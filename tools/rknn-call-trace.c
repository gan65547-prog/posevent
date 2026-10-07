/*
 * Optional diagnostic shim, not linked into posevent.
 *
 * Build against the board-matched rknn_api.h, then run a process with:
 *   LD_PRELOAD=/tmp/librknn_call_trace.so <command>
 *
 * It records the RKNN calls that determine the native input contract.  This
 * is intentionally useful for comparing a known-good reference process with
 * posevent without importing that process's code or runtime into posevent.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <rknn_api.h>

typedef int (*init_fn)(rknn_context*, void*, uint32_t, uint32_t, rknn_init_extend*);
typedef int (*core_fn)(rknn_context, rknn_core_mask);
typedef rknn_tensor_mem* (*create_mem_fn)(rknn_context, uint32_t);
typedef int (*set_io_mem_fn)(rknn_context, rknn_tensor_mem*, rknn_tensor_attr*);
typedef int (*mem_sync_fn)(rknn_context, rknn_tensor_mem*, rknn_mem_sync_mode);
typedef int (*inputs_set_fn)(rknn_context, uint32_t, rknn_input*);
typedef int (*run_fn)(rknn_context, rknn_run_extend*);

#define TRACE_CALL(name, type) \
    static type real_##name(void) { \
        static type function; \
        if (!function) function = (type)dlsym(RTLD_NEXT, #name); \
        return function; \
    }

TRACE_CALL(rknn_init, init_fn)
TRACE_CALL(rknn_set_core_mask, core_fn)
TRACE_CALL(rknn_create_mem, create_mem_fn)
TRACE_CALL(rknn_set_io_mem, set_io_mem_fn)
TRACE_CALL(rknn_mem_sync, mem_sync_fn)
TRACE_CALL(rknn_inputs_set, inputs_set_fn)
TRACE_CALL(rknn_run, run_fn)

int rknn_init(rknn_context* ctx, void* model, uint32_t size, uint32_t flags,
              rknn_init_extend* extend) {
    int ret = real_rknn_init()(ctx, model, size, flags, extend);
    fprintf(stderr, "[posevent-rknn-trace] init size=%u flags=%u ret=%d ctx=%" PRIu64 "\n",
            size, flags, ret, ctx ? (uint64_t)*ctx : 0);
    return ret;
}

int rknn_set_core_mask(rknn_context ctx, rknn_core_mask mask) {
    int ret = real_rknn_set_core_mask()(ctx, mask);
    fprintf(stderr, "[posevent-rknn-trace] core ctx=%" PRIu64 " mask=%d ret=%d\n",
            (uint64_t)ctx, mask, ret);
    return ret;
}

rknn_tensor_mem* rknn_create_mem(rknn_context ctx, uint32_t size) {
    rknn_tensor_mem* mem = real_rknn_create_mem()(ctx, size);
    fprintf(stderr, "[posevent-rknn-trace] create_mem ctx=%" PRIu64 " size=%u mem=%p\n",
            (uint64_t)ctx, size, (void*)mem);
    return mem;
}

int rknn_set_io_mem(rknn_context ctx, rknn_tensor_mem* mem, rknn_tensor_attr* attr) {
    int ret = real_rknn_set_io_mem()(ctx, mem, attr);
    fprintf(stderr, "[posevent-rknn-trace] set_io ctx=%" PRIu64
            " index=%u size=%u type=%d fmt=%d pass=%u mem=%p ret=%d\n",
            (uint64_t)ctx, attr ? attr->index : 0, attr ? attr->size_with_stride : 0,
            attr ? attr->type : -1, attr ? attr->fmt : -1,
            attr ? attr->pass_through : 0, (void*)mem, ret);
    return ret;
}

int rknn_mem_sync(rknn_context ctx, rknn_tensor_mem* mem, rknn_mem_sync_mode mode) {
    int ret = real_rknn_mem_sync()(ctx, mem, mode);
    fprintf(stderr, "[posevent-rknn-trace] mem_sync ctx=%" PRIu64
            " mem=%p mode=%d ret=%d\n", (uint64_t)ctx, (void*)mem, mode, ret);
    return ret;
}

int rknn_inputs_set(rknn_context ctx, uint32_t n, rknn_input* input) {
    int ret = real_rknn_inputs_set()(ctx, n, input);
    fprintf(stderr, "[posevent-rknn-trace] inputs_set ctx=%" PRIu64
            " n=%u type=%d fmt=%d pass=%u size=%u ret=%d\n",
            (uint64_t)ctx, n, n ? input[0].type : -1, n ? input[0].fmt : -1,
            n ? input[0].pass_through : 0, n ? input[0].size : 0, ret);
    return ret;
}

int rknn_run(rknn_context ctx, rknn_run_extend* extend) {
    int ret = real_rknn_run()(ctx, extend);
    fprintf(stderr, "[posevent-rknn-trace] run ctx=%" PRIu64
            " ext=%p nonblock=%d timeout=%d frame=%" PRIu64 " ret=%d\n",
            (uint64_t)ctx, (void*)extend, extend ? extend->non_block : 0,
            extend ? extend->timeout_ms : 0, extend ? extend->frame_id : 0, ret);
    return ret;
}
