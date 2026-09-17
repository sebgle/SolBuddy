#include <Adafruit_Sensor.h>
#include <Adafruit_TMP117.h>
#include <Wire.h>
#include <Adafruit_AS7343.h>

#define MUTEX_TIMEOUT 5000  // 5s timeout


Adafruit_TMP117 tmp11x;
Adafruit_AS7343 as7343;
SemaphoreHandle_t wireBufferMutex = NULL;
QueueHandle_t readings = NULL;
int dropped_payloads_temp;
int dropped_payloads_light;

typedef enum {
  SENSOR_TEMP,
  SENSOR_LIGHT
} Sensor;

typedef struct {
  Sensor sensor_type;
  TickType_t timestamp;
  float temp;
  uint16_t counts[18];
  bool valid;
} Payload;

void readTemp(void* pvParameters) {

  TickType_t wakeTime = xTaskGetTickCount(); // get curr tick count

  for (;;) {
    Payload pload = {};
    sensors_event_t sreading;

    pload.sensor_type = SENSOR_TEMP;
    pload.timestamp = xTaskGetTickCount();

    if (xSemaphoreTake(wireBufferMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT))) {
      if (!tmp11x.getEvent(&sreading)) {
        pload.valid = false;
      } else {
        pload.valid = true;
        pload.temp = sreading.temperature; 
      }
      xSemaphoreGive(wireBufferMutex);
    } else {
      pload.valid = false;
    }

    if (xQueueSendToBack(readings, &pload, pdMS_TO_TICKS(10)) != pdPASS) {
      dropped_payloads_temp++;
    }

    vTaskDelayUntil(&wakeTime, pdMS_TO_TICKS(1000));
  }
}

void readLight(void* pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(500));
  TickType_t wakeTime = xTaskGetTickCount();
  for (;;) {
    Payload pload = {};
    pload.sensor_type = SENSOR_LIGHT;
    pload.timestamp = xTaskGetTickCount();

    if (xSemaphoreTake(wireBufferMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT))) {
      if(!as7343.readAllChannels(pload.counts)) {
        pload.valid = false;
      } else {
        pload.valid = true;
      }
      xSemaphoreGive(wireBufferMutex);
    } else {
      pload.valid = false;
    }

    if (xQueueSendToBack(readings, &pload, pdMS_TO_TICKS(10)) != pdPASS) {
      dropped_payloads_light++;
    }
    vTaskDelayUntil(&wakeTime, pdMS_TO_TICKS(1000));
  }
}

void printReadings(void* pvParameters) {
  for(;;) {
    Payload pload_to_print = {};
    if (xQueueReceive(readings, &pload_to_print, portMAX_DELAY) == pdPASS) {
      if (pload_to_print.valid) {
        switch (pload_to_print.sensor_type) {
          case SENSOR_LIGHT:
            Serial.print("AS7343 @ ");
            Serial.print((uint32_t)tick2ms(pload_to_print.timestamp));
            Serial.println(": ");
            Serial.print("F1  (405nm violet):     ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F1]);

            Serial.print("F2  (425nm violet-blue):");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F2]);

            Serial.print("FZ  (450nm blue):       ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_FZ]);

            Serial.print("F3  (475nm blue-cyan):  ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F3]);

            Serial.print("F4  (515nm green):      ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F4]);

            Serial.print("F5  (550nm green-yel):  ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F5]);

            Serial.print("FY  (555nm yellow-grn): ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_FY]);

            Serial.print("FXL (600nm orange):     ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_FXL]);

            Serial.print("F6  (640nm red):        ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F6]);

            Serial.print("F7  (690nm deep red):   ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F7]);

            Serial.print("F8  (745nm near-IR):    ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_F8]);

            Serial.print("NIR (855nm near-IR):    ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_NIR]);

            // Print clear/VIS channels (one from each cycle)
            Serial.print("VIS (clear):            ");
            Serial.println(pload_to_print.counts[AS7343_CHANNEL_VIS_TL_0]);
            Serial.println("");
            break;

          case SENSOR_TEMP:
            Serial.print("TMP117 @ ");
            Serial.print((uint32_t)tick2ms(pload_to_print.timestamp));
            Serial.print(": ");
            Serial.println(pload_to_print.temp);
            Serial.println("");
            break;
        }
      } else {
        Serial.print("invalid reading from : ");
        if (pload_to_print.sensor_type == SENSOR_LIGHT) {
          Serial.print("AS7343 ");
        } else {
          Serial.print("TMP117 ");
        }
        Serial.print(" @ ");
        Serial.println((uint32_t)tick2ms(pload_to_print.timestamp));
        Serial.println("");
      }
      
    }
  }
}

void setup(void) {

  /* PERIPHERAL SET UP*/
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Adafruit TMP117 & AS7343 test");
  if (!tmp11x.begin()) {
    Serial.println("Failed to find TMP117 chip");
    while (1) { delay(10); }
  }
  Serial.println("TMP117 Found!");
  if (!as7343.begin()) {
    Serial.println("Could not find AS7343 sensor!");
    while (1) {
      delay(10);
    }
  }
  Serial.println("AS7343 found!");
  as7343.setGain(AS7343_GAIN_64X);
  as7343.setATIME(29);  // Integration cycles
  as7343.setASTEP(599); // Step size

  /* WIRE BUFFER MUTEX */
  wireBufferMutex = xSemaphoreCreateMutex();
  if (wireBufferMutex == NULL) {
    Serial.println("Failed to create mutex!");
    while (1) { delay(10); };
  }

  /* QUEUE SET UP */
  readings = xQueueCreate(8, sizeof(Payload));
  if (readings == NULL) {
    Serial.println("Failed to create queue!");
    while (1) { delay(10); };
  }


  /* CREATE TASKS */
  if (xTaskCreate(readTemp, "readTemp", 256*4, NULL, TASK_PRIO_NORMAL, NULL) != pdPASS) {
    Serial.println("failed to create readtemp task");
    while (1) { delay(10); }
  }
  if (xTaskCreate(readLight, "readLight", 256*4, NULL, TASK_PRIO_NORMAL, NULL) != pdPASS) {
    Serial.println("failed to create readlight task");
    while (1) { delay(10); }
  }
  if (xTaskCreate(printReadings, "printReadings", 256*4, NULL, TASK_PRIO_NORMAL, NULL) != pdPASS) {
    Serial.println("failed to create printreadings task");
    while (1) { delay(10); }
  }
}

void loop() {
  delay(1000*1000);
}
