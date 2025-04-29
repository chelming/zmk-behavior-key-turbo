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

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_key_turbo_config {
    uint8_t index;
    uint32_t delay_ms;
    uint32_t tempo_ms;
    uint32_t hold_ms;
    uint8_t usage_pages_count;
    uint16_t usage_pages[];
};

struct behavior_key_turbo_data {
    struct zmk_keycode_state_changed last_keycode_pressed;
    struct zmk_keycode_state_changed current_keycode_pressed;
    bool turbo_active;
    bool trigger_key_pressed;
    struct k_work_delayable start_turbo_work;
    struct k_work_delayable turbo_press_work;
    struct k_work_delayable turbo_release_work;
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

    const struct device *dev = CONTAINER_OF(data, struct device, data);
    const struct behavior_key_turbo_config *config = dev->config;

    // Send key release event
    data->current_keycode_pressed.timestamp = k_uptime_get();
    data->current_keycode_pressed.state = false;
    raise_zmk_keycode_state_changed(data->current_keycode_pressed);

    // Schedule next press after wait period (tempo_ms minus hold_ms)
    uint32_t wait_ms = config->tempo_ms - config->hold_ms;
    if (wait_ms > 0) {
        k_work_schedule(&data->turbo_press_work, K_MSEC(wait_ms));
    } else {
        // If hold_ms >= tempo_ms, schedule press immediately
        k_work_schedule(&data->turbo_press_work, K_NO_WAIT);
    }
}

static void turbo_press_work_handler(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct behavior_key_turbo_data *data = CONTAINER_OF(work_delayable, 
                                                        struct behavior_key_turbo_data,
                                                        turbo_press_work);

    if (!data->turbo_active || !data->trigger_key_pressed) {
        return;
    }

    const struct device *dev = CONTAINER_OF(data, struct device, data);
    const struct behavior_key_turbo_config *config = dev->config;

    // Send key press event
    memcpy(&data->current_keycode_pressed, &data->last_keycode_pressed,
           sizeof(struct zmk_keycode_state_changed));
    data->current_keycode_pressed.timestamp = k_uptime_get();
    data->current_keycode_pressed.state = true;
    raise_zmk_keycode_state_changed(data->current_keycode_pressed);

    // Schedule key release
    k_work_schedule(&data->turbo_release_work, K_MSEC(config->hold_ms));
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

    if (data->last_keycode_pressed.usage_page == 0) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    // Reset any previous turbo and set up for potential new one
    reset_turbo_key(data);
    data->trigger_key_pressed = true;

    // Store current key for turbo playback
    memcpy(&data->current_keycode_pressed, &data->last_keycode_pressed,
           sizeof(struct zmk_keycode_state_changed));
    data->current_keycode_pressed.timestamp = k_uptime_get();
    
    // Send initial key press event immediately
    raise_zmk_keycode_state_changed(data->current_keycode_pressed);

    // Schedule the turbo to start after delay
    k_work_schedule(&data->start_turbo_work, K_MSEC(config->delay_ms));
    
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_turbo_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_key_turbo_data *data = dev->data;

    data->trigger_key_pressed = false;

    if (data->turbo_active) {
        // If turbo was active, stop the turbo
        reset_turbo_key(data);
    } else {
        // Key was released before turbo started, just send release event
        data->current_keycode_pressed.timestamp = k_uptime_get();
        data->current_keycode_pressed.state = false;
        raise_zmk_keycode_state_changed(data->current_keycode_pressed);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_key_turbo_driver_api = {
    .binding_pressed = on_key_turbo_binding_pressed,
    .binding_released = on_key_turbo_binding_released,
};

static const struct device *devs[DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)];

static int key_turbo_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT); i++) {
        const struct device *dev = devs[i];
        if (dev == NULL) {
            continue;
        }

        struct behavior_key_turbo_data *data = dev->data;
        const struct behavior_key_turbo_config *config = dev->config;

        for (int u = 0; u < config->usage_pages_count; u++) {
            if (config->usage_pages[u] == ev->usage_page) {
                memcpy(&data->last_keycode_pressed, ev, sizeof(struct zmk_keycode_state_changed));
                data->last_keycode_pressed.implicit_modifiers |= zmk_hid_get_explicit_mods();
                break;
            }
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_key_turbo, key_turbo_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_key_turbo, zmk_keycode_state_changed);

static int behavior_key_turbo_init(const struct device *dev) {
    const struct behavior_key_turbo_config *config = dev->config;
    devs[config->index] = dev;

    struct behavior_key_turbo_data *data = dev->data;
    k_work_init_delayable(&data->start_turbo_work, start_turbo_work_handler);
    k_work_init_delayable(&data->turbo_press_work, turbo_press_work_handler);
    k_work_init_delayable(&data->turbo_release_work, turbo_release_work_handler);

    return 0;
}

#define KT_INST(n)                                                                              \
    static struct behavior_key_turbo_data behavior_key_turbo_data_##n = {};                   \
    static struct behavior_key_turbo_config behavior_key_turbo_config_##n = {                 \
        .index = n,                                                                             \
        .delay_ms = DT_INST_PROP(n, delay_ms),                                                 \
        .tempo_ms = DT_INST_PROP(n, tempo_ms),                                                 \
        .hold_ms = DT_INST_PROP(n, hold_ms),                                                   \
        .usage_pages = DT_INST_PROP(n, usage_pages),                                           \
        .usage_pages_count = DT_INST_PROP_LEN(n, usage_pages),                                 \
    };                                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_key_turbo_init, NULL, &behavior_key_turbo_data_##n,  \
                            &behavior_key_turbo_config_##n, POST_KERNEL,                       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_key_turbo_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KT_INST)
