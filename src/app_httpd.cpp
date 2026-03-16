// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"

typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t fb_len = 0;
    if(fb->format == PIXFORMAT_JPEG){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        jpg_chunking_t jchunk = {req, 0};
        res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);
        fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];
    int64_t fr_start = 0;

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        } else {
            fr_start = esp_timer_get_time();
            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if(!jpeg_converted){
                    Serial.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(fb){
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if(_jpg_buf){
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - fr_start;
        Serial.printf("MJPG: %uB %ums\n", (uint32_t)(_jpg_buf_len), (uint32_t)(frame_time/1000));
    }

    return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
    char*  buf;
    size_t buf_len;
    char variable[32] = {0,};
    char value[32] = {0,};

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if(!buf){
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
            } else {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        } else {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t * s = esp_camera_sensor_get();
    int res = 0;

    if(!strcmp(variable, "framesize")) {
        if(s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
    }
    else if(!strcmp(variable, "quality")) res = s->set_quality(s, val);
    else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
    else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
    else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
    else if(!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
    else if(!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
    else if(!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
    else if(!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
    else if(!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
    else if(!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
    else if(!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
    else if(!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
    else if(!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
    else if(!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
    else if(!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
    else if(!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
    else if(!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
    else if(!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
    else if(!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
    else if(!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
    else if(!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
    else if(!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
    else if(!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
    else {
        res = -1;
    }

    if(res){
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req){
    static char json_response[1024];

    sensor_t * s = esp_camera_sensor_get();
    char * p = json_response;
    *p++ = '{';

    p+=sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p+=sprintf(p, "\"quality\":%u,", s->status.quality);
    p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p+=sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p+=sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p+=sprintf(p, "\"awb\":%u,", s->status.awb);
    p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p+=sprintf(p, "\"aec\":%u,", s->status.aec);
    p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p+=sprintf(p, "\"agc\":%u,", s->status.agc);
    p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p+=sprintf(p, "\"vflip\":%u,", s->status.vflip);
    p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p+=sprintf(p, "\"colorbar\":%u", s->status.colorbar);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!doctype html>
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>ESP32-CAM</title>
        <style>
            body {
                font-family: Arial,Helvetica,sans-serif;
                background: #181818;
                color: #EFEFEF;
                font-size: 16px
            }
            h2 {
                font-size: 18px
            }
            section.main {
                display: flex
            }
            img {
                width: 100%;
                height: auto;
                max-width: 300px
            }
            @media (min-width: 800px) {
                img {
                    max-width: unset;
                    width: auto
                }
            }
            .image-container {
                position: relative;
                min-width: 160px
            }
            .close {
                position: absolute;
                right: 5px;
                top: 5px;
                background: #ff3034;
                width: 16px;
                height: 16px;
                border-radius: 100px;
                color: #fff;
                text-align: center;
                line-height: 18px;
                text-decoration: none
            }
            .hidden {
                display: none
            }
        </style>
    </head>
    <body>
        <section class="main">
            <div id="stream-container" class="image-container hidden">
                <div class="close" id="close-stream">×</div>
                <img id="stream" src="">
            </div>
        </section>
        <section class="main">
            <div id="image-container" class="image-container">
                <div class="close" id="close-image">×</div>
                <img id="image" src="">
            </div>
        </section>
        <section class="main">
            <table>
                <tr>
                    <td colspan="3" align="center">
                        <button id="get-still">Get Still</button>
                        <button id="toggle-stream">Start Stream</button>
                    </td>
                </tr>
                <tr>
                    <td>Framesize</td>
                    <td colspan="2">
                        <select id="framesize" class="default-action">
                            <option value="10">UXGA(1600x1200)</option>
                            <option value="9">SXGA(1280x1024)</option>
                            <option value="8">XGA(1024x768)</option>
                            <option value="7">SVGA(800x600)</option>
                            <option value="6">VGA(640x480)</option>
                            <option value="5">CIF(400x296)</option>
                            <option value="4">QVGA(320x240)</option>
                            <option value="3">HQVGA(240x176)</option>
                            <option value="0">QQVGA(160x120)</option>
                        </select>
                    </td>
                </tr>
                <tr>
                    <td>Quality</td>
                    <td colspan="2">
                        <select id="quality" class="default-action">
                            <option value="63">Very Low</option>
                            <option value="50">Low</option>
                            <option value="40">Medium Low</option>
                            <option value="30">Medium</option>
                            <option value="20">Medium High</option>
                            <option value="10">High</option>
                            <option value="0">Very High</option>
                        </select>
                    </td>
                </tr>
                <tr>
                    <td>Brightness</td>
                    <td align="left">
                        <input type="button" value="-" class="button" onclick="changeBrightness(this)">
                    </td>
                    <td align="right">
                        <input type="button" value="+" class="button" onclick="changeBrightness(this)">
                    </td>
                </tr>
                <tr>
                    <td>Contrast</td>
                    <td align="left">
                        <input type="button" value="-" class="button" onclick="changeContrast(this)">
                    </td>
                    <td align="right">
                        <input type="button" value="+" class="button" onclick="changeContrast(this)">
                    </td>
                </tr>
                <tr>
                    <td>Saturation</td>
                    <td align="left">
                        <input type="button" value="-" class="button" onclick="changeSaturation(this)">
                    </td>
                    <td align="right">
                        <input type="button" value="+" class="button" onclick="changeSaturation(this)">
                    </td>
                </tr>
                <tr>
                    <td>Special Effect</td>
                    <td colspan="2">
                        <select id="special_effect" class="default-action">
                            <option value="0">No Effect</option>
                            <option value="1">Negative</option>
                            <option value="2">Grayscale</option>
                            <option value="3">Red Tint</option>
                            <option value="4">Green Tint</option>
                            <option value="5">Blue Tint</option>
                            <option value="6">Sepia</option>
                        </select>
                    </td>
                </tr>
                <tr>
                    <td>Flip & Mirror</td>
                    <td colspan="2">
                        <select id="flip" class="default-action">
                            <option value="0">Normal</option>
                            <option value="1">Flip Vertical</option>
                            <option value="2">Flip Horizontal</option>
                            <option value="3">Rotate 180°</option>
                        </select>
                    </td>
                </tr>
            </table>
        </section>
        <script>
            document.addEventListener('DOMContentLoaded', function (event) {
                var baseHost = document.location.origin
                var streamUrl = baseHost + ':81'

                const hide = el => {
                    el.classList.add('hidden')
                }
                const show = el => {
                    el.classList.remove('hidden')
                }

                const disable = el => {
                    el.classList.add('disabled')
                }

                const enable = el => {
                    el.classList.remove('disabled')
                }

                const updateValue = (el, value, updateRemote) => {
                    updateRemote = updateRemote == null ? true : updateRemote
                    let initialValue
                    if (el.type === 'checkbox') {
                        initialValue = el.checked
                        value = !!value
                        el.checked = value
                    } else {
                        initialValue = el.value
                        el.value = value
                    }

                    if (updateRemote && initialValue !== value) {
                        updateConfig(el);
                    }
                }

                function updateConfig (el) {
                    let value
                    switch (el.type) {
                        case 'checkbox':
                            value = el.checked ? 1 : 0
                            break
                        case 'range':
                        case 'select-one':
                            value = el.value
                            break
                        case 'button':
                        case 'submit':
                            value = '1'
                            break
                        default:
                            return
                    }

                    const query = `${baseHost}/control?var=${el.id}&val=${value}`

                    fetch(query)
                        .then(response => {
                            console.log(`request to ${query} finished, status: ${response.status}`)
                        })
                }

                document
                    .querySelectorAll('.close')
                    .forEach(el => {
                        el.onclick = () => {
                            hide(el.parentNode)
                        }
                    })

                // read initial values
                fetch(`${baseHost}/status`)
                    .then(function (response) {
                        return response.json()
                    })
                    .then(function (state) {
                        document
                            .querySelectorAll('.default-action')
                            .forEach(el => {
                                updateValue(el, state[el.id], false)
                            })
                    })

                const view = document.getElementById('stream')
                const viewContainer = document.getElementById('stream-container')
                const stillButton = document.getElementById('get-still')
                const streamButton = document.getElementById('toggle-stream')
                const closeButton = document.getElementById('close-stream')

                const stopStream = () => {
                    window.stop();
                    streamButton.innerHTML = 'Start Stream'
                }

                const startStream = () => {
                    view.src = `${streamUrl}/stream`
                    show(viewContainer)
                    streamButton.innerHTML = 'Stop Stream'
                }

                const snapshot = () => {
                    view.src = `${baseHost}/capture?_cb=${Date.now()}`
                    show(viewContainer)
                }

                // Attach actions to buttons
                stillButton.onclick = () => snapshot()
                closeButton.onclick = () => stopStream()
                streamButton.onclick = () => {
                    const streamEnabled = streamButton.innerHTML === 'Stop Stream'
                    if (streamEnabled) {
                        stopStream()
                    } else {
                        startStream()
                    }
                }

                // Attach default on change action
                document
                    .querySelectorAll('.default-action')
                    .forEach(el => {
                        el.onchange = () => updateConfig(el)
                    })

                // Custom actions
                function changeBrightness (dir) {
                    var value = parseInt(document.getElementById("brightness").value);
                    value += dir == "+" ? 1 : -1;
                    if (value < -2) value = -2;
                    if (value > 2) value = 2;
                    document.getElementById("brightness").value = value;
                    updateConfig(document.getElementById("brightness"));
                }

                function changeContrast (dir) {
                    var value = parseInt(document.getElementById("contrast").value);
                    value += dir == "+" ? 1 : -1;
                    if (value < -2) value = -2;
                    if (value > 2) value = 2;
                    document.getElementById("contrast").value = value;
                    updateConfig(document.getElementById("contrast"));
                }

                function changeSaturation (dir) {
                    var value = parseInt(document.getElementById("saturation").value);
                    value += dir == "+" ? 1 : -1;
                    if (value < -2) value = -2;
                    if (value > 2) value = 2;
                    document.getElementById("saturation").value = value;
                    updateConfig(document.getElementById("saturation"));
                }
            })
        </script>
    </body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        httpd_register_uri_handler(stream_httpd, &capture_uri);
    }
}

void setupLedFlash() {
    // Setup LED Flash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
    pinMode(LED_GPIO_NUM, OUTPUT);
#endif
}