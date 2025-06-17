#ifndef MAIN_PIN_MAP_H_
#define MAIN_PIN_MAP_H_


//debug LEDS
#define DEBUG_LED_1    GPIO_NUM_39
#define DEBUG_LED_2    GPIO_NUM_40


//Modem pin map
#define MODEM_RX        GPIO_NUM_35
#define MODEM_TX        GPIO_NUM_34
#define MODEM_PWR_KEY   GPIO_NUM_36
#define MODEM_RST       GPIO_NUM_37
#define RAIL_4V_EN      GPIO_NUM_38


//Serial pin map
#define UART1_TXD      GPIO_NUM_18
#define UART1_RXD      GPIO_NUM_17


//Display pin map
#define LCD_CS         GPIO_NUM_1
#define LCD_RST        GPIO_NUM_2
#define LCD_DC         GPIO_NUM_3
#define LCD_MOSI       GPIO_NUM_4 
#define LCD_SCK        GPIO_NUM_5
#define LCD_LED        GPIO_NUM_6  
#define LCD_MISO       GPIO_NUM_7




#endif /* MAIN_PIN_MAP_H_ */