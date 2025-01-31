#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <pax_codecs.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "audio.h"
#include "bootscreen.h"
#include "driver/uart.h"
#include "efuse.h"
#include "esp32/rom/crc.h"
#include "esp_ota_ops.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "factory_test.h"
#include "filesystems.h"
#include "fpga_download.h"
#include "fpga_test.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "managed_i2c.h"
#include "menu.h"
#include "menus/start.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rp2040_updater.h"
#include "rp2040bl.h"
#include "rtc_memory.h"
#include "sao_eeprom.h"
#include "settings.h"
#include "system_wrapper.h"
#include "webusb.h"
#include "wifi_cert.h"
#include "wifi_connection.h"
#include "wifi_defaults.h"
#include "wifi_ota.h"
#include "ws2812.h"

extern const uint8_t wallpaper_png_start[] asm("_binary_wallpaper_png_start");
extern const uint8_t wallpaper_png_end[] asm("_binary_wallpaper_png_end");

extern const uint8_t logo_screen_png_start[] asm("_binary_logo_screen_png_start");
extern const uint8_t logo_screen_png_end[] asm("_binary_logo_screen_png_end");

extern const uint8_t lattice_logo_png_start[] asm("_binary_m_logo_lattice_png_start");
extern const uint8_t lattice_logo_png_end[] asm("_binary_m_logo_lattice_png_end");

static const char* TAG = "main";

void display_fatal_error(pax_buf_t* pax_buffer, ILI9341* ili9341, const char* line0, const char* line1, const char* line2, const char* line3) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xa85a32);
    if (line0 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 23, 0, 20 * 0, line0);
    if (line1 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 1, line1);
    if (line2 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 2, line2);
    if (line3 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 3, line3);
    ili9341_write(ili9341, pax_buffer->buf);
}

void display_rp2040_crashed_message(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xf5ec42);
    pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Oops...");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 2, "The co-processor crashed, causing");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 3, "the badge to be restarted.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 5, "Help us debug the problem by");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 6, "submitting a ticket on Github");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 7, "explaining what caused the crash.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 9, "You can find the repository at:");
    pax_draw_text(pax_buffer, 0xFF000000, font, 12, 0, 20 * 10, "https://github.com/badgeteam\n/mch2022-firmware-rp2040    Press A to continue.");
    ili9341_write(ili9341, pax_buffer->buf);
    wait_for_button(buttonQueue);
}

bool display_rp2040_flash_lock_warning(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xf5ec42);
    pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Flashing attempt detected");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 1, "Hi there developer!");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 2, "You tried to overwrite the launcher");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 3, "firmware, are you sure you want to");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 4, "do that? We recommend you to");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 5, "install your app using the");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 6, "webusb_push.py tool.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 7, "Check out https://docs.badge.team");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 8, "for more information.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 12, 0, 20 * 10,
                  "You can disable this protection by pressing A\nTo continue starting without disabling the protection\npress B.");
    ili9341_write(ili9341, pax_buffer->buf);
    return wait_for_button(buttonQueue);
}

void display_rp2040_debug_message(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    const pax_font_t* font = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xf5ec42);
    pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Debug mode");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 2, "Co-processor is in debug mode");
    ili9341_write(ili9341, pax_buffer->buf);
    vTaskDelay(pdMS_TO_TICKS(500));
}

