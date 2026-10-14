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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rylr689_driver.h"
#include "ds3231_driver.h"
#include "stm32_commons.h"
#include "sdCard_driver.h"
#include "ff.h"
#include "oled_sh1106_driver.h"
#include "dht22_driver.h"
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LAST_UPDATE_HEADER_LINE		0
#define DATE_HEADER_LINE 			1
#define TIME_HEADER_LINE 			2
#define TEMPERATURE_HEADER_LINE 	4
#define HUMIDITY_HEADER_LINE 		5


#define RTC_BASE_CENTURY			2000

#define TIME_FORMAT_WIDTH 8 /* HH:MM:SS -> 10 characters. */
#define DATE_FORMAT_WIDTH 10 /* MM:DD:YYYY -> 10 characters. */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE BEGIN PV */

/* Extern in diskio.h */
sdCard_instance_t sdCardDev = {
		  .csPin = {
				  .port =SD_CARD_CS_GPIO_Port,
				  .pin = SD_CARD_CS_Pin
		  },
		  .pinRead = wrapper_pin_read,
		  .pinWrite = wrapper_pin_write,
		  .delay = wrapper_delay,
		  .spiHandle = &hspi1,
		  .spiTransmitReceive = wrapper_spi_transmit_receive,
		  .spiTransmit = wrapper_spi_transmit,
		  .getState = wrapper_get_spi_state,
		  .setBaudrate = wrapper_sdCard_spi_setBaudrate,
		  .initialized = 0
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */

static void rylry689_radio_init(rylr689_instance_t *dev, uint8_t *buffer);
static void sh1106_oled_init(oled_sh1106_instance_t *dev);
static void ds3231_rtc_init(ds3231_instance_t *dev);
static void sdCard_global_init(void);
static void syncIntRTCPeriphToExtRTC(ds3231_instance_t *dev);

/* For FatFS RTC capability. */
static void getDateTime(RTC_TimeTypeDef *curTime, RTC_DateTypeDef *curDate);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

typedef enum {
	STATE_APP_START = 0,
	STATE_SLEEP_ON_WAIT,
	STATE_ON_RECEIVE
} app_state_t;

volatile app_state_t appState = STATE_APP_START;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */


	/* Oled module START. */
	oled_sh1106_instance_t oled1;
	sh1106_oled_init(&oled1);
	/* Oled module END. */

	/* RTC module START. */
	ds3231_instance_t rtc1;
	ds3231_rtc_init(&rtc1);

	/* Update MCU RTC peripheral to avoid constantly querying ds3231 module. */
	syncIntRTCPeriphToExtRTC(&rtc1);

	RTC_TimeTypeDef curTime = {0};
	RTC_DateTypeDef curDate = {0};
	getDateTime(&curTime, &curDate);
	/* RTC module END. */

	/* SD card module START. */
	sdCard_global_init(); /* Initialize SD card. */

  	FATFS fs;   /* File system. */
  	FIL file;   /* File handler. */
  	FRESULT res; /* Result from file function calls. */
  	UINT br; /* Bytes read from file. */
  	UINT bw; /* Bytes written to file. */


  	/* Original files will have only the date stamp. If this file exist but the header
  	 * did not match, create a new file with time stamp.
  	 *
  	 * The limitation is that when the original fails. Then each boot up for the current
  	 * date will create new files with new time stamp. To avoid this, one possible solution
  	 * is to create a backup file for failing files, then reuse the original filename. This way
  	 * file creation can be reduced. The down-side is that creating a backup may take time if
  	 * original file is large.
  	 *
  	 * */
  	char filename[30];
  	char header[] = "Date: [mm-dd-yyyy],Time: (24-hour format): [hh-mm-ss],Temperature: (Celsius),Humidity: (%)\r\n";
  	uint32_t headerLen = strlen(header);

  	res = f_mount(&fs, "",0);

  	if(res != FR_OK) {
  		char *errMsg = "SD card error! Failed to mount file system!";
  		sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE, 0);
		sh1106_write_line(&oled1, errMsg, strlen(errMsg));
		Error_Handler();
  	}

  	snprintf(filename, 29, "sensor_log_%04u-%02u-%02u.csv", \
  			(curDate.Year + RTC_BASE_CENTURY), curDate.Month, curDate.Date);
  	uint32_t startTime = HAL_GetTick();

  	/* Open file for current date. */
  	while((res = f_open(&file, filename, FA_WRITE | FA_READ | FA_OPEN_APPEND)) != FR_OK) {
  		if((HAL_GetTick() - startTime) > SD_TIMEOUT) {
  			char *errMsg = "SD card error! Failed to open file!";
			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE, 0);
			sh1106_write_line(&oled1, errMsg, strlen(errMsg));
  			Error_Handler();
  		}
  	}

  	/* Check if file is empty. If not check if header exist. */
  	if(f_size(&file) > 0) {
  		char testRead[headerLen];
  		/* Set cursor to beginning. Then perform a read. */
  		f_lseek(&file, 0);
  		res = f_read(&file, testRead, headerLen, &br);

  		/* 1st Method of handling failing files. */
  		/* No matching header. Create new file for same date but with timestamp. */
  		if(memcmp((char*)testRead, header, headerLen) != 0) {
  			FIL tempFile;
  			char tempFilename[FF_MAX_LFN];
  			uint8_t readBuff[512];
  			UINT bytesRead;
  			UINT bytesWritten;
  			FSIZE_t bytesCopied = 0;
  			FSIZE_t bytesRemaining = f_size(&file); /* Though f_size() returns uint64_t, it may be unrealistic
  																	  for a CSV file to be that large. So, only the lower 32-bit
  																	  will be used for now. */
  			char msgDspBuff[100] = "Error with log file! Performing backup...";
  			char *bytesRemHeader = "Bytes remaining: ";
  			char *bytesCopHeader = "Bytes copied: ";

  			uint8_t oldBytesRemValBuffLen;
			uint8_t oldBytesCopValBuffLen;

			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE, 0);
			sh1106_write_line(&oled1, msgDspBuff, strlen(msgDspBuff));

			/* Line #2. */
			sprintf(msgDspBuff, "File size: %lu", (uint32_t)bytesRemaining);
			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 2, 0);
			sh1106_write_line(&oled1, msgDspBuff, strlen(msgDspBuff));

			/* Line #4. */
			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 3, 0);
			sh1106_write_line(&oled1, bytesRemHeader, strlen(bytesRemHeader));
			sprintf(msgDspBuff, "%lu", (uint32_t)bytesRemaining);
			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 4, 0);
			sh1106_write_line(&oled1, msgDspBuff, strlen(msgDspBuff));
			oldBytesRemValBuffLen = strlen(msgDspBuff);

			/* Line #5. */
			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 5, 0);
			sh1106_write_line(&oled1, bytesCopHeader, strlen(bytesCopHeader));
			sprintf(msgDspBuff, "%lu", (uint32_t)bytesCopied);
			sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 6, 0);
			sh1106_write_line(&oled1, msgDspBuff, strlen(msgDspBuff));
			oldBytesCopValBuffLen = strlen(msgDspBuff);

			getDateTime(&curTime, &curDate);

  			/* Filename format: mail_log_YYYY-MM-DD_hh-mm-ss.csv. */
  			snprintf(tempFilename, FF_MAX_LFN, "bak_failing_sensorlog_%04u-%02u-%02u_%02u-%02u-%02u.csv", \
  					(curDate.Year + RTC_BASE_CENTURY), curDate.Month, curDate.Date, \
  					 curTime.Hours, curTime.Minutes, curTime.Seconds);

  			f_open(&tempFile, tempFilename, (FA_WRITE | FA_READ | FA_OPEN_APPEND));

  			/* Set cursor to beginning of original file. Then perform a read. */
  			f_lseek(&file, 0);


  			while(bytesRemaining > bytesCopied) {
  				f_read(&file, readBuff, 512, &bytesRead);
  				f_write(&tempFile, readBuff, bytesRead, &bytesWritten);
  				f_sync(&tempFile);
  				bytesCopied += bytesWritten;

  				/* Display bytes remaining to be copied. */
				sprintf(msgDspBuff, "%lu", (uint32_t)(bytesRemaining - bytesCopied));


				if(oldBytesRemValBuffLen > strlen(msgDspBuff)) {
				  memset((msgDspBuff + strlen(msgDspBuff)), ' ', oldBytesRemValBuffLen - strlen(msgDspBuff));
				}
				sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 4, 0);
				sh1106_write_line(&oled1, msgDspBuff, strlen(msgDspBuff));

				/* Display bytes copied. */
				sprintf(msgDspBuff, "%lu", (uint32_t)bytesCopied);
				if(oldBytesCopValBuffLen > strlen(msgDspBuff)) {
				  memset((msgDspBuff + strlen(msgDspBuff)), ' ', oldBytesCopValBuffLen - strlen(msgDspBuff));
				}
				sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE + 6, 0);
				sh1106_write_line(&oled1, msgDspBuff, strlen(msgDspBuff));
  			}

  			sh1106_clearScreen(&oled1);
  			f_close(&tempFile);

