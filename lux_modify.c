#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <errno.h>
#include <curl/curl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

// ALL COMMAND TSL2561
// Default I2C RPI address in (0x39) = FLOAT ADDR (Slave) Other [(0x49) = VCC ADDR / (0x29) = GROUND ADDR]
#define TSL2561_ADDR_LOW                   (0x29)
#define TSL2561_ADDR_FLOAT                 (0x39)    
#define TSL2561_ADDR_HIGH                   (0x49)
#define TSL2561_CONTROL_POWERON             (0x03)
#define TSL2561_CONTROL_POWEROFF          (0x00)
#define TSL2561_GAIN_0X                        (0x00)   //No gain
#define TSL2561_GAIN_AUTO                (0x01)
#define TSL2561_GAIN_1X                 (0x02)
#define TSL2561_GAIN_16X                  (0x10)
#define TSL2561_INTEGRATIONTIME_13MS          (0x00)   // 13.7ms
#define TSL2561_INTEGRATIONTIME_101MS          (0x01) // 101ms
#define TSL2561_INTEGRATIONTIME_402MS         (0x02) // 402ms
#define TSL2561_READBIT                   (0x01)
#define TSL2561_COMMAND_BIT                (0x80)   //Must be 1
#define TSL2561_CLEAR_BIT                (0x40)   //Clears any pending interrupt (write 1 to clear)
#define TSL2561_WORD_BIT                   (0x20)   // 1 = read/write word (rather than byte)
#define TSL2561_BLOCK_BIT                  (0x10)   // 1 = using block read/write
#define TSL2561_REGISTER_CONTROL           (0x00)
#define TSL2561_REGISTER_TIMING            (0x81)
#define TSL2561_REGISTER_THRESHHOLDL_LOW      (0x02)
#define TSL2561_REGISTER_THRESHHOLDL_HIGH     (0x03)
#define TSL2561_REGISTER_THRESHHOLDH_LOW      (0x04)
#define TSL2561_REGISTER_THRESHHOLDH_HIGH     (0x05)
#define TSL2561_REGISTER_INTERRUPT            (0x06)
#define TSL2561_REGISTER_CRC                  (0x08)
#define TSL2561_REGISTER_ID                   (0x0A)
#define TSL2561_REGISTER_CHAN0_LOW            (0x8C)
#define TSL2561_REGISTER_CHAN0_HIGH           (0x8D)
#define TSL2561_REGISTER_CHAN1_LOW            (0x8E)
#define TSL2561_REGISTER_CHAN1_HIGH           (0x8F)

#define LUXDELAY13   15
#define LUXDELAY101  110


#define KP_NUM  0.8

typedef enum progState {
	STATE_WAIT = 0,
	STATE_FIND,
	STATE_CONTROL,
	STATE_TIMING,
	STATE_BULB_TIMING,
} progState;

progState state = STATE_WAIT;
struct curl_slist *headers = NULL;

size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
	return size * nmemb;
}

void sendBrightnessMessage( char* message ) {
	CURL *curl;
	CURLcode res;
	
	/* get a curl handle */ 
	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
		curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.180/api/Nqk1wk-FCruCodDE4P-jxgepEri0c8uGj77A24G8/lights/1/state");  
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); /* !!! */
		
		//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message); /* data goes here */

		res = curl_easy_perform(curl);
		
		/* always cleanup */ 
		curl_easy_cleanup(curl);
		
	}
}


int getLux(int fd, int fastSample){
   
	wiringPiI2CWriteReg8(fd, TSL2561_COMMAND_BIT, TSL2561_CONTROL_POWERON); //enable the device
	if( state == STATE_TIMING || fastSample) {
		wiringPiI2CWriteReg8(fd, TSL2561_REGISTER_TIMING, TSL2561_INTEGRATIONTIME_13MS | 0x10); //16x and timing = 101 mSec
		//Wait for the conversion to complete
		delay(LUXDELAY13);
	}
	else {
		wiringPiI2CWriteReg8(fd, TSL2561_REGISTER_TIMING, TSL2561_INTEGRATIONTIME_101MS | 0x10); //16x and timing = 101 mSec
		//Wait for the conversion to complete
		delay(LUXDELAY101);
	}
	//Reads visible + IR diode from the I2C device auto
	uint16_t visible_and_ir = wiringPiI2CReadReg16(fd, TSL2561_REGISTER_CHAN0_LOW);
	wiringPiI2CWriteReg8(fd, TSL2561_COMMAND_BIT, TSL2561_CONTROL_POWEROFF); //disable the device
	return visible_and_ir;
}

