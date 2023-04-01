/*!
    \file main.ino

    \brief Capture image and display it on html page

    The image captured is stored on SPIFFS. This image is a grayscale image.
    The contrast and exposure is set automatically. To change camera parameters, please update 
    the values for "cameraImageExposure" and "cameraImageGain".

    \author Sameer Tuteja
*/

#include <Arduino.h>        ///< Arduino Core if built using for PlatformIO

/*!
    \brief Check for ESP32 board.
*/
#if !defined ESP32
#error Wrong board selected
#endif

#define CAMERA_MODEL_AI_THINKER

#include "esp_camera.h"       ///< Header file for camera obtained from https://github.com/espressif/

#include "driver/ledc.h"      ///< To enable onboard Illumination/flash LED pin attached on 4

#include "soc/soc.h"          //! Used to disable brownout detection
#include "soc/rtc_cntl_reg.h" //! Used to disable brownout detection

#include "WiFi.h"
#include <WiFiClient.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <FS.h>

/*!
    \brief WiFi network credentials
*/
const char *ssid = "REPLACE_WITH_YOUR_SSID";
const char *password = "REPLACE_WITH_YOUR_PASSWORD";

/*!
    \brief Webserver creation on port 80
*/
WebServer server(80);

//! Image resolution:
/*!
    default = "const framesize_t FRAME_SIZE_IMAGE = FRAMESIZE_VGA"

    Other available Frame Sizes:
    160x120 (QQVGA), 128x160 (QQVGA2), 176x144 (QCIF), 240x176 (HQVGA),
    320x240 (QVGA), 400x296 (CIF), 640x480 (VGA, default), 800x600 (SVGA),
    1024x768 (XGA), 1280x1024 (SXGA), 1600x1200 (UXGA)
*/
const framesize_t FRAME_SIZE_IMAGE = FRAMESIZE_240X240;

//! Image Format
/*!
    Other Available formats:
    YUV422, GRAYSCALE, RGB565, JPEG, RGB888
*/
#define PIXFORMAT PIXFORMAT_GRAYSCALE

//! Camera exposure
/*!
    Range: (0 - 1200)
    If gain and exposure both set to zero then auto adjust is enabled
*/
int cameraImageExposure = 0;

//! Image gain
/*!
    Range: (0 - 30)
    If gain and exposure both set to zero then auto adjust is enabled
*/
int cameraImageGain = 0;

const uint8_t ledPin = 4;                  ///< onboard Illumination/flash LED pin (4)
unsigned int ledBrightness = 0;            ///< Initial brightness (0 - 255)
const int pwmFrequency = 50000;            ///< PWM settings for ESP32
const uint8_t ledChannel = LEDC_CHANNEL_0; ///< Camera timer0
const uint8_t pwmResolution = 8;           ///< resolution (8 = from 0 to 255)

const int serialSpeed = 115200;            ///< Serial data speed to use

boolean captureNewImage = false;


#define SAVED_FILE "/image.jpg"            ///< Image File Name for SPIFFS

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { text-align:center; }
  </style>
  <title>Dashboard</title>
</head>
<body>
  <div>
    <h2>ESP32-CAM Image HTML Dump</h2>
    <p>
      There is an auto refresh of image after 5 seconds is enabled.
      <label for="autoRef">Auto Refresh</label>
      <input type="checkbox" id="autoRef">
    </p>
    <p>
      <button onclick="captureImage()">Capture Image</button>
      <button onclick="getJpgData()">Refresh Image</button>
    </p>
  </div>
  <div><img src="#" id="image" width="50%"></div>
</body>
<script>
  setInterval(function(){
    autoRefChecked = document.getElementById('autoRef')
    if(autoRefChecked.checked == true)
    {
      captureImage();
      getJpgData();
    }
  }, 5000);

  function captureImage(){
    var xhttp = new XMLHttpRequest();
    xhttp.open('GET', "/capture", true);
    xhttp.send();
  }

  function getJpgData(){
    var feed = 'saved-image';
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        var timestamp = new Date().getTime();
        var queryString = '?t=' + timestamp;
        document.getElementById('image').src =feed+ queryString;
      }
    };
    xhttp.open("GET", feed, true);
    xhttp.send();
  }

</script>
</html>)rawliteral";                        ///< HTML Page

//! Camera setting
/*!
    Camera settings for CAMERA_MODEL_AI_THINKER OV2640
    Based on CameraWebServer sample code by ESP32 Arduino

*/
#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#endif

/**************************************************************************/
/**
  Camera Image Settings
  Set Image parameters
  Based on CameraWebServer sample code by ESP32 Arduino
  \return true: successful, false: failed
*/
/**************************************************************************/
bool cameraImageSettings()
{

  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr)
  {
    Serial.println("Error: problem reading camera sensor settings");
    return false;
  }

  // if both set to zero enable auto adjust
  if (cameraImageExposure == 0 && cameraImageGain == 0)
  {
    // enable auto adjust
    s->set_gain_ctrl(s, 1);     // auto gain on
    s->set_exposure_ctrl(s, 1); // auto exposure on
    s->set_awb_gain(s, 1);      // Auto White Balance enable (0 or 1)
    s->set_hmirror(s, 1);
    s->set_vflip(s, 1);
  }
  else
  {
    // Apply manual settings
    s->set_gain_ctrl(s, 0);                   // auto gain off
    s->set_awb_gain(s, 1);                    // Auto White Balance enable (0 or 1)
    s->set_exposure_ctrl(s, 0);               // auto exposure off
    s->set_agc_gain(s, cameraImageGain);      // set gain manually (0 - 30)
    s->set_aec_value(s, cameraImageExposure); // set exposure manually  (0-1200)
  }

  return true;
}

