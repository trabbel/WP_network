#include <Arduino.h>
#include <HardwareSerial.h>
#include <loopTimer.h>
#ifdef TARGET_STM_32
#include "stm32wbxx_hal.h"
//#elif TARGET_ESP_32
//#include <lorawan.h>//"ESP32_LoRaWAN.h"
#endif
#include "zigbee.h"
#include "utils.h"
#define MAXIMUM_BUFFER_SIZE                                                 300
#define START_DELIMITER                                                     0x7E
#define RST_PIN                                                             PA0


#ifdef TARGET_STM_32
HardwareSerial xbee(TX_PIN, RX_PIN);
#elif TARGET_ESP_32
HardwareSerial xbee(1);
#endif





// Define RX and TX global buffer
char tx_buf[MAXIMUM_BUFFER_SIZE] = {0};
char rx_buf[MAXIMUM_BUFFER_SIZE] = {0};
int tx_length = 0;
uint64_t sink_addr = 0x0013a20041f223b8;

// Delays for software-"multithreading"/scheduling
millisDelay sendDelay;

int measurement_interval = 5000;


/*
############ TODO #############
Change void to int, return error codes
*/
static void rx_callback(char *buffer){

    int length = 0;
    char c;
    parsedFrame result;
    int timeoutCnt = 0;
    // Wait until a frame delimiter arrives
    if(xbee.available()>0){
        c = 0;
        while(c != START_DELIMITER){
            if(waitForByte(xbee)==0){
                return;
            }
            c = xbee.read();
        }
        length = 0;
        // Parse the frame. In case of an error, a negative length
        // is returned
        result = readFrame(buffer, xbee);
        length = result.length;
        Serial.printf("Payload Size: %i\n", length);

        if(length <= 0){
            return;
        }
        for (int i = 0 ; i<length; i++){
            Serial.printf("%02x", buffer[i]);
        }Serial.printf("\n");

        if(result.frameID == 0x90){
            sendDelay.stop();
            Serial.printf("%.*s\n", length - 12, buffer + 12);
            char identifier = buffer[12];
            Serial.printf("identifier: %02x\n", identifier);
            if (identifier == 0x00){
              sink_addr = 0;
              for(int i=0;i<8;i++){
                sink_addr |= (uint64_t)buffer[i+13] << ((7-i)*8);
              }
            }else if (identifier == 0x01){
              measurement_interval = 0;
              for(int i=0;i<4; i++){
                measurement_interval |= (int)buffer[i+13] << ((3-i)*8);
              }
            }
            sendDelay.start(measurement_interval);
      }
    }
    return;
}


void setup() {
    Serial.begin(115200);
    #ifdef TARGET_STM_32
    xbee.begin(115200);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
    #elif TARGET_ESP_32
    xbee.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    digitalWrite(15, LOW); 
    #endif


    delay(100);
    #ifdef TARGET_STM_32
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
    #elif TARGET_ESP_32
    digitalWrite(15, HIGH); 
    #endif
    delay(100);
    sendDelay.start(measurement_interval);
    Serial.printf("Setup finished\n");
}


void sendMessage() {
  if (sendDelay.justFinished()) {
    sendDelay.repeat();
    Serial.printf("Send message\n");
    char payload[] = "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata";
    tx_length = writeFrame(tx_buf, 0x01, 0xFFFE, sink_addr, payload, sizeof(payload)-1);
    xbee.write(tx_buf, tx_length);
  }
}


void loop() {
    sendMessage();
    /*Serial.printf("Payload length: %i\n", tx_length);
    for (int i = 0 ; i<tx_length; i++){
            Serial.printf("%02x", tx_buf[i]);
        }Serial.printf("\n");*/
    rx_callback(rx_buf);
}