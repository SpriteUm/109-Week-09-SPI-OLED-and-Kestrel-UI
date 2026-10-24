#ifndef SSD1306_SPI_H
#define SSD1306_SPI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// กำหนดพินเชื่อมต่อกับ ESP32 ตามมาตรฐานของวิชา
#define OLED_PIN_SCK   18   // D0 (SPI Clock)
#define OLED_PIN_MOSI  23   // D1 (SPI MOSI Data)
#define OLED_PIN_CS     5   // CS (Chip Select - Active LOW)
#define OLED_PIN_DC     2   // DC (0 = Command, 1 = Data)
#define OLED_PIN_RES    4   // RES (Reset - Active LOW)

// ขนาดความละเอียดทางกายภาพของหน้าจอ
#define OLED_WIDTH     128
#define OLED_HEIGHT     64
#define OLED_BUFFER_SIZE (OLED_WIDTH * OLED_HEIGHT / 8) // 1,024 Bytes

#ifdef __cplusplus
extern "C" {
#endif

// ฟังก์ชันควบคุมฮาร์ดแวร์ SPI และการตั้งค่าเริ่มต้น
esp_err_t oled_spi_init(void);
void oled_hardware_reset(void);
void oled_send_cmd(uint8_t cmd);
void oled_send_data(const uint8_t *data, size_t len);
void oled_init_display(void);

// ฟังก์ชันจัดการ Framebuffer ขนาด 1KB ในหน่วยความจำของ ESP32
void oled_clear(void);
void oled_fill_pattern(uint8_t pattern);
void oled_flush(void);
void oled_dump_region(int x, int y, int width, int height);

// ฟังก์ชันกราฟิกระดับพิกเซลและรูปทรงเรขาคณิต
void oled_draw_pixel(int x, int y, bool color);
void oled_draw_line_h(int x, int y, int w, bool color);
void oled_draw_line_v(int x, int y, int h, bool color);
void oled_draw_rect(int x, int y, int w, int h, bool color);
void oled_fill_rect(int x, int y, int w, int h, bool color);

// ฟังก์ชันแสดงผลข้อความด้วย Font Matrix 5x7
void oled_draw_char(int x, int y, char c, bool color);
void oled_draw_string(int x, int y, const char *str, bool color);

// ฟังก์ชันปรับแต่งการแสดงผล
void oled_set_contrast(uint8_t contrast);
void oled_invert_display(bool invert);

#ifdef __cplusplus
}
#endif

#endif // SSD1306_SPI_H
