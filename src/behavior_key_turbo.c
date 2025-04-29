/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_key_turbo

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define TURBO_BEHAVIOR_INST_BINDING(n, idx) \
    { \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, idx)), \
        .param1 = COND_CODE_0(DT_INST_PH_HAS_CELL_AT_IDX(n, bindings, idx, param1), (0), \
                           (DT_INST_PH_GET_BY_IDX(n, bindings, idx, param1))), \
        .param2 = COND_CODE_0(DT_INST_PH_HAS_CELL_AT_IDX(n, bindings, idx, param2), (0), \
                           (DT_INST_PH_GET_BY_IDX(n, bindings, idx, param2))), \
    },

#define TURBO_BEHAVIOR_INST_BINDINGS(n, prop) \
    { LISTIFY(DT_INST_PROP_LEN(n, prop), TURBO_BEHAVIOR_INST_BINDING, (, ), n) }

struct behavior_key_turbo_config {
    uint32_t tapping_term_ms;
    uint32_t pause_ms;
    uint32_t press_ms;
    struct zmk_behavior_binding behavior;
};

struct behavior_key_turbo_data {
    bool turbo_active;
    bool trigger_key_pressed;
    struct k_work_delayable start_turbo_work;
    struct k_work_delayable turbo_press_work;
    struct k_work_delayable turbo_release_work;
    uint32_t param;
};

static void reset_turbo_key(struct behavior_key_turbo_data *data) {
    LOG_DBG("stopping turbo");
    data->turbo_active = false;
    data->trigger_key_pressed = false;
    k_work_cancel_delayable(&data->start_turbo_work);
    k_work_cancel_delayable(&data->turbo_press_work);
    k_work_cancel_delayable(&data->turbo_release_work);
}

static void turbo_release_work_handler(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct behavior_key_turbo_data *data = CONTAINER_OF(work_delayable, 
                                                        struct behavior_key_turbo_data,
                                                        turbo_release_work);

    if (!data->turbo_active || !data->trigger_key_pressed) {
        return;
    }

    // Fix: Use parent container to access device
    const struct device *dev = k_work_delayable_from_work(work)->dev;
    const struct behavior_key_turbo_config *config = dev->config;
    
    // Release using configured behavior
    struct zmk_behavior_binding behavior_copy = config->behavior;
    behavior_copy.param1 = data->param;
    
    // Call release
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    behavior_keymap_binding_released(&behavior_copy, event);

    // Schedule next press after pause period
    k_work_schedule(&data->turbo_press_work, K_MSEC(config->pause_ms));
}

static void turbo_press_work_handler(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct behavior_key_turbo_data *data = CONTAINER_OF(work_delayable, 
                                                        struct behavior_key_turbo_data,
                                                        turbo_press_work);

    if (!data->turbo_active || !data->trigger_key_pressed) {
        return;
    }

    // Fix: Use parent container to access device
    const struct device *dev = k_work_delayable_from_work(work)->dev;
    const struct behavior_key_turbo_config *config = dev->config;

    // Press using configured behavior
    struct zmk_behavior_binding behavior_copy = config->behavior;
    behavior_copy.param1 = data->param;
    
    // Call press
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    behavior_keymap_binding_pressed(&behavior_copy, event);

    // Schedule key release
    k_work_schedule(&data->turbo_release_work, K_MSEC(config->press_ms));
}

static void start_turbo_work_handler(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct behavior_key_turbo_data *data = CONTAINER_OF(work_delayable, 
                                                        struct behavior_key_turbo_data,
                                                        start_turbo_work);

    if (!data->trigger_key_pressed) {
        return;
    }

    LOG_DBG("starting turbo sequence");
    data->turbo_active = true;
    
    // Start the turbo with first press
    k_work_schedule(&data->turbo_press_work, K_NO_WAIT);
}

static int on_key_turbo_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_key_turbo_data *data = dev->data;
    const struct behavior_key_turbo_config *config = dev->config;
    
    // Store the param for turbo replays
    data->param = binding->param1;

    // Reset any previous turbo and set up for potential new one
    reset_turbo_key(data);
    data->trigger_key_pressed = true;

    // Press using configured behavior
    struct zmk_behavior_binding behavior_copy = config->behavior;
    behavior_copy.param1 = binding->param1;
    
    // Call press
    behavior_keymap_binding_pressed(&behavior_copy, event);

    // Schedule the turbo to start after tapping term
    k_work_schedule(&data->start_turbo_work, K_MSEC(config->tapping_term_ms));
    
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_turbo_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_key_turbo_data *data = dev->data;
    const struct behavior_key_turbo_config *config = dev->config;
    
    data->trigger_key_pressed = false;

    if (data->turbo_active) {
        // If turbo was active, stop the turbo
        reset_turbo_key(data);
    }
    
    // Always release the key using configured behavior
    struct zmk_behavior_binding behavior_copy = config->behavior;
    behavior_copy.param1 = binding->param1;
    
    // Call release
    return behavior_keymap_binding_released(&behavior_copy, event);
}

static const struct behavior_driver_api behavior_key_turbo_driver_api = {
    .binding_pressed = on_key_turbo_binding_pressed,
    .binding_released = on_key_turbo_binding_released,
};

static int behavior_key_turbo_init(const struct device *dev) {
    struct behavior_key_turbo_data *data = dev->data;
    
    // Fix: Set device reference for work items
    k_work_init_delayable_for_dev(&data->start_turbo_work, dev, start_turbo_work_handler);
    k_work_init_delayable_for_dev(&data->turbo_press_work, dev, turbo_press_work_handler);
    k_work_init_delayable_for_dev(&data->turbo_release_work, dev, turbo_release_work_handler);

    return 0;
}

#define KT_INST(n)                                                                              \
    static struct behavior_key_turbo_data behavior_key_turbo_data_##n = {};                   \
    static struct behavior_key_turbo_config behavior_key_turbo_config_##n = {                 \
        .tapping_term_ms = DT_INST_PROP(n, tapping_term_ms),                                   \
        .pause_ms = DT_INST_PROP(n, pause_ms),                                                 \
        .press_ms = DT_INST_PROP(n, press_ms),                                                 \
        .behavior = TURBO_BEHAVIOR_INST_BINDINGS(n, bindings)[0],                              \
    };                                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_key_turbo_init, NULL, &behavior_key_turbo_data_##n,  \
                            &behavior_key_turbo_config_##n, POST_KERNEL,                       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_key_turbo_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KT_INST)

#endif
