// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <bc/bc_core_io.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static bc_allocators_context_t* bench_make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        fputs("fatal: allocator init failed\n", stderr);
        exit(1);
    }
    return memory;
}

static char* bench_build_json_payload(size_t entries, size_t* out_size)
{
    size_t estimated = entries * 40u + 32u;
    char* buffer = (char*)malloc(estimated);
    size_t cursor = 0u;
    buffer[cursor++] = '{';
    for (size_t i = 0u; i < entries; i += 1u) {
        if (i != 0u) {
            buffer[cursor++] = ',';
        }
        int written = snprintf(&buffer[cursor], estimated - cursor, "\"k%08zu\":%zu", i, i);
        if (written < 0 || (size_t)written >= estimated - cursor) {
            free(buffer);
            return NULL;
        }
        cursor += (size_t)written;
    }
    buffer[cursor++] = '}';
    *out_size = cursor;
    return buffer;
}

static void* bench_build_hrbl_with_many_int64(bc_allocators_context_t* memory, size_t entries, size_t* out_hrbl_size)
{
    bc_hrbl_writer_t* writer = NULL;
    if (!bc_hrbl_writer_create(memory, &writer)) {
        fputs("fatal: writer create failed\n", stderr);
        exit(1);
    }
    if (!bc_hrbl_writer_begin_block(writer, "root", 4u)) {
        fputs("fatal: writer begin_block failed\n", stderr);
        exit(1);
    }
    char key_buffer[32];
    for (size_t i = 0u; i < entries; i += 1u) {
        int written = snprintf(key_buffer, sizeof(key_buffer), "k%08zu", i);
        if (!bc_hrbl_writer_set_int64(writer, key_buffer, (size_t)written, (int64_t)i)) {
            fputs("fatal: writer set_int64 failed\n", stderr);
            exit(1);
        }
    }
    (void)bc_hrbl_writer_end_block(writer);
    void* buffer = NULL;
    size_t size = 0u;
    if (!bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size)) {
        fputs("fatal: writer finalize failed\n", stderr);
        exit(1);
    }
    bc_hrbl_writer_destroy(writer);
    *out_hrbl_size = size;
    return buffer;
}

static void bench_writer(size_t entries)
{
    bc_allocators_context_t* memory = bench_make_memory();
    uint64_t best_ns = UINT64_MAX;
    size_t output_size = 0u;
    const int iterations = 5;
    for (int iter = 0; iter < iterations; iter += 1) {
        bc_hrbl_writer_t* writer = NULL;
        (void)bc_hrbl_writer_create(memory, &writer);
        char key_buffer[32];
        uint64_t start = bench_now_ns();
        for (size_t i = 0u; i < entries; i += 1u) {
            int written = snprintf(key_buffer, sizeof(key_buffer), "k%08zu", i);
            (void)bc_hrbl_writer_set_int64(writer, key_buffer, (size_t)written, (int64_t)i);
        }
        void* buffer = NULL;
        size_t size = 0u;
        (void)bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size);
        uint64_t elapsed = bench_now_ns() - start;
        if (elapsed < best_ns) {
            best_ns = elapsed;
        }
        output_size = size;
        bc_hrbl_free_buffer(memory, buffer);
        bc_hrbl_writer_destroy(writer);
    }
    double seconds = (double)best_ns / 1e9;
    double mb = (double)output_size / (1024.0 * 1024.0);
    printf("writer_100k    : entries=%zu  output=%zu B (%.2f MiB)  best=%.3f ms  throughput=%.2f MB/s\n", entries, output_size, mb,
           seconds * 1000.0, mb / seconds);
    bc_allocators_context_destroy(memory);
}

