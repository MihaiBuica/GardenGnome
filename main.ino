#include <WiFi.h>
#include <Wire.h>

#define SIMULATION_MODE_IMAGE
//#define SIMULATION_MODE_SENSOR
#define SEND_IFTTT_PKG
#define PROCESS_IMG_CORE0
#define PROCESS_SENSOR_CORE1
//#define WOKWI_ENV

#ifndef SIMULATION_MODE_SENSOR
#include "DHT.h"
#define DHTPIN 4
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);
#endif

// WiFi status
#ifdef WOKWI_ENV
#define WLAN_SSID "Wokwi-GUEST"
#define WLAN_PSWD ""
#else
#define WLAN_SSID ""
#define WLAN_PSWD ""
#endif

// Weather forecast
#define RAIN_PREDICTION
#define AIR_PRESSURE  950
#define CITY_ALTITUDE 520

// Times
#define TARGET_TIMESTAMP 10
#define DELAY_SEC        10
#define RETRIES   5
#define DAILY_CNT 24

// Status definition
#define IOT_STATUS int
#define IOT_SUCCESS 0
#define IOT_SERVER_ERR -1
#define IOT_SENSOR_ERR -2
#define IOT_WIFI_ERR   -3
 
// Tasks from the cores
#ifdef PROCESS_SENSOR_CORE1
TaskHandle_t sensorsCore;
#endif
#ifdef PROCESS_IMG_CORE0
TaskHandle_t imageCore;
#endif

// Image definition
#define IMG_WIDTH  100
#define IMG_HEIGHT 100
#ifdef SIMULATION_MODE_IMAGE
char img_data[IMG_HEIGHT * IMG_WIDTH];
#else
char* img_data;
#endif
#define IMG_INFO_THRESHOLD 25
#define IMG_PIXEL_THRESHOLD 50
struct iot_image
{
  int width;
  int height;
  int format;
  int valid; // 0 = false, !0 = true
  char* img_data;
};
struct iot_image iot_img;

struct wifi_data
{
  const char* ssid     = WLAN_SSID;
  const char* password = WLAN_PSWD;
};
// WiFi credentials
struct wifi_data wifi;
 
struct ifttt_data
{
  const char* resource = "";
  const char* server = "maker.ifttt.com";
};
// IFTTT resource
struct ifttt_data ifttt;
 
int count_daily = 0;
struct measurements
{
  int current;
  int prevAvg;
  int prediction;
};
struct daily_data
{
  int tempCurrent;
  int humidityCurrent;
  struct measurements temp;
  struct measurements humidity;
  int    tempRaw[DAILY_CNT];
  int    humidityRaw[DAILY_CNT];
  String rainPrediction;
  struct iot_image* img; 
};

#ifndef SIMULATION_MODE_SENSOR
//struct sensors
//{
//  DHT dhtSensor;
//};
//struct sensors availSensors;
#endif

struct daily_data dailyData;
 
void setup()
{
  IOT_STATUS status = IOT_SUCCESS;
  Serial.begin(115200);
  status = initWifi();
  if (IOT_SUCCESS != status)
  {
      Serial.println("Error occured during WIFI connection");
      Serial.println("########");
  };
#ifndef SIMULATION_MODE_SENSOR
  // Sensors setup
  if (IOT_SUCCESS == status)
  {
    //availSensors.dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
    dht.begin();
  }
#endif

  dailyData.img = &iot_img;
#ifdef SIMULATION_MODE_IMAGE
  memset(&img_data, 100, IMG_WIDTH * IMG_HEIGHT);
#endif
  Serial.println("Setup...Success");
 
  // Cores init
#ifdef PROCESS_IMG_CORE0
  // Image Core tasks
  xTaskCreatePinnedToCore(
    ImageCoreLoop, /* Function to implement the task */
    "imageCore", /* Name of the task */
    10000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    1,  /* Priority of the task */
    &imageCore,  /* Task handle. */
    0); /* Core where the task should run */
#endif
 
#ifdef PROCESS_SENSOR_CORE1
  // Sensors Core Task
  xTaskCreatePinnedToCore(
      SensorsCoreLoop, /* Function to implement the task */
      "sensorsCore", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      1,  /* Priority of the task */
      &sensorsCore,  /* Task handle. */
      1); /* Core where the task should run */
#endif
}
 
