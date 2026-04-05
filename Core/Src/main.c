/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <string.h>
#include <stdio.h>

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);

uint16_t gatt_service_handle = 0;
uint16_t tx_char_handle = 0;
uint16_t rx_char_handle = 0;
uint16_t connection_handle = 0xFFFF;
uint8_t  phone_subscribed = 0;

/* ─────────────────────────────────────────
   HTU21D definitions
   ───────────────────────────────────────── */
#define HTU21D_ADDR       (0x40 << 1)
#define HTU21D_SOFT_RESET 0xFE

/* ─────────────────────────────────────────
   Log helpers
   ───────────────────────────────────────── */
void logMsg(const char* msg) {
  HAL_UART_Transmit(&huart2,
    (uint8_t*)msg, strlen(msg), 1000);
}

void logHex(const char* label,
            uint8_t* buf, uint8_t len) {
  char tmp[6];
  logMsg(label);
  logMsg(": ");
  for(int i = 0; i < len; i++) {
    uint8_t hi = (buf[i] >> 4) & 0x0F;
    uint8_t lo = buf[i] & 0x0F;
    tmp[0]='0'; tmp[1]='x';
    tmp[2]=hi<10?'0'+hi:'A'+hi-10;
    tmp[3]=lo<10?'0'+lo:'A'+lo-10;
    tmp[4]=' '; tmp[5]='\0';
    logMsg(tmp);
  }
  logMsg("\r\n");
}

/* ─────────────────────────────────────────
   HCI helpers
   ───────────────────────────────────────── */
void sendHCI(uint8_t* cmd, uint8_t len) {
  HAL_UART_Transmit(&huart1, cmd, len, 1000);
}

uint8_t waitResponse(uint8_t* buf,
                     uint8_t maxLen,
                     uint32_t timeoutMs) {
  uint8_t count = 0;
  uint32_t start = HAL_GetTick();
  while(HAL_GetTick() - start < timeoutMs) {
    uint8_t b;
    if(HAL_UART_Receive(&huart1, &b, 1, 10)
       == HAL_OK) {
      buf[count++] = b;
      uint32_t lastByte = HAL_GetTick();
      while(count < maxLen) {
        if(HAL_UART_Receive(&huart1, &b, 1, 10)
           == HAL_OK) {
          buf[count++] = b;
          lastByte = HAL_GetTick();
        } else if(HAL_GetTick()-lastByte > 50) {
          break;
        }
      }
      break;
    }
  }
  return count;
}

void checkResponse(const char* name,
                   uint8_t* buf, uint8_t len) {
  if(len == 0) {
    logMsg(name);
    logMsg(": NO RESPONSE\r\n");
    return;
  }
  logHex(name, buf, len);
  if(buf[0]==0x04 && buf[1]==0x0E && len>6) {
    if(buf[6]==0x00) logMsg("  -> SUCCESS\r\n");
    else {
      char tmp[5];
      uint8_t hi=(buf[6]>>4)&0x0F;
      uint8_t lo=buf[6]&0x0F;
      tmp[0]='0'; tmp[1]='x';
      tmp[2]=hi<10?'0'+hi:'A'+hi-10;
      tmp[3]=lo<10?'0'+lo:'A'+lo-10;
      tmp[4]='\0';
      logMsg("  -> ERROR: ");
      logMsg(tmp);
      logMsg("\r\n");
    }
  } else {
    logMsg("  -> unexpected\r\n");
  }
}

/* ─────────────────────────────────────────
   HTU21D functions
   ───────────────────────────────────────── */
uint8_t HTU21D_Init(void) {
  uint8_t cmd = HTU21D_SOFT_RESET;
  if(HAL_I2C_Master_Transmit(&hi2c1,
       HTU21D_ADDR, &cmd, 1, 1000) != HAL_OK) {
    logMsg("  HTU21D reset failed\r\n");
    return 0;
  }
  HAL_Delay(15);
  if(HAL_I2C_IsDeviceReady(&hi2c1,
       HTU21D_ADDR, 3, 1000) != HAL_OK) {
    logMsg("  HTU21D not found\r\n");
    return 0;
  }
  logMsg("  HTU21D ready\r\n");
  return 1;
}

