#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ssd1306_spi.h"
#include "font5x7.h"

static const char *TAG = "SSD1306_SPI";

// ตัวแปร Handle ของบัส SPI
static spi_device_handle_t s_spi_handle = NULL;

// หน่วยความจำจำลอง Framebuffer ขนาด 1 KByte (1,024 ไบต์)
static uint8_t s_oled_buffer[OLED_BUFFER_SIZE];

// ============================================================================
// ฟังก์ชันส่งข้อมูลผ่านฮาร์ดแวร์ SPI
// ============================================================================

void oled_send_cmd(uint8_t cmd)
{
    // ดึงขา DC เป็น 0 เพื่อบอกชิปว่านี่คือ "คำสั่ง (Command)"
    gpio_set_level(OLED_PIN_DC, 0);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8; // 8 บิต (1 ไบต์)
    t.tx_buffer = &cmd;

    spi_device_polling_transmit(s_spi_handle, &t);
}

void oled_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;

    // ดึงขา DC เป็น 1 เพื่อบอกชิปว่านี่คือ "ข้อมูลภาพลงแรม (GDDRAM Data)"
    gpio_set_level(OLED_PIN_DC, 1);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8; // จำนวนบิต
    t.tx_buffer = data;

    spi_device_polling_transmit(s_spi_handle, &t);
}

void oled_hardware_reset(void)
{
    ESP_LOGI(TAG, "กำลังทำ Hardware Reset ผ่านขา RES (GPIO %d)...", OLED_PIN_RES);
    gpio_set_level(OLED_PIN_RES, 0); // ดึง LOW เพื่อเริ่มรีเซ็ต
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(OLED_PIN_RES, 1); // ดึง HIGH กลับเพื่อพร้อมทำงาน
    vTaskDelay(pdMS_TO_TICKS(15));
}

// ============================================================================
// ฟังก์ชันกำหนดค่าเริ่มต้นของระบบ (Initialization)
// ============================================================================

esp_err_t oled_spi_init(void)
{
    ESP_LOGI(TAG, "เริ่มต้นกำหนดค่าพิน GPIO และบัส SPI2...");

    // 1. กำหนดค่า GPIO สำหรับขา DC และ RES ให้เป็น Output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RES),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // 2. กำหนดค่าบัส SPI (Master Out Only - ไม่ใช้ MISO)
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,               // จอนี้ Write-Only ไม่มีขา MISO
        .mosi_io_num = OLED_PIN_MOSI,     // GPIO 23
        .sclk_io_num = OLED_PIN_SCK,      // GPIO 18
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = OLED_BUFFER_SIZE + 16,
    };

    // ใช้ SPI2_HOST (VSPI เดิมบน ESP32)
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ไม่สามารถ Initialize SPI Bus ได้ (Error: %s)", esp_err_to_name(ret));
        return ret;
    }

    // 3. ผูก Device เข้ากับ Bus (ความถี่ 10 MHz, SPI Mode 0)
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz ความเร็วสูง แสดงผลลื่นไหล
        .mode = 0,                          // Mode 0: CPOL=0, CPHA=0
        .spics_io_num = OLED_PIN_CS,        // GPIO 5
        .queue_size = 7,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ไม่สามารถ Add SPI Device ได้ (Error: %s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ตั้งค่าบัส SPI สำเร็จ!");
    return ESP_OK;
}

void oled_init_display(void)
{
    // รีเซ็ตฮาร์ดแวร์ก่อนเริ่มส่งคำสั่ง
    oled_hardware_reset();

    ESP_LOGI(TAG, "ส่งชุดคำสั่ง Magic Init Sequence สู่ชิป SSD1306...");

    oled_send_cmd(0xAE); // 1. Turn OFF Display
    oled_send_cmd(0xD5); // 2. Set Display Clock Divide Ratio
    oled_send_cmd(0x80);
    oled_send_cmd(0xA8); // 3. Set Multiplex Ratio (64 แถว)
    oled_send_cmd(0x3F);
    oled_send_cmd(0xD3); // 4. Set Display Offset (0)
    oled_send_cmd(0x00);
    oled_send_cmd(0x40); // 5. Set Display Start Line (บรรทัด 0)

    // *** หัวใจสำคัญที่สุด: เปิดวงจรทวีแรงดัน 7.5V (Charge Pump) ***
    oled_send_cmd(0x8D); // 6. Charge Pump Setting
    oled_send_cmd(0x14); // 0x14 = Enable Charge Pump (ถ้าส่ง 0x10 จะดับ)

    oled_send_cmd(0x20); // 7. Memory Addressing Mode
    oled_send_cmd(0x00); // 0x00 = Horizontal Addressing Mode (ยิง 1KB รวดเดียวจบ)

    oled_send_cmd(0xA1); // 8. Set Segment Re-map (Col 0-127)
    oled_send_cmd(0xC8); // 9. Set COM Output Scan Direction (บนลงล่าง)
    oled_send_cmd(0xDA); // 10. Set COM Pins Hardware Config
    oled_send_cmd(0x12);
    oled_send_cmd(0x81); // 11. Set Contrast Control
    oled_send_cmd(0xCF); // ค่าคอนทราสต์ระดับสว่างคมชัด
    oled_send_cmd(0xD9); // 12. Set Pre-charge Period
    oled_send_cmd(0xF1);
    oled_send_cmd(0xDB); // 13. Set VCOMH Deselect Level
    oled_send_cmd(0x40);
    oled_send_cmd(0xA4); // 14. Entire Display Resume (นำข้อมูลจากแรมมาแสดง)
    oled_send_cmd(0xA6); // 15. Set Normal Display (1=สว่าง, 0=ดับ)
    oled_send_cmd(0xAF); // 16. Turn ON Display! (เปิดสว่างขึ้นมา)

    ESP_LOGI(TAG, "ส่งคำสั่งเปิดหน้าจอเรียบร้อย!");
}

