#include "who_lcd.h"
#include <string.h>
#if CONFIG_MPYTHON_PRO_BOARD
#include "logo_mpython_pro_320x172_lcd.h"
#elif CONFIG_LABPLUS_LEDONG_V2_BOARD
#include "logo_labplus_ledong_v2_320x172_lcd.h"
#elif CONFIG_LABPLUS_XUNFEI_JS_PRIMARY_BOARD || CONFIG_LABPLUS_XUNFEI_JS_MIDDLE_BOARD
#include "logo_xunfei_320x172_lcd.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_jd9853.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_camera.h"
#include "driver/i2c.h"

static const char *TAG = "who_lcd";

bool is_lcd_init = false;
lcd_t *lcd = NULL;

#if CONFIG_LABPLUS_LEDONG_V2_BOARD || CONFIG_LABPLUS_XUNFEI_JS_PRIMARY_BOARD
static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static bool gReturnFB = true;
#endif

static bool on_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lcd_t *lcd = (lcd_t *) user_ctx;

    if (lcd->transfer_done_cb != NULL){
        lcd->transfer_done_cb(lcd->transfer_done_user_data);
    }

    return false;
}

static void lcd_SPI_init(void)
{
    spi_bus_config_t bus_conf = {
        .sclk_io_num = BOARD_LCD_SCK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_conf, SPI_DMA_CH_AUTO));
    lcd->bus_initialized = true;
}

static void lcd_SPI_deinit(void)
{
    if (lcd->bus_initialized){
        ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));
        lcd->bus_initialized = false;
    }
}

esp_err_t lcd_init(void)
{
    if(!lcd){
        lcd = calloc(1, sizeof(lcd_t));

        lcd_SPI_init();
        lcd->bus_initialized = true;

        esp_lcd_panel_io_spi_config_t io_config = {
            .dc_gpio_num = BOARD_LCD_DC,
            .cs_gpio_num = BOARD_LCD_CS,
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .lcd_cmd_bits = BOARD_LCD_CMD_BITS,
            .lcd_param_bits = BOARD_LCD_PARAM_BITS,
            .spi_mode = 0,
            .trans_queue_depth = 10,
            .on_color_trans_done = on_color_trans_done_cb,
            .user_ctx = lcd,
        };
    
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &lcd->io_handle));
    
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = BOARD_LCD_RST,
            .rgb_ele_order = BOARD_LCD_RGB_ELE_ORDER,
            .bits_per_pixel = 16,
        };

        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = 44,
            .scl_io_num = 43,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = 400000,
        };

        i2c_param_config(0, &conf);
        i2c_driver_install(0, conf.mode, 0, 0, 0);
        uint8_t reg = BOARD_STM8_CMD;
        i2c_master_write_to_device(0, BOARD_STM8_ADDR, &reg, 1, 1000 / portTICK_PERIOD_MS);
        i2c_driver_delete(0);

        ESP_ERROR_CHECK(esp_lcd_new_panel_jd9853(lcd->io_handle, &panel_config, &lcd->panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd->panel));
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_ERROR_CHECK(esp_lcd_panel_init(lcd->panel));
    
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcd->panel, BOARD_LCD_INVERT));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(lcd->panel, BOARD_LCD_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd->panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(lcd->panel, BOARD_LCD_GAP_X, BOARD_LCD_GAP_Y));
    
        lcd_set_color(GUI_Black);
    
        if (BOARD_LCD_BL >= 0) {
            gpio_config_t io_conf = {
                .mode = GPIO_MODE_OUTPUT,
                .pin_bit_mask = 1ULL << BOARD_LCD_BL,
            };
            gpio_config(&io_conf);
            gpio_set_level(BOARD_LCD_BL, 1);
        }
    
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd->panel, true));
    }

    return ESP_OK;
}

esp_err_t lcd_deinit(void)
{
    if(lcd){
        if(lcd->panel != NULL){
            ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd->panel, false));
            ESP_ERROR_CHECK(esp_lcd_panel_del(lcd->panel));
            lcd->panel = NULL;
        }
    
        if(lcd->io_handle != NULL){
            ESP_ERROR_CHECK(esp_lcd_panel_io_del(lcd->io_handle));
            lcd->io_handle = NULL;
            lcd_SPI_deinit();
        }

        free(lcd);

        if (BOARD_LCD_BL >= 0) {
            gpio_set_level(BOARD_LCD_BL, 0);
        }
    }

    return ESP_OK;
}

void lcd_draw_logo(void)
{
    uint16_t *pixels = (uint16_t *)heap_caps_malloc((logo_en_320x172_lcd_width * logo_en_320x172_lcd_height) * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (NULL == pixels)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
        return;
    }
    memcpy(pixels, logo_en_320x172_lcd, (logo_en_320x172_lcd_width * logo_en_320x172_lcd_height) * sizeof(uint16_t));
    esp_lcd_panel_draw_bitmap(lcd->panel, 0, 0, logo_en_320x172_lcd_width, logo_en_320x172_lcd_height, (uint16_t *)pixels);
    heap_caps_free(pixels);
}

void lcd_set_color(int color)
{
    uint16_t *buffer = (uint16_t *)malloc(BOARD_LCD_H_RES * sizeof(uint16_t));
    if (NULL == buffer){
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
    }
    else{
        for (size_t i = 0; i < BOARD_LCD_H_RES; i++){
            buffer[i] = color;
        }

        for (int y = 0; y < BOARD_LCD_V_RES; y++){
            esp_lcd_panel_draw_bitmap(lcd->panel, 0, y, BOARD_LCD_H_RES, y+1, buffer);
        }

        free(buffer);
    }
}

void lcd_draw_image(int x, int y, int width, int height, const void *buff)
{
    esp_lcd_panel_draw_bitmap(lcd->panel, x, y, (width > 320)? 320 : width, (height > 172)? 172 : height, (uint16_t *)buff);
}

lcd_t *get_lcd_handle(void)
{
    return lcd;
}

#if CONFIG_LABPLUS_LEDONG_V2_BOARD || CONFIG_LABPLUS_XUNFEI_JS_PRIMARY_BOARD
static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;

    while (true){
        if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY)){
            esp_lcd_panel_draw_bitmap(lcd->panel, 0, 0, (frame->width > 320)? 320 : frame->width, (frame->height > 172)? 172 : frame->height, (uint16_t *)frame->buf);
            if (xQueueFrameO){
                xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
            }else if (gReturnFB){
                esp_camera_fb_return(frame);
            }else{
                free(frame);
            }
        }
    }
}

esp_err_t register_lcd(const QueueHandle_t frame_i, const QueueHandle_t frame_o, const bool return_fb)
{
    lcd_init();

    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    gReturnFB = return_fb;
    xTaskCreatePinnedToCore(task_process_handler, TAG, 4 * 1024, NULL, 5, NULL, 0);

    return ESP_OK;
}
#endif