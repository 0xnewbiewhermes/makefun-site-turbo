#include "engine_core.h"

__attribute__((visibility("hidden")))
void run_engine_avx2(char *cases_buf, size_t cases_len, uint8_t *arena_mem, size_t arena_cap,
                     uint8_t *out_mem, long out_fd) {
    run_engine_impl(cases_buf, cases_len, arena_mem, arena_cap, out_mem, out_fd);
}
