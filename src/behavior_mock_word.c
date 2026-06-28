/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * mock_word: behaves like caps_word, but each alphabetic key is randomly
 * capitalized instead of always capitalized -- i.e. sPoNgEbOb mOcKiNg case.
 * Derived from ZMK's behavior_caps_word.c (v0.3.0).
 */

#define DT_DRV_COMPAT zmk_behavior_mock_word

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct mock_word_continue_item {
    uint16_t page;
    uint32_t id;
    uint8_t implicit_modifiers;
};

struct behavior_mock_word_config {
    zmk_mod_flags_t mods;
    uint8_t continuations_count;
    struct mock_word_continue_item continuations[];
};

struct behavior_mock_word_data {
    bool active;
    uint32_t rng_state;
};

// xorshift32 PRNG -- cheap, no entropy subsystem dependency. Seeded from the
// CPU cycle counter on each activation so the pattern differs every time.
static uint32_t mock_word_rand(struct behavior_mock_word_data *data) {
    uint32_t x = data->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    data->rng_state = x;
    return x;
}

static void activate_mock_word(const struct device *dev) {
    struct behavior_mock_word_data *data = dev->data;

    // Seed with the cycle counter; force non-zero (xorshift dies at 0).
    data->rng_state = k_cycle_get_32() | 1u;
    data->active = true;
}

static void deactivate_mock_word(const struct device *dev) {
    struct behavior_mock_word_data *data = dev->data;

    data->active = false;
}

static int on_mock_word_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_mock_word_data *data = dev->data;

    if (data->active) {
        deactivate_mock_word(dev);
    } else {
        activate_mock_word(dev);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_mock_word_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_mock_word_driver_api = {
    .binding_pressed = on_mock_word_binding_pressed,
    .binding_released = on_mock_word_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

static int mock_word_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_mock_word, mock_word_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_mock_word, zmk_keycode_state_changed);

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static bool mock_word_is_continuelist(const struct behavior_mock_word_config *config,
                                      uint16_t usage_page, uint8_t usage_id,
                                      uint8_t implicit_modifiers) {
    for (int i = 0; i < config->continuations_count; i++) {
        const struct mock_word_continue_item *continuation = &config->continuations[i];

        if (continuation->page == usage_page && continuation->id == usage_id &&
            (continuation->implicit_modifiers &
             (implicit_modifiers | zmk_hid_get_explicit_mods())) ==
                continuation->implicit_modifiers) {
            return true;
        }
    }

    return false;
}

static bool mock_word_is_alpha(uint8_t usage_id) {
    return (usage_id >= HID_USAGE_KEY_KEYBOARD_A && usage_id <= HID_USAGE_KEY_KEYBOARD_Z);
}

static bool mock_word_is_numeric(uint8_t usage_id) {
    return (usage_id >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
            usage_id <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
}

static void mock_word_enhance_usage(const struct behavior_mock_word_config *config,
                                    struct behavior_mock_word_data *data,
                                    struct zmk_keycode_state_changed *ev) {
    if (ev->usage_page != HID_USAGE_KEY || !mock_word_is_alpha(ev->keycode)) {
        return;
    }

    // Coin flip: capitalize this letter or not.
    if (mock_word_rand(data) & 1u) {
        ev->implicit_modifiers |= config->mods;
    }
}

static int mock_word_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];

        struct behavior_mock_word_data *data = dev->data;
        if (!data->active) {
            continue;
        }

        const struct behavior_mock_word_config *config = dev->config;

        mock_word_enhance_usage(config, data, ev);

        if (!mock_word_is_alpha(ev->keycode) && !mock_word_is_numeric(ev->keycode) &&
            !is_mod(ev->usage_page, ev->keycode) &&
            !mock_word_is_continuelist(config, ev->usage_page, ev->keycode,
                                       ev->implicit_modifiers)) {
            LOG_DBG("Deactivating mock_word for 0x%02X - 0x%02X", ev->usage_page, ev->keycode);
            deactivate_mock_word(dev);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define PARSE_BREAK(i)                                                                              \
    {.page = ZMK_HID_USAGE_PAGE(i), .id = ZMK_HID_USAGE_ID(i), .implicit_modifiers = SELECT_MODS(i)}

#define BREAK_ITEM(i, n) PARSE_BREAK(DT_INST_PROP_BY_IDX(n, continue_list, i))

#define KP_INST(n)                                                                                 \
    static struct behavior_mock_word_data behavior_mock_word_data_##n = {.active = false,           \
                                                                         .rng_state = 1u};          \
    static const struct behavior_mock_word_config behavior_mock_word_config_##n = {                 \
        .mods = DT_INST_PROP_OR(n, mods, MOD_LSFT),                                                 \
        .continuations = {LISTIFY(DT_INST_PROP_LEN(n, continue_list), BREAK_ITEM, (, ), n)},        \
        .continuations_count = DT_INST_PROP_LEN(n, continue_list),                                  \
    };                                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_mock_word_data_##n,                            \
                            &behavior_mock_word_config_##n, POST_KERNEL,                            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_mock_word_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif
