/* 

	DHT22 temperature sensor driver

*/

#ifndef DHT22_H_  
#define DHT22_H_

#define DHT_OK 0
#define DHT_CHECKSUM_ERROR -1
#define DHT_TIMEOUT_ERROR -2

#define RED_LED  GPIO_NUM_5
#define BLUE_LED GPIO_NUM_18
#define DHT_PIN  GPIO_NUM_4


// == function prototypes =======================================

void 	errorHandler(int response);
void   *setupDHT(int dht_gpio_pin);
int 	readDHT(void *dht_handle);
float 	getHumidity();
float 	getTemperature();

void led_start(int gpio_led, int channel, int timer, int freq_hz);
void led_stop(int channel);
void led_fade_stop(int channel);

#endif