int main(int argc, char* argv[]){
	int lux, newlux;
	int fd = 0;
	int c;
	char message[40];
	int setpoint = 45;
	int bri = 150;
	int pbri;
	FILE * src;
	struct stat file_info;
	struct timeval  tv1, tv2;
	int i = 0;
	int n = 0;
	int iCount;
	int forever = 1;
	
	state = STATE_CONTROL;
	
	while ((c = getopt (argc, argv, "tfbc:s:")) != -1) {
		switch (c) {
			case 'b' :
				state = STATE_BULB_TIMING;
				break;
				
			case 'c' :
				forever = 0;
				iCount = atoi(optarg);
				break;
			
			case 'f' :
				state = STATE_FIND;
				break;
			
			case 's' :
				setpoint = atoi(optarg);
				break;
				
			case 't' :
				state = STATE_TIMING;
				break;
				
			case '?' :
				return 1;
				
			default:
				break;
				
		}
	}
		

	curl_global_init(CURL_GLOBAL_ALL);
	
	headers = curl_slist_append(headers, "Accept: application/json");
	
	stat("temp", &file_info);

	fd = wiringPiI2CSetup(TSL2561_ADDR_FLOAT);
	
	gettimeofday(&tv1, NULL);
	srand((unsigned int) tv1.tv_usec);
		
	for( i = 0; i<iCount || forever; i++) {
		
		switch(state) {
			case STATE_FIND :
			
				printf("Finding setpoint...\n");
				
				bri = 200;
				
				sprintf(message, "{\"on\":true, \"bri\":%d}", bri);
				
				sendBrightnessMessage( message );
				
				// Delay for a second to settle
				delay(1000);
				
				lux = getLux(fd, 0);
				printf("Lux: %d\n", lux);
				
				setpoint = lux;
				
				printf("Setpoint: %d\n", setpoint);
				
				state = STATE_CONTROL;
				
				break;
				
			
			case STATE_CONTROL :

				lux = getLux(fd, 0);
				printf("Lux: %d\n", lux);

				bri += KP_NUM*(setpoint - lux);
				if(bri<0) {bri = 0;}
				if(bri>254) {bri = 254;}

				if( bri > 0 ) {
					sprintf(message, "{\"on\":true, \"bri\":%d}", bri);
				}
				else {
					sprintf(message, "{\"on\":false}", bri);
				}

				sendBrightnessMessage( message );
				break;
				
			case STATE_TIMING :				
				// Get current lux...
				lux = getLux(fd, 1);
				
				pbri = bri;
				while(abs(bri-pbri) < 45) bri = rand() % 254;;
				
				sprintf(message, "{\"on\":true, \"bri\":%d}", bri);
				
				sendBrightnessMessage( message );
				
				gettimeofday(&tv1, NULL);
				
				
				do {
					newlux = getLux(fd, 1);
				}
				while( newlux == lux);
				
				gettimeofday(&tv2, NULL);
				
				printf ("\nTotal time = %f useconds\n", (double) (tv2.tv_usec - tv1.tv_usec ) + (double) (tv2.tv_sec - tv1.tv_sec) * 1000000);
				
				usleep(2000000);
				
				break;
				
			case STATE_BULB_TIMING :
				// Get current lux...
				lux = getLux(fd, 1);
				
				pbri = bri;
				while(abs(bri-pbri) < 45) bri = rand() % 254;;
				
				sprintf(message, "{\"on\":true, \"bri\":%d}", bri);
				
				sendBrightnessMessage( message );
				
				do {
					newlux = getLux(fd, 1);
				}
				while( newlux == lux);
				
				gettimeofday(&tv1, NULL);
				
				lux = getLux(fd, 0);
				do {
					newlux = getLux(fd, 0);
					if( newlux == lux ) n++;
					else {
						lux = newlux;
						n = 0;
					}
					//printf("\nnl %d",newlux);
				}
				while( n < 5 );
				
				gettimeofday(&tv2, NULL);
				
				printf ("\nTotal time = %f useconds\n", (double) (tv2.tv_usec - tv1.tv_usec ) + (double) (tv2.tv_sec - tv1.tv_sec) * 1000000 - 4 * 110000);
				
				usleep(2000000);
				
				break;

		
		}

		usleep(500000);
	}
	
	curl_global_cleanup();
	return 0;
}
