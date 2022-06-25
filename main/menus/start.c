#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "dev.h"
#include "hardware.h"
#include "hatchery.h"
#include "ili9341.h"
#include "launcher.h"
#include "math.h"
#include "menu.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "settings.h"
#include "nametag.h"
#include "rtc_memory.h"

extern const uint8_t home_png_start[] asm("_binary_home_png_start");
extern const uint8_t home_png_end[] asm("_binary_home_png_end");

extern const uint8_t apps_png_start[] asm("_binary_apps_png_start");
extern const uint8_t apps_png_end[] asm("_binary_apps_png_end");

extern const uint8_t hatchery_png_start[] asm("_binary_hatchery_png_start");
extern const uint8_t hatchery_png_end[] asm("_binary_hatchery_png_end");

extern const uint8_t dev_png_start[] asm("_binary_dev_png_start");
extern const uint8_t dev_png_end[] asm("_binary_dev_png_end");

extern const uint8_t settings_png_start[] asm("_binary_settings_png_start");
extern const uint8_t settings_png_end[] asm("_binary_settings_png_end");

extern const uint8_t tag_png_start[] asm("_binary_tag_png_start");
extern const uint8_t tag_png_end[] asm("_binary_tag_png_end");

extern const uint8_t python_png_start[] asm("_binary_python_png_start");
extern const uint8_t python_png_end[] asm("_binary_python_png_end");

extern const uint8_t bitstream_png_start[] asm("_binary_bitstream_png_start");
extern const uint8_t bitstream_png_end[] asm("_binary_bitstream_png_end");

typedef enum action { ACTION_NONE, ACTION_APPS, ACTION_HATCHERY, ACTION_NAMETAG, ACTION_DEV, ACTION_SETTINGS, ACTION_PYTHON, ACTION_FPGA } menu_start_action_t;

void render_start_help(pax_buf_t* pax_buffer, const char* text) {
    const pax_font_t* font = pax_get_font("saira regular");
    pax_background(pax_buffer, 0xFFFFFF);
    pax_noclip(pax_buffer);
    pax_draw_text(pax_buffer, 0xFF491d88, font, 18, 5, 240 - 18, "[A] accept");
    pax_vec1_t version_size = pax_text_size(font, 18, text);
    pax_draw_text(pax_buffer, 0xFF491d88, font, 18, 320 - 5 - version_size.x, 240 - 18, text);
}