static void bench_reader_scan(size_t entries)
{
    bc_allocators_context_t* memory = bench_make_memory();
    size_t hrbl_size = 0u;
    void* hrbl_buffer = bench_build_hrbl_with_many_int64(memory, entries, &hrbl_size);

    bc_hrbl_reader_t* reader = NULL;
    (void)bc_hrbl_reader_open_buffer(memory, hrbl_buffer, hrbl_size, &reader);

    bc_hrbl_value_ref_t root_ref;
    if (!bc_hrbl_reader_find(reader, "root", 4u, &root_ref)) {
        fputs("fatal: scan root lookup failed\n", stderr);
        exit(1);
    }

    uint64_t total_best = UINT64_MAX;
    volatile int64_t sink = 0;
    const int iterations = 5;
    uint64_t visited = 0u;
    for (int iter = 0; iter < iterations; iter += 1) {
        bc_hrbl_iter_t it;
        (void)bc_hrbl_reader_iter_block(&root_ref, &it);
        uint64_t start = bench_now_ns();
        bc_hrbl_value_ref_t value;
        const char* key = NULL;
        size_t key_length = 0u;
        uint64_t local = 0u;
        while (bc_hrbl_iter_next(&it, &value, &key, &key_length)) {
            int64_t v = 0;
            bc_hrbl_reader_get_int64(&value, &v);
            sink += v;
            local += 1u;
        }
        uint64_t elapsed = bench_now_ns() - start;
        if (elapsed < total_best) {
            total_best = elapsed;
        }
        visited = local;
    }
    double seconds = (double)total_best / 1e9;
    double mb = (double)hrbl_size / (1024.0 * 1024.0);
    printf("reader_scan    : entries=%zu  size=%zu B (%.2f MiB)  best=%.3f ms  %.2f MB/s   (sink=%" PRId64 ", visited=%" PRIu64 ")\n",
           entries, hrbl_size, mb, seconds * 1000.0, mb / seconds, (int64_t)sink, visited);
    bc_hrbl_reader_destroy(reader);
    bc_hrbl_free_buffer(memory, hrbl_buffer);
    bc_allocators_context_destroy(memory);
}

static void bench_query_latency(size_t entries)
{
    bc_allocators_context_t* memory = bench_make_memory();
    size_t hrbl_size = 0u;
    void* hrbl_buffer = bench_build_hrbl_with_many_int64(memory, entries, &hrbl_size);

    bc_hrbl_reader_t* reader = NULL;
    (void)bc_hrbl_reader_open_buffer(memory, hrbl_buffer, hrbl_size, &reader);

    size_t num_keys = 32u;
    size_t iterations_per_key = 1u << 16;
    uint64_t best_total = UINT64_MAX;
    volatile int64_t sink = 0;
    const int runs = 5;
    for (int run = 0; run < runs; run += 1) {
        uint64_t start = bench_now_ns();
        for (size_t k = 0u; k < num_keys; k += 1u) {
            char key_buffer[32];
            size_t key_index = (entries / num_keys) * k;
            int written = snprintf(key_buffer, sizeof(key_buffer), "k%08zu", key_index);
            for (size_t i = 0u; i < iterations_per_key; i += 1u) {
                bc_hrbl_value_ref_t value;
                if (bc_hrbl_reader_find(reader, key_buffer, (size_t)written, &value)) {
                    int64_t v = 0;
                    bc_hrbl_reader_get_int64(&value, &v);
                    sink += v;
                }
            }
        }
        uint64_t elapsed = bench_now_ns() - start;
        if (elapsed < best_total) {
            best_total = elapsed;
        }
    }
    uint64_t total_ops = (uint64_t)num_keys * (uint64_t)iterations_per_key;
    double ns_per_op = (double)best_total / (double)total_ops;
    printf("query_latency  : entries=%zu  lookups=%" PRIu64 "  median_ns=%.1f   (sink=%" PRId64 ")\n", entries, total_ops, ns_per_op,
           (int64_t)sink);
    bc_hrbl_reader_destroy(reader);
    bc_hrbl_free_buffer(memory, hrbl_buffer);
    bc_allocators_context_destroy(memory);
}

