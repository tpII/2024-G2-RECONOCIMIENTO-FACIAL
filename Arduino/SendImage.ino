#include <Base64.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Configuro la red Wi-Fi
const char* ssid = NETWORK_ID;
const char* password = NETWORK_PASS;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 5, //0-63 lower number means higher quality
    .fb_count = 1,       //if more than one, i2s runs in continuous mode. Use only with JPEG
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

// Configuración MQTT
const char* mqttServer = MY_IP;  // IP del broker MQTT
const int mqttPort = 1884;
const char* mqttTopic = "home/security/camera";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void setup() {
  Serial.begin(115200);
  connectWiFi();
  delay(1000);
  setupMQTT();
  delay(1000);
  startCamera();
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi");
}

void setupMQTT() {
  client.setServer(mqttServer, mqttPort);
  while (!client.connected()) {
    if (client.connect("ESP32CAM")) {
      Serial.println("Conectado al broker MQTT");
    } else {
      Serial.print("Conexión fallida, estado: ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void startCamera() {
  if (esp_camera_init(&camera_config) != ESP_OK) {
    Serial.println("Error al inicializar la cámara");
  } else {
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
      s->set_brightness(s, 1);   // Brillo: -2 a 2
    }
    Serial.println("Cámara inicializada correctamente");
  }
}

String sendImage() {
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }  

  char *input = (char *)fb->buf;
  char output[base64_enc_len(3)];
  String imageFile = "data:image/jpeg;base64,";
  for (int i=0;i<fb->len;i++) {
    base64_encode(output, (input++), 3);
    if (i%3==0) imageFile += String(output);
  }
  int fbLen = imageFile.length();
  
  String clientId = "ESP32-";
  clientId += String(random(0xffff), HEX);
  if (client.connect(clientId.c_str())) {
    
    client.beginPublish(mqttTopic, fbLen, true);

    String str = "";
    for (size_t n=0;n<fbLen;n=n+2048) {
      if (n+2048<fbLen) {
        str = imageFile.substring(n, n+2048);
        client.write((uint8_t*)str.c_str(), 2048);
      }
      else if (fbLen%2048>0) {
        size_t remainder = fbLen%2048;
        str = imageFile.substring(n, n+remainder);
        client.write((uint8_t*)str.c_str(), remainder);
      }
    }  
    
    client.endPublish();
    
    esp_camera_fb_return(fb);
    return "Foto enviada";
  }
  esp_camera_fb_return(fb);
  return "failed, rc="+client.state();
}

void loop() {
  if (!client.connected()) {
    setupMQTT();
  }
  client.loop();
  Serial.println(sendImage());
  delay(10000);  // Enviar cada 10 segundos, reducir de 1 minuto para pruebas posteriores
}