// Returns temperature in tenths of degree
// e.g. 245 = 24.5C
int16_t HTU21D_ReadTemperature(void) {
  uint8_t cmd = 0xF3;
  uint8_t data[3];
  if(HAL_I2C_Master_Transmit(&hi2c1,
       HTU21D_ADDR, &cmd, 1, 1000) != HAL_OK) {
    return -999;
  }
  HAL_Delay(50);
  if(HAL_I2C_Master_Receive(&hi2c1,
       HTU21D_ADDR, data, 3, 1000) != HAL_OK) {
    return -999;
  }
  uint16_t raw = ((uint16_t)data[0] << 8)
                 | data[1];
  raw &= 0xFFFC;
  float t = ((float)raw * 175.72f /
              65536.0f) - 46.85f;
  return (int16_t)(t * 10);
}

// Returns humidity as whole percentage
int16_t HTU21D_ReadHumidity(void) {
  uint8_t cmd = 0xF5;
  uint8_t data[3];
  if(HAL_I2C_Master_Transmit(&hi2c1,
       HTU21D_ADDR, &cmd, 1, 1000) != HAL_OK) {
    return -999;
  }
  HAL_Delay(20);
  if(HAL_I2C_Master_Receive(&hi2c1,
       HTU21D_ADDR, data, 3, 1000) != HAL_OK) {
    return -999;
  }
  uint16_t raw = ((uint16_t)data[0] << 8)
                 | data[1];
  raw &= 0xFFFC;
  float h = ((float)raw * 125.0f /
              65536.0f) - 6.0f;
  if(h < 0) h = 0;
  if(h > 100) h = 100;
  return (int16_t)h;
}

/* ─────────────────────────────────────────
   BLE init sequence
   ───────────────────────────────────────── */
void hciReset(void) {
  logMsg("[1] HCI Reset...\r\n");
  uint8_t cmd[] = {0x01, 0x03, 0x0C, 0x00};
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  checkResponse("HCI Reset", buf, len);
}

void readRandomAddress(uint8_t* addr) {
  logMsg("[2] Reading random address...\r\n");
  uint8_t cmd[] = {0x01, 0xCD, 0xFC, 0x01, 0x80};
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  if(len > 8 && buf[0]==0x04 &&
     buf[1]==0x0E && buf[6]==0x00) {
    memcpy(addr, &buf[8], 6);
    logMsg("  -> Got address\r\n");
  } else {
    addr[0]=0x01; addr[1]=0x02;
    addr[2]=0x03; addr[3]=0x04;
    addr[4]=0x05; addr[5]=0xC0;
    logMsg("  -> Using default\r\n");
  }
}

void writeRandomAddress(uint8_t* addr) {
  logMsg("[3] Writing random address...\r\n");
  uint8_t cmd[12];
  cmd[0]=0x01; cmd[1]=0xCC;
  cmd[2]=0xFC; cmd[3]=0x08;
  cmd[4]=0x2E; cmd[5]=0x06;
  memcpy(&cmd[6], addr, 6);
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  if(len > 0 && buf[0]==0x04 &&
     buf[1]==0x0E && buf[6]==0x00) {
    logMsg("  -> SUCCESS\r\n");
  } else {
    logMsg("  -> Using built-in address (OK)\r\n");
  }
}

void gattInit(void) {
  logMsg("[4] GATT Init...\r\n");
  uint8_t cmd[] = {0x01, 0x01, 0xFD, 0x00};
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  checkResponse("GATT Init", buf, len);
}

void gapInit(void) {
  logMsg("[5] GAP Init...\r\n");
  uint8_t cmd[] = {
    0x01, 0x8A, 0xFC, 0x04,
    0x01, 0x00, 0x08, 0x01
  };
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  checkResponse("GAP Init", buf, len);
}

