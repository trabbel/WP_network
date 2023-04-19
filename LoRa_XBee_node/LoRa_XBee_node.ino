#include <Arduino.h>
#include <HardwareSerial.h>
#include <loopTimer.h>
#include "LoRaWan_APP.h"
#include "zigbee.h"
#include "utils.h"
#define MAXIMUM_BUFFER_SIZE                                                 300
#define START_DELIMITER                                                     0x7E
#define RST_PIN                                                             PA0


HardwareSerial xbee(1);
/* OTAA para*/ 
uint8_t devEui[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // This will be created by device chip ID.
uint8_t appEui[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // AKA JoinEUI, it is ok to be all zeros.
char appKeyString[] = "724CBB6B573DD8C01D151FF45F1680FD";
uint8_t appKey[16];  // Will be filled in during setup.

void convert_keystring(void)
{
  const char *pos = appKeyString;
  for (size_t count = 0; count < 16; count++) {
        sscanf(pos, "%2hhx", &appKey[count]);
        pos += 2;
    }
}


/* ABP para*/ 
uint8_t nwkSKey[] = { 0x15, 0xb1, 0xd0, 0xef, 0xa4, 0x63, 0xdf, 0xbe, 0x3d, 0x11, 0x18, 0x1e, 0x1e, 0xc7, 0xda,0x85 };
uint8_t appSKey[] = { 0xd7, 0x2c, 0x78, 0x75, 0x8c, 0xdc, 0xca, 0xbf, 0x55, 0xee, 0x4a, 0x77, 0x8d, 0x16, 0xef,0x67 };
uint32_t devAddr =  ( uint32_t )0x007e6ae1;

/*LoraWan channelsmask, default channels 0-7*/ 
uint16_t userChannelsMask[6]={ 0x00FF,0x0000,0x0000,0x0000,0x0000,0x0000 };

/*LoraWan region, select in arduino IDE tools*/
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/*LoraWan Class, Class A and Class C are supported*/
DeviceClass_t  loraWanClass = CLASS_C;

/*the application data transmission duty cycle.  value in [ms].*/
uint32_t appTxDutyCycle = 1000 * 10;

/*OTAA or ABP*/
bool overTheAirActivation = true;

/*ADR enable*/
bool loraWanAdr = false;

/* Indicates if the node is sending confirmed or unconfirmed messages */
bool isTxConfirmed = false;

/* Application port */
uint8_t appPort = 2;
/*!
* Number of trials to transmit the frame, if the LoRaMAC layer did not
* receive an acknowledgment. The MAC performs a datarate adaptation,
* according to the LoRaWAN Specification V1.0.2, chapter 18.4, according
* to the following table:
*
* Transmission nb | Data Rate
* ----------------|-----------
* 1 (first)       | DR
* 2               | DR
* 3               | max(DR-1,0)
* 4               | max(DR-1,0)
* 5               | max(DR-2,0)
* 6               | max(DR-2,0)
* 7               | max(DR-3,0)
* 8               | max(DR-3,0)
*
* Note, that if NbTrials is set to 1 or 2, the MAC will not decrease
* the datarate, in case the LoRaMAC layer did not receive an acknowledgment
*/ 
uint8_t confirmedNbTrials = 4;

/* Prepares the payload of the frame */ 
static void prepareTxFrame(int mode, uint8_t port )
{
  /*appData size is LORAWAN_APP_DATA_MAX_SIZE which is defined in "commissioning.h".
  *appDataSize max value is LORAWAN_APP_DATA_MAX_SIZE.
  *if enabled AT, don't modify LORAWAN_APP_DATA_MAX_SIZE, it may cause system hanging or failure.
  *if disabled AT, LORAWAN_APP_DATA_MAX_SIZE can be modified, the max value is reference to lorawan region and SF.
  *for example, if use REGION_CN470, 
  *the max value for different DR can be found in MaxPayloadOfDatarateCN470 refer to DataratesCN470 and BandwidthsCN470 in "RegionCN470.h".
  */ 
    if(mode == -1){
      appDataSize = 4;
      appData[0] = 0x00;
      appData[1] = 0x01;
      appData[2] = 0x02;
      appData[3] = 0x03;
    }else{
      appDataSize=1;
      appData[0] = mode;
    }
}


uint64_t sink_addr = 0x0013a20041f223b8;


void downLinkDataHandle(McpsIndication_t *mcpsIndication){
  Serial.printf("+REV DATA:%s,RXSIZE %d,PORT %d\r\n",mcpsIndication->RxSlot?"RXWIN2":"RXWIN1",mcpsIndication->BufferSize,mcpsIndication->Port);
  Serial.print("+REV DATA:");
  uint8_t identifier = mcpsIndication->Buffer[0];
  Serial.printf("identifier: %02x\n", identifier);
  if (identifier == 0x00){
    sink_addr = 0;
    for(uint8_t i=1;i<mcpsIndication->BufferSize;i++){
      sink_addr |= (uint64_t)mcpsIndication->Buffer[i] << ((8-i)*8);
    }

  }
    for(uint8_t i=0;i<mcpsIndication->BufferSize;i++)
    {
      Serial.printf("%02X",mcpsIndication->Buffer[i]);
    }
    Serial.println();
  


}



// Define RX and TX global buffer
char tx_buf[MAXIMUM_BUFFER_SIZE] = {0};
char rx_buf[MAXIMUM_BUFFER_SIZE] = {0};
int tx_length = 0;

// Delays for software-"multithreading"/scheduling
millisDelay sendDelay;




bool send_status = false;
int zigbeeFailed = 0;

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
        //Serial.printf("Payload Size: %i\n", length);

        if(length <= 0){
            return;
        }
        /*for (int i = 0 ; i<length; i++){
            Serial.printf("%02x", buffer[i]);
        }Serial.printf("\n");*/
        if(result.frameID == 0x90){
            Serial.printf("%.*s\n", length - 12, buffer + 12);
        }else if(result.frameID == 0x8b){
          if(buffer[5] != 0x00){
            zigbeeFailed++;
          }
        }
    }
    return;
}

