# RP2040 Prototype Board
![rp2040 image](RP2040.jpg)
This is a prototype board for the Raspberry RP2040.  
Main features (if compared to the Raspberry Pi Pico module) is that it has a linear voltage regulator for the 3.3VDC power supply to the RP2040. There is also a 3.0VDC voltage reference connected to VCC_ADC and a FLASH memory. 
The USB connector is USB-C type and the board also incorporates both a BOOLSEL and RESET button. There is also an I2C EEPROM connected to GPIO18 (SCL_1) and GPIO19 (SDA_1).

### Features
 - RP2040 (56 pin QFN)  
 - FLASH 128Mbit (Winbond W25Q128JV)
 - EEPROM 32kbit (Microchip 24AA32A)
 - USB-C connector
 - BOOTSEL and RESET buttons
 - Powered via external input or USB-C
 - 3.3V Linear voltage regulator
 - 3.0V Voltage Reference
 - 28 GPIOs (4 can be ADC inputs)

### Dimensions
Height: 60 mm  
Width: 40 mm  
Depth: 10 mm (pinheaders excluded)  

### Building Notes
The SMT components are listed in the bom.csv file with LCSC part number.  
Here is a list of suggested TH components:
| Pos | Description | Mfg | Partnumber | 
| --- | ----------- | --- | ---------- |
| C20 | 10uF / 50V Low Profile | Rubycon | 50ML10MEFC5X7
| RESET | 6x6mm Tact Switch | TE CONNECTIVITY | 1825910-6 |
| BOOTSEL | 6x6mm Tact Switch | TE CONNECTIVITY | 1825910-6 |
| J2+J3| 40 pin 2.54mm pin-header | HARWIN | M20-9774046 |

### YouTube videos
[Raspberry RP2040 Prototype board](https://youtu.be/EUp3dIfy8HQ)
