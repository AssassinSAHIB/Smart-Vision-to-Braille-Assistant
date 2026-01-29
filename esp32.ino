#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include "esp_http_server.h" 
#include "img_converters.h" 
#include <ArduinoJson.h>
#include "soc/soc.h"           // Disable brownout
#include "soc/rtc_cntl_reg.h"  // Disable brownout

HardwareSerial UnoSerial(1);

// ======================= USER SETTINGS =======================
const char* ssid = "EastCoast";
const char* password = "bubbly_happier_onion";
const char* gemini_api_key = "AIzaSyBe8kznV8OpcTOSIBOc-bNURk-u21MOCm0";
// =============================================================

#define BUTTON_PIN 12

// ======================= STREAM DEFINITIONS =======================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// CAMERA PINS (AI-Thinker Model)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

httpd_handle_t stream_httpd = NULL;

// ================= GEMINI FUNCTION (ROBUST VERSION) =================
String sendToGemini(camera_fb_t *fb) {
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate check
  client.setTimeout(60000); // Very long timeout (60s)

  Serial.println("Connecting to Gemini...");
  // Use a fixed IP for Google if DNS fails, but usually hostname is fine
  if (!client.connect("generativelanguage.googleapis.com", 443)) {
    Serial.println("Connection failed!");
    return "Connect Failed";
  }

  // UPDATED MODEL: gemini-1.5-flash-latest
  String startJson = "{\"contents\":[{\"parts\":[{\"text\": \"Read this text. Return ONLY the text.\"},{\"inline_data\": {\"mime_type\": \"image/jpeg\",\"data\": \"";
  String endJson = "\"}}]}]}";

  size_t b64len = ((fb->len + 2) / 3) * 4;
  size_t totalLen = startJson.length() + b64len + endJson.length();

  // Send HTTP Headers
  client.println("POST /v1/models/gemini-2.5-flash:generateContent?key=" + String(gemini_api_key) + " HTTP/1.1");

  client.println("Host: generativelanguage.googleapis.com");
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(totalLen);
  client.println("Connection: close"); // Tell Google to close after sending
  client.println(); 

  client.print(startJson);
  
  // Stream Base64 Image Data
  const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (size_t i = 0; i < fb->len; i += 3) {
    uint32_t val = fb->buf[i] << 16;
    if (i + 1 < fb->len) val |= fb->buf[i + 1] << 8;
    if (i + 2 < fb->len) val |= fb->buf[i + 2];
    client.write(b64[(val >> 18) & 0x3F]);
    client.write(b64[(val >> 12) & 0x3F]);
    client.write((i + 1 < fb->len) ? b64[(val >> 6) & 0x3F] : '=');
    client.write((i + 2 < fb->len) ? b64[val & 0x3F] : '=');
  }

  client.print(endJson);
  Serial.println("Data sent. Waiting for response...");

  // Read Response (Improved Logic)
  String response = "";
  long start = millis();
  
  // Keep reading as long as connected OR data is available
  while ((client.connected() || client.available()) && millis() - start < 60000) { 
    if (client.available()) {
      char c = client.read();
      response += c;
    }
  }

  // Extract Text (Handling JSON structure)
  int textIdx = response.indexOf("\"text\":");
  if (textIdx > 0) {
    int startQuote = response.indexOf("\"", textIdx + 7); // Find opening quote
    if (startQuote > 0) {
        int endQuote = response.indexOf("\"", startQuote + 1); // Find closing quote
        // Simple scan for end quote, ignoring escaped quotes for now (basic parser)
        while (endQuote > 0 && response[endQuote - 1] == '\\') {
            endQuote = response.indexOf("\"", endQuote + 1);
        }
        if (endQuote > 0) {
            String extracted = response.substring(startQuote + 1, endQuote);
            extracted.replace("\\n", "\n"); 
            return extracted;
        }
    }
  }
  
  return "No Text Found (See Raw Response)";
}

// ================= VIDEO STREAM HANDLER =================
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) break;
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t stream_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.printf("Video Stream Ready! Go to: http://%s\n", WiFi.localIP().toString().c_str());
  }
}

// ================= SETUP & LOOP =================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout
  
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  // Settings for Stability
  config.xclk_freq_hz = 10000000; // 10MHz to prevent overflow
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  // Low res for Memory Safety
  config.frame_size = FRAMESIZE_QVGA; 
  config.jpeg_quality = 12;
  config.fb_count = 1; // 1 Buffer is safer for SSL memory

  esp_camera_init(&config);
  
  // FORCE GOOGLE DNS
  IPAddress dns(8, 8, 8, 8); 
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  startCameraServer();

  UnoSerial.begin(9600, SERIAL_8N1, -1, 14);
}

void loop() {
  static bool lastBtn = HIGH;
  bool btn = digitalRead(BUTTON_PIN);

  // Detect press edge (HIGH -> LOW)
  if (lastBtn == HIGH && btn == LOW) {
    delay(60); // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {

      Serial.println(">> CAPTURING FOR GEMINI...");

      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("❌ fb NULL");
      } else {
        Serial.printf("✅ Captured %d bytes\n", fb->len);
        String text = sendToGemini(fb);

        Serial.println(text);

        // ---- CLEAN TEXT BEFORE SENDING ----
        text.replace("\n", " ");
        text.replace("\r", " ");
        text.trim();

        // Optional: limit length (safety)
        if (text.length() > 200) {
          text = text.substring(0, 200);
        }

        UnoSerial.print("TXT:");
        UnoSerial.println(text);
        UnoSerial.println("END");

        esp_camera_fb_return(fb);
      }

      // lockout to avoid accidental double-press spam
      delay(1500);
    }
  }

  lastBtn = btn;
}