void addGattService(void) {
  logMsg("[6] Adding GATT Service...\r\n");
  uint8_t cmd[] = {
    0x01, 0x02, 0xFD, 0x05,
    0x01, 0x00, 0xA0, 0x01, 0x08
  };
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  logHex("Add Service", buf, len);
  if(len >= 9 && buf[0]==0x04 &&
     buf[1]==0x0E && buf[6]==0x00) {
    gatt_service_handle = buf[7] | (buf[8] << 8);
    logMsg("  -> SUCCESS\r\n");
  } else {
    logMsg("  -> ERROR\r\n");
  }
}

void addTxCharacteristic(void) {
  logMsg("[7] Adding TX Characteristic...\r\n");
  uint8_t cmd[] = {
    0x01, 0x04, 0xFD, 0x0C,
    (uint8_t)(gatt_service_handle & 0xFF),
    (uint8_t)(gatt_service_handle >> 8),
    0x01, 0x01, 0xA0,
    0x14, 0x00,
    0x12,   // Notify + Read
    0x00, 0x00, 0x10, 0x01
  };
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  logHex("Add TX Char", buf, len);
  if(len >= 9 && buf[0]==0x04 &&
     buf[1]==0x0E && buf[6]==0x00) {
    tx_char_handle = buf[7] | (buf[8] << 8);
    logMsg("  -> SUCCESS\r\n");
    uint8_t h[2] = {
      (uint8_t)(tx_char_handle & 0xFF),
      (uint8_t)(tx_char_handle >> 8)
    };
    logHex("  TX handle", h, 2);
  } else {
    logMsg("  -> ERROR\r\n");
  }
}

void addRxCharacteristic(void) {
  logMsg("[8] Adding RX Characteristic...\r\n");
  uint8_t cmd[] = {
    0x01, 0x04, 0xFD, 0x0C,
    (uint8_t)(gatt_service_handle & 0xFF),
    (uint8_t)(gatt_service_handle >> 8),
    0x01, 0x02, 0xA0,
    0x14, 0x00,
    0x04,   // Write without response
    0x00,
    0x01,   // Notify host on write
    0x10, 0x01
  };
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  logHex("Add RX Char", buf, len);
  if(len >= 9 && buf[0]==0x04 &&
     buf[1]==0x0E && buf[6]==0x00) {
    rx_char_handle = buf[7] | (buf[8] << 8);
    logMsg("  -> SUCCESS\r\n");
    uint8_t h[2] = {
      (uint8_t)(rx_char_handle & 0xFF),
      (uint8_t)(rx_char_handle >> 8)
    };
    logHex("  RX handle", h, 2);
  } else {
    logMsg("  -> ERROR\r\n");
  }
}

void setAdvConfig(void) {
  logMsg("[9] Set Adv Config...\r\n");
  uint8_t cmd[] = {
    0x01, 0xAB, 0xFC, 0x1B,
    0x00, 0x02, 0x13, 0x00,
    0x40, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00,
    0x07, 0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00, 0x7F, 0x01, 0x00, 0x01, 0x00, 0x00
  };
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  checkResponse("Adv Config", buf, len);
}

void setAdvData(void) {
  logMsg("[10] Set Adv Data...\r\n");
  uint8_t advData[] = {
    0x02, 0x01, 0x06,
    0x09, 0x09,
    0x57, 0x42, 0x30, 0x35,
    0x54, 0x45, 0x53, 0x54
  };
  uint8_t advLen = sizeof(advData);
  uint8_t cmd[32];
  cmd[0]=0x01; cmd[1]=0xAD;
  cmd[2]=0xFC; cmd[3]=3+advLen;
  cmd[4]=0x00; cmd[5]=0x03;
  cmd[6]=advLen;
  memcpy(&cmd[7], advData, advLen);
  uint8_t buf[32];
  sendHCI(cmd, 7+advLen);
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  checkResponse("Adv Data", buf, len);
}

void setAdvEnable(void) {
  logMsg("Enabling advertising...\r\n");
  uint8_t cmd[] = {
    0x01, 0xAC, 0xFC, 0x06,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00
  };
  uint8_t buf[32];
  sendHCI(cmd, sizeof(cmd));
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  checkResponse("Adv Enable", buf, len);
}

/* ─────────────────────────────────────────
   Send notification to phone
   ───────────────────────────────────────── */
