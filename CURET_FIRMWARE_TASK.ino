#include <Arduino.h>

#define LED_PIN 32          
#define BAUD_RATE 115200    

typedef struct {
  char type;             
  int16_t value;         
  uint32_t timestamp_ms; 
} Command_t;

QueueHandle_t commandQueue;

volatile uint32_t last_valid_command_ms = 0; 
uint8_t current_output_state = 0;   
char last_cmd_type = 'N';           
bool fail_safe_active = false;      

void Actuator_SetOutput(uint8_t percent) {
  uint32_t duty = (percent * 255) / 100;
  ledcWrite(LED_PIN, duty); 
  current_output_state = percent;
}

void UART_Println(const char *msg) {
  Serial.println(msg);
}

void Task_CommandRX(void *pvParameters) {
  String inputBuffer = "";
  for (;;) {
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (inputBuffer.length() > 0) {
          inputBuffer.trim(); 
          
          Command_t cmd;
          cmd.timestamp_ms = millis();
          bool valid = false;

          if (inputBuffer.equals("PING")) {
            cmd.type = 'P'; cmd.value = 0; valid = true;
            UART_Println("PONG");
          } else if (inputBuffer.startsWith("THROTTLE ")) {
            cmd.type = 'T'; cmd.value = inputBuffer.substring(9).toInt(); valid = true;
          } else if (inputBuffer.startsWith("STEER ")) {
            cmd.type = 'S'; cmd.value = inputBuffer.substring(6).toInt(); valid = true;
          } else if (inputBuffer.startsWith("BRAKE ")) {
            cmd.type = 'B'; cmd.value = inputBuffer.substring(6).toInt(); valid = true;
          }

          if (valid) {
            last_valid_command_ms = millis(); 
            last_cmd_type = cmd.type;
            
            if (fail_safe_active) {
              fail_safe_active = false; 
            }
            
            if (cmd.type == 'B') {
              xQueueSendToFront(commandQueue, &cmd, portMAX_DELAY);
            } else {
              xQueueSendToBack(commandQueue, &cmd, portMAX_DELAY);
            }
          } else {
            UART_Println("LOG: Malformed command ignored.");
          }
          inputBuffer = ""; 
        }
      } else {
        inputBuffer += c; 
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5)); 
  }
}

void Task_Actuate(void *pvParameters) {
  Command_t rxCmd;
  for (;;) {
    if (xQueueReceive(commandQueue, &rxCmd, portMAX_DELAY) == pdPASS) {
      
      if (rxCmd.type == 'B' && rxCmd.value > 0) {
        Actuator_SetOutput(0); 
        xQueueReset(commandQueue); 
      } 
      else if (rxCmd.type == 'T' && !fail_safe_active) {
        Actuator_SetOutput(rxCmd.value); 
      } 

      if (!fail_safe_active) {
        Serial.print("ACTUATE: Executed ");
        Serial.print(rxCmd.type);
        Serial.print(" | Value: ");
        Serial.println(rxCmd.value);
      }
    }
  }
}

void Task_Watchdog(void *pvParameters) {
  bool blink_state = false;
  for (;;) {
    if ((millis() - last_valid_command_ms > 500) && last_valid_command_ms != 0) {
      if (!fail_safe_active) {
        fail_safe_active = true;
        UART_Println("LINK LOST, failing safe");
      }
      
      blink_state = !blink_state;
      ledcWrite(LED_PIN, blink_state ? 255 : 0);
      
      vTaskDelay(pdMS_TO_TICKS(100)); 
    } else {
      vTaskDelay(pdMS_TO_TICKS(50)); 
    }
  }
}

void Task_Status(void *pvParameters) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    Serial.print("STATUS -> Uptime: ");
    Serial.print(millis() / 1000);
    Serial.print("s | Last Cmd: ");
    Serial.print(last_cmd_type);
    Serial.print(" | Output: ");
    Serial.print(current_output_state);
    Serial.println("%");
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  
  ledcAttach(LED_PIN, 5000, 8); 
  Actuator_SetOutput(0);

  commandQueue = xQueueCreate(10, sizeof(Command_t));

  xTaskCreate(Task_CommandRX, "RX",   2048, NULL, 4, NULL);
  xTaskCreate(Task_Actuate,   "ACT",  2048, NULL, 3, NULL);
  xTaskCreate(Task_Watchdog,  "WDG",  1024, NULL, 2, NULL);
  xTaskCreate(Task_Status,    "STAT", 1024, NULL, 1, NULL);
}

void loop() {
  vTaskDelete(NULL); 
}