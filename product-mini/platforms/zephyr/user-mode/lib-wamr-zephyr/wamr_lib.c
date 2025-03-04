/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdlib.h>
#include <string.h>
#include "bh_platform.h"
#include "bh_assert.h"
#include "bh_log.h"
#include "bh_queue.h"
#include "wasm_export.h"
#if defined(BUILD_TARGET_RISCV64_LP64) || defined(BUILD_TARGET_RISCV32_ILP32)
#include "test_wasm_riscv64.h"
#else
#include "test_wasm.h"
#endif /* end of BUILD_TARGET_RISCV64_LP64 || BUILD_TARGET_RISCV32_ILP32 */

#if defined(BUILD_TARGET_RISCV64_LP64) || defined(BUILD_TARGET_RISCV32_ILP32)
#define CONFIG_GLOBAL_HEAP_BUF_SIZE 5120
#define CONFIG_APP_STACK_SIZE 512
#define CONFIG_APP_HEAP_SIZE 512
#else /* else of BUILD_TARGET_RISCV64_LP64 || BUILD_TARGET_RISCV32_ILP32 */
#define CONFIG_GLOBAL_HEAP_BUF_SIZE WASM_GLOBAL_HEAP_SIZE
#define CONFIG_APP_STACK_SIZE 8192
#define CONFIG_APP_HEAP_SIZE 8192
#endif /* end of BUILD_TARGET_RISCV64_LP64 || BUILD_TARGET_RISCV32_ILP32 */

static int app_argc;
static char **app_argv;

/**
 * Find the unique main function from a WASM module instance
 * and execute that function.
 *
 * @param module_inst the WASM module instance
 * @param argc the number of arguments
 * @param argv the arguments array
 *
 * @return true if the main function is called, false otherwise.
 */
bool
wasm_application_execute_main(wasm_module_inst_t module_inst, int argc,
                              char *argv[]);

static void *
app_instance_main(wasm_module_inst_t module_inst)
{
    const char *exception;
    wasm_function_inst_t func;
    wasm_exec_env_t exec_env;
    unsigned argv[2] = { 0 };

    if (wasm_runtime_lookup_function(module_inst, "main")
        || wasm_runtime_lookup_function(module_inst, "__main_argc_argv")) {
        LOG_VERBOSE("Calling main function\n");
        wasm_application_execute_main(module_inst, app_argc, app_argv);
    }
    else if ((func = wasm_runtime_lookup_function(module_inst, "app_main"))) {
        exec_env =
            wasm_runtime_create_exec_env(module_inst, CONFIG_APP_HEAP_SIZE);
        if (!exec_env) {
            os_printf("Create exec env failed\n");
            return NULL;
        }

        LOG_VERBOSE("Calling app_main function\n");
        wasm_runtime_call_wasm(exec_env, func, 0, argv);

        if (!wasm_runtime_get_exception(module_inst)) {
            os_printf("result: 0x%x\n", argv[0]);
        }

        wasm_runtime_destroy_exec_env(exec_env);
    }
    else {
        os_printf("Failed to lookup function main or app_main to call\n");
        return NULL;
    }

    if ((exception = wasm_runtime_get_exception(module_inst)))
        os_printf("%s\n", exception);

    return NULL;
}

#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
static char global_heap_buf[CONFIG_GLOBAL_HEAP_BUF_SIZE] = { 0 };
#endif

void
iwasm_main(void *arg1, void *arg2, void *arg3)
{
    int start, end;
    start = k_uptime_get_32();
    uint8 *wasm_file_buf = NULL;
    uint32 wasm_file_size;
    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;
    RuntimeInitArgs init_args;
    char error_buf[128];
#if WASM_ENABLE_LOG != 0
    int log_verbose_level = 2;
#endif

    (void)arg1;
    (void)arg2;
    (void)arg3;

    os_printf("User mode thread: start\n");

    memset(&init_args, 0, sizeof(RuntimeInitArgs));

#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
#elif (defined(CONFIG_COMMON_LIBC_MALLOC)            \
       && CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE != 0) \
    || defined(CONFIG_NEWLIB_LIBC)
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
#else
#error "memory allocation scheme is not defined."
#endif

    /* initialize runtime environment */
    if (!wasm_runtime_full_init(&init_args)) {
        printf("Init runtime environment failed.\n");
        return;
    }

#if WASM_ENABLE_LOG != 0
    bh_log_set_verbose_level(log_verbose_level);
#endif

    /* load WASM byte buffer from byte buffer of include file */
    wasm_file_buf = (uint8 *)wasm_test_file;
    wasm_file_size = sizeof(wasm_test_file);

    /* load WASM module */
    if (!(wasm_module = wasm_runtime_load(wasm_file_buf, wasm_file_size,
                                          error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        goto fail1;
    }

    /* instantiate the module */
    if (!(wasm_module_inst = wasm_runtime_instantiate(
              wasm_module, CONFIG_APP_STACK_SIZE, CONFIG_APP_HEAP_SIZE,
              error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        goto fail2;
    }

    /* invoke the main function */
    app_instance_main(wasm_module_inst);

    /* destroy the module instance */
    wasm_runtime_deinstantiate(wasm_module_inst);

fail2:
    /* unload the module */
    wasm_runtime_unload(wasm_module);

fail1:
    /* destroy runtime environment */
    wasm_runtime_destroy();

    end = k_uptime_get_32();

    os_printf("User mode thread: elapsed %d\n", (end - start));
}