void sendNotification(uint8_t* data,
                      uint8_t dataLen) {
  if(connection_handle == 0xFFFF) return;
  if(!phone_subscribed) return;
  uint8_t cmd[40];
  uint16_t attr_handle = tx_char_handle + 1;
  cmd[0]  = 0x01;
  cmd[1]  = 0x2F;
  cmd[2]  = 0xFD;
  cmd[3]  = 7 + dataLen;
  cmd[4]  = (uint8_t)(connection_handle & 0xFF);
  cmd[5]  = (uint8_t)(connection_handle >> 8);
  cmd[6]  = (uint8_t)(attr_handle & 0xFF);
  cmd[7]  = (uint8_t)(attr_handle >> 8);
  cmd[8]  = 0x01;
  cmd[9]  = dataLen;
  cmd[10] = 0x00;
  memcpy(&cmd[11], data, dataLen);
  uint8_t buf[32];
  sendHCI(cmd, 11 + dataLen);
  uint8_t len = waitResponse(buf, sizeof(buf), 1000);
  if(len > 0 && buf[0]==0x04 &&
     buf[1]==0x0E && len > 6) {
    if(buf[6]==0x00) logMsg("  Notification sent\r\n");
    else logMsg("  Notification error\r\n");
  }
}

/* ─────────────────────────────────────────
   Helpers
   ───────────────────────────────────────── */
void toUpper(uint8_t* data, uint8_t len) {
  for(int i = 0; i < len; i++) {
    if(data[i] >= 'a' && data[i] <= 'z')
      data[i] -= 32;
  }
}

uint8_t trimSpaces(uint8_t* data,
                   uint8_t len) {
  while(len > 0 && data[len-1] == ' ')
    len--;
  return len;
}

/* ─────────────────────────────────────────
   Handle vendor events
   CCCD write = phone subscribing
   RX write   = LED command from phone
   ───────────────────────────────────────── */
void handleVendorEvent(uint8_t* buf,
                       uint8_t len) {
  uint16_t attr_handle = 0;
  uint8_t* data = NULL;
  uint16_t dlen = 0;

  if(buf[0]==0x04 && buf[1]==0xFF &&
     len > 11) {
    attr_handle = buf[7] | (buf[8] << 8);
    dlen = buf[11];
    data = &buf[12];
  }
  else if(buf[0]==0x82 && buf[1]==0xFF &&
          len > 11) {
    attr_handle = buf[8] | (buf[9] << 8);
    dlen = buf[10] | (buf[11] << 8);
    data = &buf[12];
  }

  if(data == NULL || dlen == 0) return;

  uint16_t cccd   = tx_char_handle + 2;
  uint16_t rx_val = rx_char_handle + 1;

  /* ── CCCD write — subscription ── */
  if(attr_handle == cccd) {
    uint16_t val = data[0] | (data[1] << 8);
    if(val == 0x0001) {
      phone_subscribed = 1;
      logMsg(">>> Phone SUBSCRIBED\r\n");
    } else {
      phone_subscribed = 0;
      logMsg(">>> Phone UNSUBSCRIBED\r\n");
    }
    return;
  }

  /* ── RX write — LED command ── */
  if(attr_handle == rx_val && dlen > 0) {

    logMsg(">>> Received: ");
    char printable[32] = {0};
    uint8_t pl = dlen < 31 ?
                 (uint8_t)dlen : 31;
    for(int i = 0; i < pl; i++) {
      printable[i] = (data[i] >= 32 &&
                      data[i] < 127)
                     ? data[i] : '.';
    }
    logMsg(printable);
    logMsg("\r\n");

    dlen = trimSpaces(data, (uint8_t)dlen);
    toUpper(data, (uint8_t)dlen);

    if(dlen == 2 &&
       data[0]=='O' && data[1]=='N') {
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6,
                        GPIO_PIN_SET);
      logMsg("  LED ON\r\n");
    }
    else if(dlen == 3 &&
            data[0]=='O' && data[1]=='F' &&
            data[2]=='F') {
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6,
                        GPIO_PIN_RESET);
      logMsg("  LED OFF\r\n");
    }
    else if(dlen == 6 &&
            data[0]=='T' && data[1]=='O' &&
            data[2]=='G' && data[3]=='G' &&
            data[4]=='L' && data[5]=='E') {
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6);
      logMsg("  LED TOGGLED\r\n");
    }
    else {
      logMsg("  Unknown command\r\n");
    }
    return;
  }
}