/**************************************************************************/
/**
  Initialise Camera
  Set camera parameters
  Based on CameraWebServer sample code by ESP32 Arduino
  \return true: successful, false: failed
*/
/**************************************************************************/
bool initialiseCamera()
{
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; // 20000000 ori //new 1000000
  config.pixel_format = PIXFORMAT;
  config.frame_size = FRAME_SIZE_IMAGE;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  // Check the esp32cam board has a PSRAM chip installed (extra memory used for storing captured images)
  // Note: if not using "AI thinker esp32 cam" in the Arduino IDE, PSRAM must be enabled
  if (!psramFound())
  {
    Serial.println("Warning: No PSRam found so defaulting to image size 'CIF'");
    config.frame_size = FRAMESIZE_CIF;
  }

  esp_err_t camera = esp_camera_init(&config); // initialise the camera
  if (camera != ESP_OK)
  {
    Serial.printf("ERROR: Camera init failed with error 0x%x", camera);
  }

  cameraImageSettings(); // Apply custom camera settings

  return (camera == ESP_OK); // Return boolean result of camera initialisation
}

/**************************************************************************/
/**
  Setup On Board Flash
  Initialize on board LED with pwm channel
*/
/**************************************************************************/
void setupOnBoardFlash()
{
  ledcSetup(ledChannel, pwmFrequency, pwmResolution);
  ledcAttachPin(ledPin, ledChannel);
}

/**************************************************************************/
/**
  Set Led Brightness
  Set pwm value to change brightness of LED
*/
/**************************************************************************/
void setLedBrightness(unsigned int &ledBrightness)
{
  unsigned int temp = map(ledBrightness, 0, 255, 0, 255);
  ledcWrite(ledChannel, temp);
}

/**************************************************************************/
/**
  Setup wifi connection with provided SSID and Password  
*/
/**************************************************************************/
void wifiSetup()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/**************************************************************************/
/*!
  \brief  Check image function

    Check if image captured and stored in SPIFFS
*/
/**************************************************************************/
bool checkImageOnStorage(fs::FS &fs, size_t &imageSizeBytes)
{
  File f_pic = fs.open(SAVED_FILE);
  unsigned int pic_sz = f_pic.size();
  return (pic_sz == imageSizeBytes);
}

/**************************************************************************/
/*!
  \brief  Capture Image abd save to Spiffs function

*/
/**************************************************************************/
void captureImageSaveSpiffs()
{
  camera_fb_t *fb = nullptr; ///< Pointer to camera buffer
  bool status_OK = false; ///< Boolean indicating if the picture has been taken correctly

  do
  {
    // capture image with the camera
    Serial.println("Capturing image...");

    fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.println("Camera capture failed");
      return;
    }

    // Convert buffer to JPG
    size_t _jpgBufLen = 0;
    uint8_t *_jpgBuf = nullptr;
    bool _jpgConverted  = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_GRAYSCALE, 80, &_jpgBuf, &_jpgBufLen);
    if (!_jpgConverted) {
      Serial.println("Frame to JPG conversion failed!");
    }
    size_t imageSizeBytes = _jpgBufLen;

    // Create file in SPIFFS and open in write mode
    Serial.printf("Picture file name: %s\n", SAVED_FILE);
    File file = SPIFFS.open(SAVED_FILE, FILE_WRITE);

    // Save data to created file
    if (!file)
    {
      Serial.println("Failed to open file in writing mode");
    }
    else
    {
      file.write(_jpgBuf, _jpgBufLen); // payload (image), payload length
      Serial.print("Image saved at: ");
      Serial.print(SAVED_FILE);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }

    // Close file
    file.close();

    //Reset jpg buffer
    free(_jpgBuf);
    _jpgBuf = nullptr;

    // Return camera buffer for future capture
    esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    status_OK = checkImageOnStorage(SPIFFS, imageSizeBytes);

  } while (!status_OK);
}

/**************************************************************************/
/*!
  \brief  Setup function

  Initialization for following:
    disable Brownout detection
    camera

*/
/**************************************************************************/
void setup()
{
  Serial.begin(serialSpeed); ///< Initialize serial communication

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); ///< Disable 'brownout detector'

  Serial.print("\nInitialising SPIFFS: "); ///< SPIFFS check
  if (SPIFFS.begin(true))
  {
    Serial.println("OK");
  }
  else
  {
    Serial.println("Error!");
    return;
  }

  wifiSetup(); ///< wifi connection

  Serial.print("\nInitialising camera: "); ///< Camera check
  if (initialiseCamera())
  {
    Serial.println("OK");
  }
  else
  {
    Serial.println("Error!");
    return;
  }

  ///< Webpage declaration
  server.on("/", []()
  {
    server.send(200, "text/html", index_html);
  });

  server.on("/capture", []()
  {
    captureNewImage = true;
    server.send(200, "text/plain", "Capturing image");
  });

  server.on("/saved-image", []()
  {
    File file = SPIFFS.open(SAVED_FILE, "r");
    server.streamFile(file, "image/jpg");
    file.close();
  });
  Serial.println("Starting server on IP");
  server.begin();
}

/**************************************************************************/
/*!
  \brief  Loop function
  Capture image when button pressed on webpage.
*/
/**************************************************************************/
void loop()
{
  server.handleClient();
  if (captureNewImage)
  {
    captureImageSaveSpiffs();
    captureNewImage = false;
  }
  delay(1);
}