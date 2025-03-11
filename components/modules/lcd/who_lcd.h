#pragma once

#include <stdint.h>
#include "esp_log.h"
#include "esp_event.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_camera.h"

#define BOARD_LCD_MOSI 37
#define BOARD_LCD_MISO -1
#define BOARD_LCD_SCK 36
#define BOARD_LCD_CS -1
#define BOARD_LCD_DC 35
#define BOARD_LCD_RST -1
#define BOARD_LCD_BL -1
#define BOARD_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define BOARD_LCD_BK_LIGHT_ON_LEVEL 0
#define BOARD_LCD_BK_LIGHT_OFF_LEVEL !BOARD_LCD_BK_LIGHT_ON_LEVEL
#define BOARD_LCD_H_RES 320
#define BOARD_LCD_V_RES 172
#define BOARD_LCD_CMD_BITS 8
#define BOARD_LCD_PARAM_BITS 8
#define LCD_HOST SPI2_HOST

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t display_init(void);
    void display_task_begin(const QueueHandle_t frame_i, const QueueHandle_t frame_o, const bool return_fb);
    void display_draw_logo();
    void display_set_color(int color);
    void display_draw_image(camera_fb_t *frame);

#ifdef __cplusplus
}
#endif
