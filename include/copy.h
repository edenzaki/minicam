
// #include "esp_camera.h"
// #include "Arduino.h"

// #define CAMERA_MODEL_AI_THINKER

// #include "camera_pins.h"

// // simple Base64 encoder used to transmit the JPEG over serial
// const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// String base64Encode(const uint8_t *data, size_t len) {
//     String ret;
//     int i = 0;
//     uint32_t val = 0;
//     int valb = -6;
//     while (len--) {
//         val = (val << 8) + *data++;
//         valb += 8;
//         while (valb >= 0) {
//             ret += b64_chars[(val >> valb) & 0x3F];
//             valb -= 6;
//         }
//     }
//     if (valb > -6) ret += b64_chars[((val << 8) >> (valb + 8)) & 0x3F];
//     while (ret.length() % 4) ret += '=';
//     return ret;
// }

// // The setup function runs once when you press reset or power the board
// void setup()
// {
//     Serial.begin(115200); // Start the serial communication to print out debug messages

//     //----------------------------- Camera Configuration ----------------------------
//     camera_config_t config;
//     config.ledc_channel = LEDC_CHANNEL_0;
//     config.ledc_timer = LEDC_TIMER_0;
//     config.pin_d0 = 5;
//     config.pin_d1 = 18;
//     config.pin_d2 = 19;
//     config.pin_d3 = 21;
//     config.pin_d4 = 36;
//     config.pin_d5 = 39;
//     config.pin_d6 = 34;
//     config.pin_d7 = 35;
//     config.pin_xclk = 0;
//     config.pin_pclk = 22;
//     config.pin_vsync = 25;
//     config.pin_href = 23;
//     config.pin_sccb_sda = 26;
//     config.pin_sccb_scl = 27;
//     config.pin_pwdn = 32;
//     config.pin_reset = -1;

//     // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode)
//     config.xclk_freq_hz = 20000000; // 20MHz for OV2640

//     // LOW RESOLUTION SETTINGS
//     config.pixel_format = PIXFORMAT_JPEG; // YUV422, GRAYSCALE, RGB565, JPEG
//     config.frame_size = FRAMESIZE_QQVGA;  // 160x120
//     config.jpeg_quality = 12;             // 0-63 lower means higher quality
//     config.fb_count = 1;                  // if more than one, i2s runs in continuous mode. Use only with JPEG

//     /*
//     // HIGH RESOLUTION SETTINGS
//     config.pixel_format = PIXFORMAT_JPEG; // YUV422, GRAYSCALE, RGB565, JPEG
//     config.frame_size = FRAMESIZE_UXGA;  // 1600x1200
//     config.jpeg_quality = 10;
//     config.fb_count = 2; // if more than one, i2s runs in continuous mode. Use only with JPEG
//     */

//     if (esp_camera_init(&config) != ESP_OK)
//     {
//         Serial.println("Camera init failed");
//         return;
//     }

//     // Capture a single frame and print its size
//     Serial.println("Taking picture...");

//     camera_fb_t *fb = esp_camera_fb_get(); // Get the frame buffer from the camera

//     // Check if the frame buffer is valid
//     if (!fb)
//     {
//         Serial.println("Camera capture failed");
//         return;
//     }

//     // send the picture as base64 so the PC side script can save it
//     Serial.println("BEGIN_IMAGE");
//     String encoded = base64Encode(fb->buf, fb->len);
//     Serial.println(encoded);
//     Serial.println("END_IMAGE");

//     esp_camera_fb_return(fb);
// }

// void loop()
// {
// }