void stop() {
    ESP_LOGW(TAG, "*** HALTED ***");
    gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SD_PWR, 1);
    ws2812_init(GPIO_LED_DATA);
    uint8_t led_off[15]  = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t led_red[15]  = {0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0};
    uint8_t led_red2[15] = {0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0};
    while (true) {
        ws2812_send_data(led_red2, sizeof(led_red2));
        vTaskDelay(pdMS_TO_TICKS(200));
        ws2812_send_data(led_red, sizeof(led_red));
        vTaskDelay(pdMS_TO_TICKS(200));
        ws2812_send_data(led_off, sizeof(led_off));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

const char*      fatal_error_str = "A fatal error occured";
const char*      reset_board_str = "Reset the board to try again";
static pax_buf_t pax_buffer;

static void audio_player_task(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(200));
    play_bootsound();
    uint8_t leds[15] = {0};
    for (uint8_t part = 0; part < 50; part++) {
        // Center of the kite: green.
        leds[3 * 0 + 1] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Left of the kite: red.
        leds[3 * 1 + 0] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Top of the kit: blue.
        leds[3 * 2 + 2] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Right of the kite: yellow.
        leds[3 * 3 + 0] = part;
        leds[3 * 3 + 1] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Bottom of the kite: blue.
        leds[3 * 4 + 2] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Center of the kite: green.
        leds[3 * 0 + 1] = 49 - part;
        // Left of the kite: red.
        leds[3 * 1 + 0] = 49 - part;
        // Top of the kit: blue.
        leds[3 * 2 + 2] = 49 - part;
        // Right of the kite: yellow.
        leds[3 * 3 + 0] = 49 - part;
        leds[3 * 3 + 1] = 49 - part;
        // Bottom of the kite: blue.
        leds[3 * 4 + 2] = 49 - part;

        // Send the LED data.
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t res;

    audio_init();

    const esp_app_desc_t* app_description = esp_ota_get_app_description();
    ESP_LOGI(TAG, "App version: %s", app_description->version);
    // ESP_LOGI(TAG, "Project name: %s", app_description->project_name);

    /* Initialize GFX */
    pax_buf_init(&pax_buffer, NULL, ILI9341_WIDTH, ILI9341_HEIGHT, PAX_BUF_16_565RGB);

    /* Initialize hardware */

    efuse_protect();

    if (bsp_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize basic board support functions");
        esp_restart();
    }

    ILI9341* ili9341 = get_ili9341();
    if (ili9341 == NULL) {
        ESP_LOGE(TAG, "ili9341 is NULL");
        esp_restart();
    }

    /* Start NVS */
    res = nvs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", res);
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "NVS failed to initialize", "Flash may be corrupted", NULL);
        stop();
    }

    nvs_handle_t handle;
    res = nvs_open("system", NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", res);
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to open NVS namespace", "Flash may be corrupted", reset_board_str);
        stop();
    }

    display_boot_screen(&pax_buffer, ili9341, "Starting...");

    /* Initialize RP2040 co-processor */
    if (bsp_rp2040_init() != ESP_OK) {
        // This error state happens when
        //  - The I2C bus gets shorted out
        //  - The RP2040 does not boot
        ESP_LOGE(TAG, "Failed to initialize the RP2040 co-processor");
        const pax_font_t* font = pax_font_saira_regular;
        pax_background(&pax_buffer, 0xa85a32);
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 23, 0, 20 * 0, "Communication error");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 1, "The ESP32 is unable to communicate");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 2, "with the RP2040 co-processor, this");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 3, "could be caused by a problem with");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 4, "the I2C bus, which in turn can be");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 5, "caused by a connected SAO board or");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 6, "a connected QWIIC / Stemma QT device");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 8, "Please check the I2C bus and power");
        pax_draw_text(&pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 9, "cycle the badge to try again");
        ili9341_write(ili9341, pax_buffer.buf);
        stop();
    }

    RP2040* rp2040 = get_rp2040();

    rp2040_updater(rp2040, &pax_buffer, ili9341);  // Handle RP2040 firmware update & bootloader mode

    uint8_t crash_debug;
    if (rp2040_get_crash_state(rp2040, &crash_debug) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RP2040 crash & debug state");
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to communicate with", "the RP2040 co-processor", reset_board_str);
        stop();
    }

    bool rp2040_crashed = crash_debug & 0x01;
    bool rp2040_debug   = crash_debug & 0x02;

    if (rp2040_crashed) {
        display_rp2040_crashed_message(rp2040->queue, &pax_buffer, ili9341);
    }

    if (rp2040_debug) {
        display_rp2040_debug_message(&pax_buffer, ili9341);
    }

    factory_test(&pax_buffer, ili9341);

    /* Apply flashing lock */

    uint8_t prevent_flashing;
    res = nvs_get_u8(handle, "flash_lock", &prevent_flashing);
    if (res != ESP_OK) {
        prevent_flashing = true;
    }

    if (rp2040_set_reset_lock(rp2040, prevent_flashing) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write RP2040 flashing lock state");
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to communicate with", "the RP2040 co-processor", reset_board_str);
        stop();
    }

    /* Start FPGA driver */

    if (bsp_ice40_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the ICE40 FPGA");
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "A hardware failure occured", "while initializing the FPGA", reset_board_str);
        stop();
    }

    ICE40* ice40 = get_ice40();

    /* Start AppFS */
    res = appfs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "AppFS init failed: %d", res);
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to initialize AppFS", "Flash may be corrupted", reset_board_str);
        stop();
    }

    /* Start internal filesystem */
    if (mount_internal_filesystem() != ESP_OK) {
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to initialize flash FS", "Flash may be corrupted", reset_board_str);
        stop();
    }

    /* Start SD card filesystem */
    gpio_set_level(GPIO_SD_PWR, 1);  // Enable power to LEDs and SD card

    bool sdcard_mounted = (mount_sdcard_filesystem() == ESP_OK);
    if (sdcard_mounted) {
        ESP_LOGI(TAG, "SD card filesystem mounted");
    }

    ws2812_init(GPIO_LED_DATA);
    const uint8_t led_off[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ws2812_send_data(led_off, sizeof(led_off));

    /* Start WiFi */
    wifi_init();

    if (!wifi_check_configured()) {
        if (wifi_set_defaults()) {
            const pax_font_t* font = pax_font_saira_regular;
            pax_background(&pax_buffer, 0xFFFFFF);
            pax_draw_text(&pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰 continue");
            render_message(&pax_buffer, "Default WiFi settings\nhave been restored!\nPress A to continue...");
            ili9341_write(ili9341, pax_buffer.buf);
            wait_for_button(rp2040->queue);
        } else {
            display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to configure WiFi", "Flash may be corrupted", reset_board_str);
            stop();
        }
    }

    res = init_ca_store();
    if (res != ESP_OK) {
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to initialize", "TLS certificate storage", reset_board_str);
        stop();
    }

    /* Clear RTC memory */
    rtc_memory_clear();

    /* Check WebUSB mode */

    uint8_t webusb_mode;
    res = rp2040_get_webusb_mode(rp2040, &webusb_mode);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WebUSB mode: %d", res);
        display_fatal_error(&pax_buffer, ili9341, fatal_error_str, "Failed to read WebUSB mode", NULL, NULL);
        stop();
    }

    ESP_LOGI(TAG, "WebUSB mode 0x%02X", webusb_mode);

    if (webusb_mode == 0x00) {  // Normal boot
        if (prevent_flashing) {
            uint8_t attempted;
            if (rp2040_get_reset_attempted(rp2040, &attempted) == ESP_OK) {
                if (attempted) {
                    rp2040_set_reset_attempted(rp2040, false);
                    ESP_LOGE(TAG, "Detected esptool.py style reset while flash lock is active");
                    bool disable_lock = display_rp2040_flash_lock_warning(rp2040->queue, &pax_buffer, ili9341);
                    if (disable_lock) {
                        nvs_set_u8(handle, "flash_lock", 0);
                        nvs_commit(handle);
                        rp2040_set_reset_lock(rp2040, 0);
                    }
                    esp_restart();
                }
            }
        } else {
            ESP_LOGW(TAG, "Flashing using esptool.py is allowed");
        }

        /* Sponsors check */
        uint8_t sponsors;
        res = nvs_get_u8(handle, "sponsors", &sponsors);
        if ((res != ESP_OK) || (sponsors < 1)) {
            appfs_handle_t appfs_fd = appfsOpen("sponsors");
            if (appfs_fd != APPFS_INVALID_FD) {
                appfs_boot_app(appfs_fd);
                stop();
            } else {
                ESP_LOGW(TAG, "Sponsors app not installed while sponsors should have been shown");
            }
        }

        /* Lattice check */
        uint8_t lattice;
        res = nvs_get_u8(handle, "lattice", &lattice);
        if ((res != ESP_OK) || (lattice < 1)) {
            pax_background(&pax_buffer, 0x000000);
            for (uint8_t i = 0; i < 250; i += 4) {
                pax_background(&pax_buffer, 0x000000);
                pax_center_text(&pax_buffer, 0xFF000000 | (i << 16) | (i << 8) | (i), pax_font_saira_regular, 18, pax_buffer.width / 2,
                                pax_buffer.height / 2 - 18, "In 2017 we managed to\nforget a sponsor...");
                ili9341_write(ili9341, pax_buffer.buf);
            }
            for (uint8_t i = 0; i < 250; i += 4) {
                uint8_t j = 254 - i;
                pax_background(&pax_buffer, 0x000000);
                pax_center_text(&pax_buffer, 0xFF000000 | (j << 16) | (j << 8) | (j), pax_font_saira_regular, 18, pax_buffer.width / 2,
                                pax_buffer.height / 2 - 18, "In 2017 we managed to\nforget a sponsor...");
                ili9341_write(ili9341, pax_buffer.buf);
            }
            for (uint8_t i = 0; i < 250; i += 4) {
                pax_background(&pax_buffer, 0x000000);
                pax_center_text(&pax_buffer, 0xFF000000 | (i << 16) | (i << 8) | (i), pax_font_saira_regular, 18, pax_buffer.width / 2,
                                pax_buffer.height / 2 - 18, "This time we managed to\ninclude the wrong company...");
                ili9341_write(ili9341, pax_buffer.buf);
            }
            for (uint8_t i = 0; i < 250; i += 4) {
                uint8_t j = 254 - i;
                pax_background(&pax_buffer, 0x000000);
                pax_center_text(&pax_buffer, 0xFF000000 | (j << 16) | (j << 8) | (j), pax_font_saira_regular, 18, pax_buffer.width / 2,
                                pax_buffer.height / 2 - 18, "This time we managed to\ninclude the wrong company...");
                ili9341_write(ili9341, pax_buffer.buf);
            }
            for (uint8_t i = 0; i < 250; i += 4) {
                pax_background(&pax_buffer, 0x000000);
                pax_center_text(&pax_buffer, 0xFF000000 | (i << 16) | (i << 8) | (i), pax_font_saira_regular, 18, pax_buffer.width / 2,
                                pax_buffer.height / 2 - 27, "Lattice Semiconductor\nthank you for sponsoring the\nICE40UP5K FPGA");
                ili9341_write(ili9341, pax_buffer.buf);
            }
            for (uint8_t i = 0; i < 250; i += 4) {
                uint8_t j = 254 - i;
                pax_background(&pax_buffer, 0x000000);
                pax_center_text(&pax_buffer, 0xFF000000 | (j << 16) | (j << 8) | (j), pax_font_saira_regular, 18, pax_buffer.width / 2,
                                pax_buffer.height / 2 - 27, "Lattice Semiconductor\nthank you for sponsoring the\nICE40UP5K FPGA");
                ili9341_write(ili9341, pax_buffer.buf);
            }
            pax_insert_png_buf(&pax_buffer, lattice_logo_png_start, lattice_logo_png_end - lattice_logo_png_start, 0, 0, 0);
            pax_draw_text(&pax_buffer, 0xFF000000, pax_font_saira_regular, 18, 5, 240 - 18, "🅰 continue");
            ili9341_write(ili9341, pax_buffer.buf);
            wait_for_button(rp2040->queue);
            nvs_set_u8(handle, "lattice", 1);
        }

        /* Crash check */
        appfs_handle_t crashed_app = appfs_detect_crash();
        if (crashed_app != APPFS_INVALID_FD) {
            const char* app_name = NULL;
            appfsEntryInfo(crashed_app, &app_name, NULL);
            pax_background(&pax_buffer, 0xFFFFFF);
            render_header(&pax_buffer, 0, 0, pax_buffer.width, 34, 18, 0xFFfa448c, 0xFF491d88, NULL, "App crashed");
            pax_draw_text(&pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52, "Failed to start app,");
            pax_draw_text(&pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 20, "check console for more details.");
            if (app_name != NULL) {
                char buffer[64];
                buffer[sizeof(buffer) - 1] = '\0';
                snprintf(buffer, sizeof(buffer) - 1, "App: %s", app_name);
                pax_draw_text(&pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 40, buffer);
            }
            pax_draw_text(&pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, pax_buffer.height - 18, "🅰 continue");
            ili9341_write(ili9341, pax_buffer.buf);
            wait_for_button(rp2040->queue);
        }

        /* Rick that roll */
        xTaskCreate(audio_player_task, "audio_player_task", 2048, NULL, 12, NULL);

        /* Launcher menu */
        while (true) {
            menu_start(rp2040->queue, &pax_buffer, ili9341, app_description->version);
        }
    } else if (webusb_mode == 0x01) {
        webusb_main(rp2040->queue, &pax_buffer, ili9341);
    } else if (webusb_mode == 0x02) {
        display_boot_screen(&pax_buffer, ili9341, "FPGA download mode");
        while (true) {
            fpga_download(rp2040->queue, ice40, &pax_buffer, ili9341);
        }
    } else if (webusb_mode == 0x03) {
        webusb_new_main(rp2040->queue, &pax_buffer, ili9341);
    } else {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Invalid mode 0x%02X", webusb_mode);
        display_boot_screen(&pax_buffer, ili9341, buffer);
    }

    nvs_close(handle);
}