void menu_start(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* version) {
    menu_t* menu = menu_alloc("Main menu", 34, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFF000000;
    menu->selectedItemColor = 0xFFfec859;
    menu->borderColor       = 0xFF491d88;
    menu->titleColor        = 0xFFfec859;
    menu->titleBgColor      = 0xFF491d88;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    pax_buf_t icon_home;
    pax_decode_png_buf(&icon_home, (void*) home_png_start, home_png_end - home_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_apps;
    pax_decode_png_buf(&icon_apps, (void*) apps_png_start, apps_png_end - apps_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_hatchery;
    pax_decode_png_buf(&icon_hatchery, (void*) hatchery_png_start, hatchery_png_end - hatchery_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_dev;
    pax_decode_png_buf(&icon_dev, (void*) dev_png_start, dev_png_end - dev_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_settings;
    pax_decode_png_buf(&icon_settings, (void*) settings_png_start, settings_png_end - settings_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_tag;
    pax_decode_png_buf(&icon_tag, (void*) tag_png_start, tag_png_end - tag_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_python;
    pax_decode_png_buf(&icon_python, (void*) python_png_start, python_png_end - python_png_start, PAX_BUF_32_8888ARGB, 0);
    pax_buf_t icon_bitstream;
    pax_decode_png_buf(&icon_bitstream, (void*) bitstream_png_start, bitstream_png_end - bitstream_png_start, PAX_BUF_32_8888ARGB, 0);

    menu_set_icon(menu, &icon_home);
    menu_insert_item_icon(menu, "Name tag", NULL, (void*) ACTION_NAMETAG, -1, &icon_tag);
    menu_insert_item_icon(menu, "ESP32 apps", NULL, (void*) ACTION_APPS, -1, &icon_apps);
    menu_insert_item_icon(menu, "FPGA apps", NULL, (void*) ACTION_FPGA, -1, &icon_bitstream);
    menu_insert_item_icon(menu, "BadgePython apps", NULL, (void*) ACTION_PYTHON, -1, &icon_python);
    menu_insert_item_icon(menu, "Hatchery", NULL, (void*) ACTION_HATCHERY, -1, &icon_hatchery);
    menu_insert_item_icon(menu, "Development tools", NULL, (void*) ACTION_DEV, -1, &icon_dev);
    menu_insert_item_icon(menu, "Settings", NULL, (void*) ACTION_SETTINGS, -1, &icon_settings);

    bool                render = true;
    menu_start_action_t action = ACTION_NONE;

    uint8_t analogReadTimer = 0;
    float   battery_voltage = 0;
    float   usb_voltage     = 0;
    // uint8_t rp2040_usb = 0;

    // Calculated:
    uint8_t battery_percent  = 0;
    bool    battery_charging = false;

    RP2040* rp2040 = get_rp2040();

    while (1) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            switch (pin) {
                case RP2040_INPUT_JOYSTICK_DOWN:
                    if (value) {
                        menu_navigate_next(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_JOYSTICK_UP:
                    if (value) {
                        menu_navigate_previous(menu);
                        render = true;
                    }
                    break;
                case RP2040_INPUT_BUTTON_ACCEPT:
                case RP2040_INPUT_JOYSTICK_PRESS:
                case RP2040_INPUT_BUTTON_SELECT:
                case RP2040_INPUT_BUTTON_START:
                    if (value) {
                        action = (menu_start_action_t) menu_get_callback_args(menu, menu_get_position(menu));
                    }
                    break;
                default:
                    break;
            }
        }

        if (analogReadTimer > 0) {
            analogReadTimer--;
        } else {
            analogReadTimer = 10;  // No need to update these values really quickly
            if (rp2040_read_vbat(rp2040, &battery_voltage) != ESP_OK) {
                battery_voltage = 0;
            }
            if (rp2040_read_vusb(rp2040, &usb_voltage) != ESP_OK) {
                usb_voltage = 0;
            }

            if (battery_voltage >= 3.6) {
                battery_percent = ((battery_voltage - 3.6) * 100) / (4.15 - 3.6);
                if (battery_percent > 100) battery_percent = 100;
            } else {
                battery_percent = 0;
            }

            battery_charging = (usb_voltage > 4.0) && (battery_percent < 100);

            render = true;
        }

        if (render) {
            char textBuffer[64];
            snprintf(textBuffer, sizeof(textBuffer), "%u%%%c (%1.1fv) v%s", battery_percent, battery_charging ? '+' : ' ', battery_voltage, version);
            render_start_help(pax_buffer, textBuffer);
            menu_render(pax_buffer, menu, 0, 0, 320, 220, 0xFF491d88);
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_APPS) {
                menu_launcher(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_HATCHERY) {
                menu_hatchery(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_NAMETAG) {
                show_nametag(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_SETTINGS) {
                menu_settings(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_DEV) {
                menu_dev(buttonQueue, pax_buffer, ili9341);
            } else if (action == ACTION_PYTHON) {
                // Test
                appfs_handle_t appfs_fd = appfsOpen("python");
                if (appfs_fd != APPFS_INVALID_FD) {
                    rtc_memory_string_write("dashboard.other.about");
                    appfs_boot_app(appfs_fd);
                } else {
                    printf("Python not installed, can't start BadgePython app!\n");
                }
            }
            action = ACTION_NONE;
            render = true;
        }
    }

    menu_free(menu);
    pax_buf_destroy(&icon_home);
    pax_buf_destroy(&icon_apps);
    pax_buf_destroy(&icon_hatchery);
    pax_buf_destroy(&icon_dev);
    pax_buf_destroy(&icon_settings);
}