int i =0;
void setup() {
  i++;
    Serial.begin(115200);
    xbee.begin(115200, SERIAL_8N1, 12, 13);
    digitalWrite(15, LOW); 
  Serial.printf("Setup loop number %d \n", i);
  convert_keystring();
    Mcu.begin();
  deviceState = DEVICE_STATE_INIT;

    delay(100);
    digitalWrite(15, HIGH); 
    delay(100);
    sendDelay.start(5000);
    Serial.printf("Setup finished-------------------------------------------------------------------------------------------------------------\n");
}


void sendMessage() {
  if (sendDelay.justFinished()) {
    sendDelay.repeat();
    //Serial.printf("Send message\n");
    char payload[] = "Zigbee_LoRa";
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

switch( deviceState )
  {
    case DEVICE_STATE_INIT:
    {
#if(LORAWAN_DEVEUI_AUTO)
      LoRaWAN.generateDeveuiByChipID();
#endif
      LoRaWAN.init(loraWanClass,loraWanRegion);
      break;
    }
    case DEVICE_STATE_JOIN:
    {
      LoRaWAN.join();
      break;
    }
    case DEVICE_STATE_SEND:
    {
      //if(send_status == true){
      prepareTxFrame(zigbeeFailed, appPort );
      zigbeeFailed = 0;
      LoRaWAN.send();
      //send_status = false;}
      deviceState = DEVICE_STATE_CYCLE;
      break;
    }
    case DEVICE_STATE_CYCLE:
    {
      // Schedule next packet transmission
      txDutyCycleTime = appTxDutyCycle + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
      LoRaWAN.cycle(txDutyCycleTime);
      deviceState = DEVICE_STATE_SLEEP;
      break;
    }
    case DEVICE_STATE_SLEEP:
    {
      LoRaWAN.sleep(loraWanClass);
      break;
    }
    default:
    {
      deviceState = DEVICE_STATE_INIT;
      break;
    }
  }

}