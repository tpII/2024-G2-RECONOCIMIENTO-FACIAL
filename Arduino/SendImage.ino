#include <Base64.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

#define PIR_PIN 14

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
  pinMode(PIR_PIN, INPUT_PULLUP); 
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

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish(MQTT_PUBLISH_TOPIC, "hello world");
      // ... and resubscribe
      client.subscribe(mqttTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
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
    //s->set_framesize(s, FRAMESIZE_QVGA);
    Serial.println("Cámara inicializada correctamente");
  }
}

String sendImage() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }

  // Crear un buffer estático grande para la codificación Base64
  const size_t maxBase64Size = (4 * ((fb->len + 2) / 3)) + 1; // Tamaño máximo Base64
  char *base64Image = (char *)malloc(maxBase64Size);
  if (!base64Image) {
    Serial.println("Fallo al alocar memoria para Base64");
    esp_camera_fb_return(fb);
    return "Fallo al alocar memoria";
  }

  // Codificar la imagen a Base64
  size_t encodedLen = base64_encode(base64Image, (char *)fb->buf, fb->len);

  // Crear el encabezado "data:image/jpeg;base64,"
  const char *header = "data:image/jpeg;base64,";
  size_t headerLen = strlen(header);

  // Calcular el tamaño total del mensaje
  size_t totalLen = headerLen + encodedLen;

  esp_camera_fb_return(fb);
  
  String clientId = "ESP32-";
  clientId += String(random(0xffff), HEX);
  if (client.connect(clientId.c_str())) {
    
    client.beginPublish(mqttTopic, totalLen, true);

    // Enviar el encabezado primero
    client.write((uint8_t *)header, headerLen);

    // Enviar la imagen codificada en partes de 2048 bytes
    for (size_t i = 0; i < encodedLen; i += 2048) {
      size_t chunkSize = (i + 2048 < encodedLen) ? 2048 : (encodedLen - i);
      client.write((uint8_t *)(base64Image + i), chunkSize);
    }
    
    client.endPublish();
    
    free(base64Image);
    //esp_camera_fb_return(fb);
    return "Foto enviada";
  }
  free(base64Image);
  //esp_camera_fb_return(fb);
  return "failed, rc="+client.state();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  int pirState = digitalRead(PIR_PIN);
  if (pirState == LOW) {
    delay(20);
    if (digitalRead(PIR_PIN) == LOW) {
      Serial.println(sendImage());
    }
  }
  delay(100);  // Enviar cada 10 segundos, reducir de 1 minuto para pruebas posteriores
}