static void bench_convert_json(size_t entries)
{
    bc_allocators_context_t* memory = bench_make_memory();
    size_t json_size = 0u;
    char* json_text = bench_build_json_payload(entries, &json_size);

    uint64_t best_ns = UINT64_MAX;
    size_t output_size = 0u;
    const int iterations = 3;
    for (int iter = 0; iter < iterations; iter += 1) {
        void* buffer = NULL;
        size_t size = 0u;
        bc_hrbl_convert_error_t error;
        uint64_t start = bench_now_ns();
        (void)bc_hrbl_convert_json_buffer_to_hrbl(memory, json_text, json_size, &buffer, &size, &error);
        uint64_t elapsed = bench_now_ns() - start;
        if (elapsed < best_ns) {
            best_ns = elapsed;
        }
        output_size = size;
        bc_hrbl_free_buffer(memory, buffer);
    }
    double seconds = (double)best_ns / 1e9;
    double json_mb = (double)json_size / (1024.0 * 1024.0);
    printf("json_to_hrbl   : json=%zu B (%.2f MiB)  hrbl=%zu B  best=%.3f ms  %.2f MB/s\n", json_size, json_mb, output_size,
           seconds * 1000.0, json_mb / seconds);
    free(json_text);
    bc_allocators_context_destroy(memory);
}

static void bench_export_json(size_t entries)
{
    bc_allocators_context_t* memory = bench_make_memory();
    size_t hrbl_size = 0u;
    void* hrbl_buffer = bench_build_hrbl_with_many_int64(memory, entries, &hrbl_size);

    bc_hrbl_reader_t* reader = NULL;
    (void)bc_hrbl_reader_open_buffer(memory, hrbl_buffer, hrbl_size, &reader);

    uint64_t best_ns = UINT64_MAX;
    size_t output_size = 0u;
    const int iterations = 3;
    /* Sized for ~entries * ~40 bytes of JSON; padded for safety. */
    size_t sink_capacity = entries * 64u + 1024u;
    char* sink_buffer = (char*)malloc(sink_capacity);
    if (sink_buffer == NULL) {
        fputs("fatal: bench export sink alloc failed\n", stderr);
        exit(1);
    }
    for (int iter = 0; iter < iterations; iter += 1) {
        bc_core_writer_t writer;
        (void)bc_core_writer_init_buffer_only(&writer, sink_buffer, sink_capacity);
        uint64_t start = bench_now_ns();
        (void)bc_hrbl_export_json(reader, &writer);
        uint64_t elapsed = bench_now_ns() - start;
        const char* out_data = NULL;
        size_t out_length = 0u;
        (void)bc_core_writer_buffer_data(&writer, &out_data, &out_length);
        output_size = out_length;
        bc_core_writer_destroy(&writer);
        if (elapsed < best_ns) {
            best_ns = elapsed;
        }
    }
    free(sink_buffer);
    double seconds = (double)best_ns / 1e9;
    double mb = (double)output_size / (1024.0 * 1024.0);
    printf("hrbl_to_json   : hrbl=%zu B  json=%zu B (%.2f MiB)  best=%.3f ms  %.2f MB/s\n", hrbl_size, output_size, mb, seconds * 1000.0,
           mb / seconds);
    bc_hrbl_reader_destroy(reader);
    bc_hrbl_free_buffer(memory, hrbl_buffer);
    bc_allocators_context_destroy(memory);
}

int main(int argument_count, char** argument_values)
{
    size_t entries = 100000u;
    if (argument_count >= 2) {
        long parsed = strtol(argument_values[1], NULL, 10);
        if (parsed > 0) {
            entries = (size_t)parsed;
        }
    }
    bench_writer(entries);
    bench_reader_scan(entries);
    bench_query_latency(entries);
    bench_convert_json(entries);
    bench_export_json(entries);
    return 0;
}
