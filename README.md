# Temperature and Humidity Logger Project
Repository for my wireless-temperature-and-humidity logger project. Code for transmitter and receiver are placed under separate folder, namely "tx-" and "rx-". 

**<u>FYI</u>:** This project is currently ***In Progress***.
<hr>

## <u>Pins used (Transmitter STM32-blackpill)</u>:

### RYLR689 LoRa module:
- PA5: SCK
- PA6: MISO
- PA7: MOSI
- PA8: NSS (CS)
- PB0: RF CONTROL: RFSW_V1_PIN_POS
- PB1: RF CONTROL: RFSW_V2_PIN_POS
- PB5: BUSY
- PB8: RESET (Active-Low)
- PB9: DIO (Interrupt)

### DHT22 temperature-and-humidity sensor module:
- PA4: I/O pin

### For debug:
- PA2: UART TX
- PA3: UART RX
- PC13: User LED for debugging purposes.

<hr>

## <u>Pins used (Receiver STM32-blackpill)</u>:

### RYLR689 LoRa module:
- PA5: SCLK
- PA6: MISO
- PA7: MOSI
- PA8: NSS (CS)
- PB0: RF CONTROL: RFSW_V1_PIN_POS
- PB1: RF CONTROL: RFSW_V2_PIN_POS
- PB5: BUSY
- PB8: RESET (Active-Low)
- PB9: DIO (Interrupt)

### DS3231 (RTC module):
- PB6: SCL
- PB7: SDA

### SD-card module (SPI-based):
- PA1: CS
- PA5: SCLK
- PA6: MISO
- PA7: MOSI

### OLED Display (SPI-based):
- PA4: CS
- PA5: SCLK
- PA7: MOSI
- PB14: DC
- PB15: RESET (Active-Low)

## LED indication lights:
- PB12: Blue LED 
- PB13: Yellow LED
  
### For debug:
- PA2: UART TX
- PA3: UART RX
- PC13: User LED for debugging purposes.

<hr>

# Flow:
- **TBU**: I will update this with an image of a diagram, typing it is not easy.

<hr>

# Demo:

### RX side Image(s):

**<u>Components:<u>**
- STM32F411CEU (blackpill), RYLR689 LoRa module, SD card module, DS3231 RTC module, OLED SH1106 module, Power supply, 9v Battery.
![Components used in this project.](/demo/images/rx_device_images/rx-init-status-full.jpg)

**<u>Wirings:<u>**
- ***Note:*** The solder blobs you would see here were intentionally done for better contact.
![Components used in this project.](/hardware/rx-dev-wirings.jpg)

**<u>Initial Status:<u>**
![Components used in this project.](/demo/images/rx_device_images/rx-init-status-close-up.jpg)

**<u>Updated Status:<u>**
![Components used in this project.](/demo/images/rx_device_images/rx-updated-status-close-up.jpg)

### TX side Image(s):

**<u>Components:<u>**
- STM32F411CEU (blackpill), RYLR689 LoRa module, DHT22 temperature-and-humidity sensor.
![Components used in this project.](/demo/images/tx_device_images/tx-device.jpg)

**<u>Wirings:<u>**
- ***Note:*** The solder blobs you would see here were intentionally done for better contact.
![Components used in this project.](/hardware/tx-dev-wirings.jpg)

**<u>Side-view:<u>**
![Components used in this project.](/demo/images/tx_device_images/tx-close-up-side-view.jpg)

**<u>Powered-On Status:<u>**
![Components used in this project.](/demo/images/tx_device_images/tx-close-up-top-view-on.jpg)


### Video(s):
- **TBU**: I will post a simple demonstration video here.