void SensorsCoreLoop( void * parameter)
{
  IOT_STATUS status = IOT_SUCCESS;
 
  while(1)
  {
    // Core sanity check
    Serial.print("SensorsCoreLoop() running on core ");
    Serial.println(xPortGetCoreID());
 
    status = runSensorsProcessing();
 
    if (IOT_SUCCESS != status)
    {
      Serial.print("runSensorsProcessing() problem ... sleeping");
      delay(3000);
    }
   
    // Sleep
    //delay(1000);
  }
 
  Serial.println("SensorsCoreLoop() ending... ");
}
 
void ImageCoreLoop( void * parameter)
{
    IOT_STATUS status = IOT_SUCCESS;
    delay(2000);
    while(1)
    {
      // Core sanity check
      Serial.print("ImageCoreLoop() running on core ");
      Serial.println(xPortGetCoreID());
      
      status = runImageProcessing();
      if (IOT_SUCCESS != status)
      {
        Serial.print("runImageProcessing() problem ... sleeping");
        delay(3000);
      }
      
      delay(2000);
    }
  Serial.println("ImageCoreLoop() ending...");
}
 
void loop()
{
#ifndef PROCESS_SENSOR_CORE1
  // Core sanity check
  Serial.print("loop() running on core ");
  Serial.println(xPortGetCoreID());
  runSensorsProcessing();
#endif
}

IOT_STATUS runImageProcessing()
{
  IOT_STATUS status = IOT_SUCCESS;
  int quantity;
  // Open image
  iot_img.height = IMG_HEIGHT;
  iot_img.width = IMG_WIDTH;
#ifdef SIMULATION_MODE_IMAGE
  iot_img.img_data = (char*) img_data;
#else
  //TODO: read image from disk

#endif
  // Process - information quantity
  status = imageInformationQuantity(iot_img, &quantity);

  if (IOT_SUCCESS == status)
  {
    if (quantity < IMG_INFO_THRESHOLD)
    {
      Serial.println("Image OBSTRUCTED");
      iot_img.valid = 0;
    }
    else
    {
      Serial.println("Image OK");
      iot_img.valid = 1;
    }
  }
  
  return status;
}

IOT_STATUS imageInformationQuantity(struct iot_image img, int* quantity)
{
    IOT_STATUS status = IOT_SUCCESS;
    *quantity = 0;
    // TODO: image format?
    // TODO: covert all image formats to Y only
    int channels = 1;
    int total_info = img.height * img.width * channels;
    int black_info = 0;
    int i, j;
    for (int i = 0; i < img.height; i++)
    {
      for (int j = 0; j < img.width; j++)
      {
        int current_pixel = img.img_data[i * img.width + j];
        black_info = (current_pixel < IMG_PIXEL_THRESHOLD) ? (black_info + 1) : black_info;
      }
    }
    *quantity = (float)(total_info - black_info) / total_info * 100;
    return status;
}

IOT_STATUS runSensorsProcessing()
{  
  IOT_STATUS status = IOT_SUCCESS;
 
  // Sensors stuff
  status = getDHTSensorData((void*)&dailyData);
  if (IOT_SUCCESS == status)
  {
    printDHTSensorData((void*)&dailyData);
  }
 
  // Aggregate sensor new values to stored ones
  dailyData.tempRaw[count_daily] = dailyData.temp.current;
  dailyData.humidityRaw[count_daily] = dailyData.humidity.current;
 
  count_daily++;
 
  // Compute the prediction for today's avg
  status = predictValues((void*)&dailyData);
 
#ifdef RAIN_PREDICTION
  // Calculate raining chances
  status = rainPrediction((void*)&dailyData);
#endif
 
  // Compute yesterday's values and reset the counter
  if (DAILY_CNT == count_daily)
  {
    int i, sumTemp = 0, sumHum = 0;
    for (i = 0; i < DAILY_CNT; i++)
    {
      sumTemp += dailyData.tempRaw[i];
      sumHum += dailyData.humidityRaw[i];
    }
    dailyData.temp.prevAvg = sumTemp / DAILY_CNT;
    dailyData.humidity.prevAvg = sumHum / DAILY_CNT;
    count_daily = 0;
  }
 
  Serial.println(count_daily);
 
  // Check if the hour is the set one
  if (TARGET_TIMESTAMP == count_daily)
  {
#   ifdef SEND_IFTTT_PKG
    if (IOT_SUCCESS == status)
    {
      Serial.println("IFTTT send message");
      status = makeIFTTTRequest((void*)&dailyData);
    }
#   endif
  }
 
  // Error checking
  if (IOT_SUCCESS != status)
  {
    //return;
  }
 
  // Sleep - ms
  delay(DELAY_SEC * 1000);
 
  return status;
}
 