//  			/* For debug START. */
//  			f_close(&file);
//  			HAL_GPIO_WritePin(BUILT_IN_LED_GPIO_Port, BUILT_IN_LED_Pin, GPIO_PIN_RESET);
//  			Error_Handler();
//  			/* For debug END. */

  			/* Set cursor to beginning of original file. Then delete its contents, keeping file open. */
  			f_lseek(&file, 0);
  			f_truncate(&file);
  			f_write(&file, header, headerLen, &bw);
  			f_sync(&file);
  		}
  		else {
  			/* Move back the cursor to EOF. To prepare for possible file write operations. */
  			f_lseek(&file, f_size(&file));
  		}
  	}
  	else {
  		res = f_write(&file, header, headerLen, &bw);
  		f_sync(&file);
  	}

  //	/* For debug. */
  //	f_close(&file);
  //	HAL_GPIO_WritePin(YELLOW_LED_GPIO_Port, YELLOW_LED_Pin, GPIO_PIN_SET);
  //	HAL_GPIO_WritePin(BLUE_LED_GPIO_Port, BLUE_LED_Pin, GPIO_PIN_SET);
  	/* SD card module END. */

  	HAL_GPIO_WritePin(BUILT_IN_LED_GPIO_Port, BUILT_IN_LED_Pin, GPIO_PIN_RESET);

  	/* Radio module START. */
  	uint8_t status[256];
  	rylr689_instance_t radio1;
  	rylry689_radio_init(&radio1, status);
  	uint32_t packetSizeReceived;
  	/* Radio module END. */


  	char tempReadBuff[16] = "N/A";
  	char humReadBuff[16] = "N/A";

	/* +1 for null character. */
	char dateBuff[DATE_FORMAT_WIDTH + 1];
	char timeBuff[TIME_FORMAT_WIDTH + 1];

	char* lastUpdateHeader = "Last updated:";
	char *dateHeader = "Date: ";
	char *timeHeader = "Time: ";
	char *temperatureHeader = "Temperature: ";
	char *humidityHeader = "Humidity: ";

	/* Write headers to designated lines. */
	sh1106_setAtCharacter(&oled1, LAST_UPDATE_HEADER_LINE, 0);
	sh1106_write_line(&oled1, lastUpdateHeader, strlen(lastUpdateHeader));

	sh1106_setAtCharacter(&oled1, DATE_HEADER_LINE, 0);
	sh1106_write_line(&oled1, dateHeader, strlen(dateHeader));
	snprintf(dateBuff, DATE_FORMAT_WIDTH + 1,"%02u/%02u/%04u", curDate.Month, curDate.Date, (uint16_t)curDate.Year + RTC_BASE_CENTURY);
	sh1106_write_line(&oled1, dateBuff, strlen(dateBuff));

	sh1106_setAtCharacter(&oled1, TIME_HEADER_LINE, 0);
	sh1106_write_line(&oled1, timeHeader, strlen(timeHeader));
	snprintf(timeBuff, TIME_FORMAT_WIDTH + 1, "%02u:%02u:%02u", curTime.Hours, curTime.Minutes, curTime.Seconds);
	sh1106_write_line(&oled1, timeBuff, strlen(timeBuff));

	sh1106_setAtCharacter(&oled1, TEMPERATURE_HEADER_LINE, 0);
	sh1106_write_line(&oled1, temperatureHeader, strlen(temperatureHeader));
	sh1106_write_line(&oled1, tempReadBuff, strlen(tempReadBuff));

	sh1106_setAtCharacter(&oled1, HUMIDITY_HEADER_LINE, 0);
	sh1106_write_line(&oled1, humidityHeader, strlen(humidityHeader));
	sh1106_write_line(&oled1, humReadBuff, strlen(humReadBuff));

	RTC_DateTypeDef savedDate = curDate;
	uint8_t oldTempBuffLen = strlen(tempReadBuff);
	uint8_t oldHumBuffLen = strlen(humReadBuff);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  switch(appState) {
		  case STATE_APP_START:
			  rylr689_radio_receive_it(&radio1);
			  appState = STATE_SLEEP_ON_WAIT;
			  break;
		  case STATE_SLEEP_ON_WAIT:
			  /* Enter sleep....
			   * For now, do nothing.*/
			  break;
		  case STATE_ON_RECEIVE:
			  raw_dht22_data_t rawSensorData;
			  dht22_data_t sensorData;

			  /* Fetch packet. */
			  rylr689_radio_get_packet(&radio1, &packetSizeReceived);

			  if(packetSizeReceived != sizeof(raw_dht22_data_t)) {
				  break; /* Error in transmission. Log it later. Don't update display. */
			  }

			  memcpy((uint8_t*)&rawSensorData, status, sizeof(raw_dht22_data_t));

			  /* Process data received. */
			  processTempHumidSensorData(&rawSensorData, &sensorData);
			  snprintf(tempReadBuff, 50, "%d.%d \xB0""C", sensorData.tempInt, sensorData.tempDec);
			  snprintf(humReadBuff, 50, "%d.%d%%", sensorData.humidityInt, sensorData.humidityDec);

			  /* Update display for date and time. */
			  getDateTime(&curTime, &curDate);
			  /* Update date on screen only when it is different. */
			  if((curDate.Month != savedDate.Month) || (curDate.Date != savedDate.Date ) || (curDate.Year != savedDate.Year)) {
				  savedDate = curDate;
				  /* Go back to Date header line. */
				  sh1106_setAtCharacter(&oled1, DATE_HEADER_LINE, strlen(dateHeader));
				  snprintf(dateBuff, DATE_FORMAT_WIDTH + 1,"%02u/%02u/%04u", curDate.Month, curDate.Date, (uint16_t)curDate.Year + RTC_BASE_CENTURY);
				  sh1106_write_line(&oled1, dateBuff, DATE_FORMAT_WIDTH);
			  }

			  /* Go back to Time header line to update. */
			  sh1106_setAtCharacter(&oled1, TIME_HEADER_LINE, strlen(timeHeader));
			  snprintf(timeBuff, TIME_FORMAT_WIDTH + 1, "%02u:%02u:%02u", curTime.Hours, curTime.Minutes, curTime.Seconds);
			  sh1106_write_line(&oled1, timeBuff, TIME_FORMAT_WIDTH);

			  /* For debug. */
			  HAL_UART_Transmit(&huart2, status, packetSizeReceived, 100);

			  /* Update display for temperature and humidity readings. */
			  uint8_t newTempBuffLen = strlen(tempReadBuff);
			  uint8_t newHumBuffLen = strlen(humReadBuff);

			  /* Pad the buffers if necessary in case new reading has fewer characters.
			   * This is to erase the all characters from previous readings displayed in screen. */
			  if(oldTempBuffLen > newTempBuffLen) {
				  memset((tempReadBuff + newTempBuffLen), ' ', oldTempBuffLen - newTempBuffLen);
			  }
			  if(oldHumBuffLen > newHumBuffLen) {
				  memset((humReadBuff + newHumBuffLen), ' ', oldHumBuffLen - newHumBuffLen);
			  }

			  sh1106_setAtCharacter(&oled1, TEMPERATURE_HEADER_LINE, strlen(temperatureHeader));
			  sh1106_write_line(&oled1, tempReadBuff, strlen(tempReadBuff));

			  sh1106_setAtCharacter(&oled1, HUMIDITY_HEADER_LINE, strlen(humidityHeader));
			  sh1106_write_line(&oled1, humReadBuff, strlen(humReadBuff));

			  /* Update old readings character count. */
			  oldTempBuffLen = newTempBuffLen;
			  oldHumBuffLen = newHumBuffLen;

			  char tempBuff[50];

			  /* Re-use status buffer. */
			  sprintf(tempBuff, "%s,%s,%d.%d,%d.%d\r\n",
					  dateBuff, timeBuff, sensorData.tempInt, \
					  sensorData.tempDec, sensorData.humidityInt, \
					  sensorData.humidityDec);

			  /* Log data to file. */
			  f_write(&file, tempBuff, (UINT)(strlen(tempBuff)), &bw);
			  f_sync(&file);
			  /* Reset application state. */
			  appState = STATE_APP_START;
			  break;
		  default:
	  }


    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
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
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

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
  huart2.Init.Mode = UART_MODE_TX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BUILT_IN_LED_GPIO_Port, BUILT_IN_LED_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, SD_CARD_CS_Pin|OLED_CS_Pin|LoRa_Module_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LoRa_Module_RFSW_V1_PIN_POS_Pin|LoRa_Module_RFSW_V2_PIN_POS_Pin|BLUE_LED_Pin|YELLOW_LED_Pin
                          |OLED_DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, OLED_NRESET_Pin|LoRa_Module_NRESET_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : BUILT_IN_LED_Pin */
  GPIO_InitStruct.Pin = BUILT_IN_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUILT_IN_LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SD_CARD_CS_Pin OLED_CS_Pin LoRa_Module_CS_Pin */
  GPIO_InitStruct.Pin = SD_CARD_CS_Pin|OLED_CS_Pin|LoRa_Module_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LoRa_Module_RFSW_V1_PIN_POS_Pin LoRa_Module_RFSW_V2_PIN_POS_Pin BLUE_LED_Pin YELLOW_LED_Pin
                           OLED_DC_Pin OLED_NRESET_Pin LoRa_Module_NRESET_Pin */
  GPIO_InitStruct.Pin = LoRa_Module_RFSW_V1_PIN_POS_Pin|LoRa_Module_RFSW_V2_PIN_POS_Pin|BLUE_LED_Pin|YELLOW_LED_Pin
                          |OLED_DC_Pin|OLED_NRESET_Pin|LoRa_Module_NRESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : LoRa_Module_Busy_Pin */
  GPIO_InitStruct.Pin = LoRa_Module_Busy_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(LoRa_Module_Busy_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LoRa_Module_DIOQ_Pin */
  GPIO_InitStruct.Pin = LoRa_Module_DIOQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(LoRa_Module_DIOQ_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* For FATFS. */
DWORD get_fattime (void) {
	RTC_TimeTypeDef curTime;
	RTC_DateTypeDef curDate;
	getDateTime(&curTime, &curDate);

	return (((curDate.Year + RTC_BASE_CENTURY) - 1980) << 25) | \
			(curDate.Month << 21) | (curDate.Date << 16) | \
			(curTime.Hours << 11) | (curTime.Minutes << 5) | \
			(curTime.Seconds >> 1);
}



static void getDateTime(RTC_TimeTypeDef *curTime, RTC_DateTypeDef *curDate) {
	/* Must be called in sequence per HAL documentation.
	 * The RTC_FORMAT_BIN converts the native BCD format to binary
	 * representation of the value. This will give us the actual
	 * number for each field. */
	HAL_RTC_GetTime(&hrtc, curTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, curDate, RTC_FORMAT_BIN);
}

static void sdCard_global_init(void) {
	sdCardDev.csPin.port = SD_CARD_CS_GPIO_Port;
	sdCardDev.csPin.pin = SD_CARD_CS_Pin;
	sdCardDev.pinRead = wrapper_pin_read;
	sdCardDev.pinWrite = wrapper_pin_write;
	sdCardDev.delay = wrapper_delay;
	sdCardDev.spiHandle = &hspi1;
	sdCardDev.spiTransmitReceive = wrapper_spi_transmit_receive;
	sdCardDev.spiTransmit = wrapper_spi_transmit;
	sdCardDev.getState = wrapper_get_spi_state;
	sdCardDev.setBaudrate = wrapper_sdCard_spi_setBaudrate;
	sdCardDev.initialized = 0;

	xSd_Init(&sdCardDev);
}

static void rylry689_radio_init(rylr689_instance_t *dev, uint8_t *buffer) {
	dev->csPin.pin = LoRa_Module_CS_Pin;
	dev->csPin.port = LoRa_Module_CS_GPIO_Port;
	dev->busyPin.pin = LoRa_Module_Busy_Pin;
	dev->busyPin.port = LoRa_Module_Busy_GPIO_Port;
	dev->nresetPin.pin = LoRa_Module_NRESET_Pin;
	dev->nresetPin.port = LoRa_Module_NRESET_GPIO_Port;
	dev->itPin.pin = LoRa_Module_DIOQ_Pin;
	dev->itPin.port = LoRa_Module_DIOQ_GPIO_Port;
	dev->rfswV1Pin.pin = LoRa_Module_RFSW_V1_PIN_POS_Pin;
	dev->rfswV1Pin.port = LoRa_Module_RFSW_V1_PIN_POS_GPIO_Port;
	dev->rfswV2Pin.pin = LoRa_Module_RFSW_V2_PIN_POS_Pin;
	dev->rfswV2Pin.port = LoRa_Module_RFSW_V2_PIN_POS_GPIO_Port;
	dev->pinWrite = wrapper_pin_write;
	dev->pinRead = wrapper_pin_read;
	dev->spiHandle = &hspi1;
	dev->spiReceive = wrapper_spi_receive;
	dev->spiTransmit = wrapper_spi_transmit;
	dev->rxBuff = buffer;
	dev->rxBuffSize = RX_BUFF_SIZE;
	dev->txBuff = buffer;
	dev->txBuffSize = TX_BUFF_SIZE;
	dev->initialized = 0;

	rylr689_radio_init(dev);
}
static void sh1106_oled_init(oled_sh1106_instance_t *dev) {
	dev->cs.port = OLED_CS_GPIO_Port;
	dev->cs.pin = OLED_CS_Pin;
	dev->dc.port = OLED_DC_GPIO_Port;
	dev->dc.pin = OLED_DC_Pin;
	dev->nreset.port = OLED_NRESET_GPIO_Port;
	dev->nreset.pin = OLED_NRESET_Pin;
	dev->gpio_write = wrapper_pin_write;
	dev->delay_ms = wrapper_delay;
	dev->spiHandle = &hspi1;
	dev->spiTransmit = wrapper_spi_transmit;
	dev->initialized = 0;

	sh1106_init(dev);
}

static void ds3231_rtc_init(ds3231_instance_t *dev) {
	dev->i2cHandle = &hi2c1;
	dev->i2cTransmit = wrapper_i2c_transmit_repeat_start;
	dev->i2cReceive = wrapper_i2c_receive_repeat_start;
	dev->baseCentury = RTC_BASE_CENTURY;
	dev->initialized = 0;

	ds3231_rtc_module_init(dev);
}

static void syncIntRTCPeriphToExtRTC(ds3231_instance_t *dev) {

	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef sDate = {0};
	raw_date_t rDate;
	raw_time_t rTime;

	/* Already in BCD, when calling HAL, use the RTC_FORMAT_BCD option. */
	ds3231_getRawCurrentDate(dev, &rDate);
	ds3231_getRawCurrentTime(dev, &rTime);

	sTime.Hours = rTime.hours;
	sTime.Minutes = rTime.minutes;
	sTime.Seconds = rTime.seconds;
	sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	sTime.StoreOperation = RTC_STOREOPERATION_RESET;

	if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
	{
		Error_Handler();
	}

	sDate.WeekDay = rDate.dayOfWeek;
	sDate.Month = rDate.month;
	sDate.Date = rDate.date;
	sDate.Year = rDate.year;

	if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
	{
		Error_Handler();
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
//	UNUSED(GPIO_Pin);
	appState = STATE_ON_RECEIVE;
}
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