// ============================================================================
// ฟังก์ชันจัดการ Framebuffer 1,024 ไบต์ และการ Flush สู่จอ
// ============================================================================

void oled_clear(void)
{
    memset(s_oled_buffer, 0x00, OLED_BUFFER_SIZE);
}

void oled_fill_pattern(uint8_t pattern)
{
    memset(s_oled_buffer, pattern, OLED_BUFFER_SIZE);
}

void oled_flush(void)
{
    // กำหนดขอบเขตคอลัมน์ 0 ถึง 127
    oled_send_cmd(0x21);
    oled_send_cmd(0);
    oled_send_cmd(OLED_WIDTH - 1);

    // กำหนดขอบเขตหน้า Page 0 ถึง Page 7
    oled_send_cmd(0x22);
    oled_send_cmd(0);
    oled_send_cmd(7);

    // ยิงส่งข้อมูล 1,024 ไบต์ ผ่านบัส SPI ในครั้งเดียว
    oled_send_data(s_oled_buffer, OLED_BUFFER_SIZE);
}

void oled_dump_region(int x, int y, int width, int height)
{
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x + width > OLED_WIDTH || y + height > OLED_HEIGHT) {
        ESP_LOGE("FORENSIC", "Invalid framebuffer region: x=%d y=%d width=%d height=%d",
                 x, y, width, height);
        return;
    }

    int first_page = y / 8;
    int last_page = (y + height - 1) / 8;
    ESP_LOGI("FORENSIC", "Framebuffer dump: x=%d..%d, y=%d..%d",
             x, x + width - 1, y, y + height - 1);

    for (int page = first_page; page <= last_page; page++) {
        for (int col = x; col < x + width; col++) {
            uint8_t value = s_oled_buffer[page * OLED_WIDTH + col];
            printf("Page %d Col %3d: 0x%02X [b7..b0=", page, col, value);
            for (int bit = 7; bit >= 0; bit--) {
                printf("%d", (value >> bit) & 1);
            }
            printf("]\n");
        }
    }
}

// ============================================================================
// ฟังก์ชันกราฟิกระดับพิกเซลและรูปทรงเรขาคณิต (Bitwise Engine)
// ============================================================================

void oled_draw_pixel(int x, int y, bool color)
{
    // ป้องกันการเข้าถึงหน่วยความจำเกินขอบเขตพิกัด
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }

    // คำนวณตำแหน่งไบต์และตำแหน่งบิต (8 พิกเซลแนวตั้งต่อ 1 ไบต์)
    int byte_index = x + (y / 8) * OLED_WIDTH;
    int bit_offset = y % 8;

    if (color) {
        s_oled_buffer[byte_index] |= (1 << bit_offset);  // จุดไฟ (OR)
    } else {
        s_oled_buffer[byte_index] &= ~(1 << bit_offset); // ดับไฟ (AND NOT)
    }
}

void oled_draw_line_h(int x, int y, int w, bool color)
{
    for (int i = 0; i < w; i++) {
        oled_draw_pixel(x + i, y, color);
    }
}

void oled_draw_line_v(int x, int y, int h, bool color)
{
    for (int i = 0; i < h; i++) {
        oled_draw_pixel(x, y + i, color);
    }
}

void oled_draw_rect(int x, int y, int w, int h, bool color)
{
    oled_draw_line_h(x, y, w, color);
    oled_draw_line_h(x, y + h - 1, w, color);
    oled_draw_line_v(x, y, h, color);
    oled_draw_line_v(x + w - 1, y, h, color);
}

void oled_fill_rect(int x, int y, int w, int h, bool color)
{
    for (int i = 0; i < h; i++) {
        oled_draw_line_h(x, y + i, w, color);
    }
}

// ============================================================================
// ฟังก์ชันแสดงผลข้อความด้วย Font Matrix 5x7
// ============================================================================

void oled_draw_char(int x, int y, char c, bool color)
{
    if (c < 32 || c > 126) c = '?'; // ถ้าอยู่นอกช่วง ASCII มาตรฐาน

    int font_idx = c - 32;

    for (int col = 0; col < 5; col++) {
        uint8_t line = font5x7[font_idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                oled_draw_pixel(x + col, y + row, color);
            } else {
                oled_draw_pixel(x + col, y + row, !color);
            }
        }
    }
    // เว้นช่องว่างระหว่างตัวอักษร 1 พิกเซล
    for (int row = 0; row < 7; row++) {
        oled_draw_pixel(x + 5, y + row, !color);
    }
}

void oled_draw_string(int x, int y, const char *str, bool color)
{
    while (*str) {
        oled_draw_char(x, y, *str, color);
        x += 6; // 5 พิกเซลตัวอักษร + 1 พิกเซลระยะห่าง
        if (x + 6 > OLED_WIDTH) break; // เกินขอบจอแนวนอน
        str++;
    }
}

// ============================================================================
// ฟังก์ชันควบคุมการทำงานเพิ่มเติม
// ============================================================================

void oled_set_contrast(uint8_t contrast)
{
    oled_send_cmd(0x81);
    oled_send_cmd(contrast);
}

void oled_invert_display(bool invert)
{
    oled_send_cmd(invert ? 0xA7 : 0xA6);
}