// Linear interpolation
IOT_STATUS predictValues(void *daily)
{
  IOT_STATUS status = IOT_SUCCESS;
  // struct daily_data* data = (struct daily_data*) daily;
 
  // int x0, y0Temp, x1, y1Temp, x, yTemp, y0Hum, y1Hum, yHum;
  // if (count_daily > 0)
  // {
  //   x0 = 0;
  //   y0Temp = data->tempRaw[x0];
  //   y0Hum = data->humidityRaw[x0];
  //   x1 = count_daily - 1;
  //   y1Temp = data->tempRaw[x1];
  //   y1Hum = data->humidityRaw[x1];
  // }
  // x = count_daily;
  // Linear interpolation formula
  // y = ((y0 * (x1 - x)) + (y1 * (x - x0))) / (x1 - x0)
  // yTemp = ((y0Temp * (x1 - x)) + (y0Temp * (x1 - x))) / (x1 - x0);
  // yHum = (y0Hum * (x1 - x)) / (x1 - x0);
 
 
  return IOT_SUCCESS;
}
 
// Zambretti algorithm
IOT_STATUS rainPrediction(void *daily)
{
  IOT_STATUS status = IOT_SUCCESS;
  struct daily_data* data = (struct daily_data*) daily;
  data->rainPrediction = String("N/A");
#ifdef RAIN_PREDICTION
  int Z, P0;
  String forecasetPredictionText;
  int temp = data->temp.current;
  // 1. atmospheric pressure reduced to sea level
  P0 = AIR_PRESSURE * (pow( 1 - ((0.0065 * CITY_ALTITUDE) / (temp + 0.0065 * CITY_ALTITUDE + 273.15)), -5.257));
 
  // 2. Compute pressure trend - steady
  Z = 144 - 0.13 * P0;
 
  // 3. Adjust Z for wind direction: Northerly winds
  Z += 1;
 
  // 4. Adjust Z for the season:
  Z += 1;
 
  // 5. Lookup for the forecast
  switch(Z)
  {
    case 9:
      forecasetPredictionText = "Very Unsettled, Rain";
      break;
    case 10:
      forecasetPredictionText = "Settled Fine";
      break;
    case 11:
      forecasetPredictionText = "Fine Weather";
      break;
    case 12:
      forecasetPredictionText = "Fine, Possibly Showers";
      break;
    case 13:
      forecasetPredictionText = "Fairly Fine, Showers Likely";
      break;
    case 14:
      forecasetPredictionText = "Showery, Bright Intervals";
      break;
    case 15:
      forecasetPredictionText = "Changeable, Some Rain";
      break;
    case 16:
      forecasetPredictionText = "Unsettled, Rain at Times";
      break;
    case 17:
      forecasetPredictionText = "Rain at Frequent Intervals";
      break;
    case 18:
      forecasetPredictionText = "Very Unsettled, Rain";
      break;
    case 19:
      forecasetPredictionText = "Stormy, Much Rain";
      break;
    case 20:
      forecasetPredictionText = "Settled Fine";
      break;
    case 21:
      forecasetPredictionText = "Becoming Fine";
      break;
    default:
      forecasetPredictionText = "Settled Fine";
      break;
  }
  data->rainPrediction = forecasetPredictionText;
  //Serial.println("Forecast: " + forecasetPredictionText);  
#endif
  return status;
}
 
