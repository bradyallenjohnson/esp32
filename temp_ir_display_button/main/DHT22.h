/* 
 *
 * DHT22 temperature sensor driver
 *
 */

#ifndef DHT22_H_  
#define DHT22_H_

#define DHT_OK 0
#define DHT_CHECKSUM_ERROR -1
#define DHT_TIMEOUT_ERROR -2
#define DHT_PIN  GPIO_NUM_4

// == function prototypes =======================================

void   error_handler(int response);
void  *setup_DHT(int dht_gpio_pin);
int    read_DHT(void *dht_handle);
float  get_humidity(void *dhtc);
float  get_temperature(void *dhtc);

#endif