/* ─────────────────────────────────────────
   Main
   ───────────────────────────────────────── */
int main(void) {
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();

  HAL_Delay(1000);
  logMsg("================================\r\n");
  logMsg("  BLE LED + Sensor Control\r\n");
  logMsg("================================\r\n\r\n");

  logMsg("Initializing HTU21D...\r\n");
  if(!HTU21D_Init()) {
    logMsg("  HTU21D FAILED - check wiring\r\n");
  }

  HAL_Delay(2000);

  uint8_t randomAddr[6];

  hciReset();            HAL_Delay(500);
  readRandomAddress(randomAddr); HAL_Delay(200);
  writeRandomAddress(randomAddr); HAL_Delay(200);
  gattInit();            HAL_Delay(200);
  gapInit();             HAL_Delay(200);
  addGattService();      HAL_Delay(200);
  addTxCharacteristic(); HAL_Delay(200);
  addRxCharacteristic(); HAL_Delay(200);
  setAdvConfig();        HAL_Delay(200);
  setAdvData();          HAL_Delay(200);
  setAdvEnable();

  logMsg("\r\n================================\r\n");
  logMsg("  Ready.\r\n");
  logMsg("  A001: temp+humidity every 2s\r\n");
  logMsg("  A002: ON / OFF / TOGGLE\r\n");
  logMsg("================================\r\n\r\n");

  uint32_t lastSensor = 0;

  while(1) {
    uint8_t buf[64];
    uint8_t len = waitResponse(
                    buf, sizeof(buf), 10);

    if(len > 0) {

      // LE Connection Complete
      if(buf[0]==0x04 && buf[1]==0x3E &&
         len>4 && buf[3]==0x01 &&
         buf[4]==0x00) {
        connection_handle = buf[5] |
                            (buf[6] << 8);
        phone_subscribed = 0;
        logMsg(">>> CONNECTED!\r\n");
      }

      // LE Enhanced Connection Complete
      else if(buf[0]==0x04 && buf[1]==0x3E &&
              len>4 && buf[3]==0x0A &&
              buf[4]==0x00) {
        connection_handle = buf[5] |
                            (buf[6] << 8);
        phone_subscribed = 0;
        logMsg(">>> CONNECTED (enhanced)!\r\n");
      }

      // Disconnection Complete
      else if(buf[0]==0x04 && buf[1]==0x05) {
        logMsg(">>> DISCONNECTED\r\n");
        connection_handle = 0xFFFF;
        phone_subscribed = 0;
        HAL_Delay(500);
        setAdvEnable();
      }

      // Vendor events
      else if(buf[0]==0x04 && buf[1]==0xFF) {
        handleVendorEvent(buf, len);
      }
      else if(buf[0]==0x82 && buf[1]==0xFF) {
        handleVendorEvent(buf, len);
      }

      else {
        logHex("Event", buf, len);
      }
    }

    // Send sensor reading every 2 seconds
    // only when phone is subscribed
    if(phone_subscribed &&
       HAL_GetTick() - lastSensor > 2000) {
      lastSensor = HAL_GetTick();

      int16_t t = HTU21D_ReadTemperature();
      int16_t h = HTU21D_ReadHumidity();

      if(t != -999 && h != -999) {
        int16_t whole = t / 10;
        int16_t frac  = t % 10;
        if(frac < 0) frac = -frac;
        char msg[30];
        int msgLen = snprintf(msg, sizeof(msg),
          "T:%d.%dC H:%d%%", whole, frac, h);
        logMsg(msg);
        logMsg("\r\n");
        sendNotification((uint8_t*)msg, msgLen);
      } else {
        logMsg("Sensor read error\r\n");
      }
    }
  }
}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10805D88;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 921600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