#ifdef SIMULATION_MODE_SENSOR
IOT_STATUS getDHTSensorData(void *daily)
{
  struct daily_data* data = (struct daily_data*) daily;
 
  data->temp.current = 25;
  data->humidity.current = 70;
  
  return IOT_SUCCESS;
}
#else
IOT_STATUS getDHTSensorData(void *daily)
{
  struct daily_data* data = (struct daily_data*) daily;
 
  //data->temp.current = availSensors.dhtSensor.getTempAndHumidity().temperature;
  data->temp.current = dht.readTemperature();

  //data->humidity.current = availSensors.dhtSensor.getTempAndHumidity().humidity;
  data->humidity.current = dht.readHumidity();
  
  return IOT_SUCCESS;
}
#endif

void printDHTSensorData(void *daily)
{
  struct daily_data* data = (struct daily_data*) daily;
  Serial.println("Temp: " + String(data->temp.current) + "°C");
  Serial.println("Humidity: " + String(data->humidity.current) + "%");
  Serial.println("---");
}
 
// Make an HTTP request to the IFTTT web service
IOT_STATUS makeIFTTTRequest(void *daily)
{
  struct daily_data* data = (struct daily_data*) daily;
 
  String current, yesterday, forecast;
 
  current = String(data->temp.current, 2) + "°C; " + String(data->humidity.current, 1) + "%";
  yesterday = String(data->temp.prevAvg, 2) + "°C; " + String(data->humidity.prevAvg, 1) + "%";
  forecast = data->rainPrediction;
  if (0 == iot_img.valid)
  {
    forecast = forecast + "....Image OBSTRUCTED";
  }
  else
  {
    forecast = forecast + "....Image OK";
  }
  // Temperature in Celsius
  String jsonObject = String("{\"value1\":\"") + current + "\",\"value2\":\""
                      + yesterday + "\",\"value3\":\"" + forecast + "\"}";
 
  if (IOT_SUCCESS != sendIFTTTRequest(jsonObject))
  {
    Serial.println("Error occured during IFTTT communication");
    Serial.println("########");
    return IOT_SERVER_ERR;
  }
  else
  {
    Serial.println("IFTTT communication succeed");
  }
 
  return IOT_SUCCESS;
}
 
IOT_STATUS sendIFTTTRequest(String jsonObject)
{
  Serial.print("Connecting to ");
  Serial.print(ifttt.server);
 
  WiFiClient client;
 
  int retries = RETRIES;
 
  while(!!!client.connect(ifttt.server, 80) && (retries-- > 0))
  {
    Serial.print(".");
  }
  Serial.println();
 
  if(!!!client.connected())
  {
 
    Serial.println("Failed to connect...");
    return IOT_SERVER_ERR;
  }
 
  Serial.print("Request resource: ");
  Serial.println(ifttt.resource);
 
  // Send request to IFTTT server
  client.println(String("POST ") + ifttt.resource + " HTTP/1.1");
  client.println(String("Host: ") + ifttt.server);
  client.println("Connection: close\r\nContent-Type: application/json");
 
  client.print("Content-Length: ");
  client.println(jsonObject.length());
  client.println();
  client.println(jsonObject);
 
  int timeout = RETRIES * 10;
  while(!!!client.available() && (timeout-- > 0))
  {
    delay(100);
  }
 
  if(!!!client.available())
  {
    Serial.println("No response...");
    return IOT_SERVER_ERR;
  }
 
  while(client.available())
  {
    Serial.write(client.read());
  }
 
  Serial.println("\nClosing connection...");
  Serial.println("\n##################################\n");
  client.stop();
 
  return IOT_SUCCESS;
}
 
// Establish a Wi-Fi connection with your router
IOT_STATUS initWifi()
{
  Serial.print("Connecting to: ");
  Serial.print(wifi.ssid);
 
  WiFi.begin(wifi.ssid, wifi.password);
 
  int timeout = 10 * 4; // 10 seconds
 
  while(WiFi.status() != WL_CONNECTED && (timeout-- > 0))
  {
    delay(250);
    Serial.print(".");
  }
  Serial.println("");
 
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect, going back to sleep");
    return IOT_WIFI_ERR;
  }
 
  Serial.print("WiFi connected in: ");
  Serial.print(millis());
  Serial.print(", IP address: ");
  Serial.println(WiFi.localIP());
 
  return IOT_SUCCESS;
}
