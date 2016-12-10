#include <stdio.h>
#include <inttypes.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <errno.h>
#include <curl/curl.h>
#include <string.h>
#include <sys/stat.h>

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
#define TSL2561_GAIN_16X                  (0x12) // (0x10)
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


//Delay getLux function
#define LUXDELAY 300

#define KP_NUM  10

typedef enum progState {
	STATE_WAIT = 0,
	STATE_FIND,
	STATE_CONTROL,
} progState;

progState state = STATE_WAIT;

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
	size_t retcode;
	curl_off_t nread;

	/* in real-world cases, this would probably get this data differently
	 as this fread() stuff is exactly what the library already would do
	 by default internally */ 
	retcode = fread(ptr, size, nmemb, stream);

	nread = (curl_off_t)retcode;

	fprintf(stderr, "*** We read %" CURL_FORMAT_CURL_OFF_T
		  " bytes from file\n", nread);

	return retcode;
}


int getLux(int fd){
   
   wiringPiI2CWriteReg8(fd, TSL2561_COMMAND_BIT, TSL2561_CONTROL_POWERON); //enable the device
   wiringPiI2CWriteReg8(fd, TSL2561_REGISTER_TIMING, TSL2561_GAIN_AUTO); //auto gain and timing = 101 mSec
   //Wait for the conversion to complete
   delay(LUXDELAY);
   //Reads visible + IR diode from the I2C device auto
   uint16_t visible_and_ir = wiringPiI2CReadReg16(fd, TSL2561_REGISTER_CHAN0_LOW);
   wiringPiI2CWriteReg8(fd, TSL2561_COMMAND_BIT, TSL2561_CONTROL_POWEROFF); //disable the device
   return visible_and_ir;
}

int main(){
	int lux;
	int fd = 0;
	CURL *curl;
	CURLcode res;
	char message[40];
	int setpoint = 45;
	int bri = 150;
	FILE * src;
	struct stat file_info;
	struct curl_slist *headers = NULL;

	curl_global_init(CURL_GLOBAL_ALL);

	headers = curl_slist_append(headers, "Accept: application/json");
	
	stat("temp", &file_info);

	fd = wiringPiI2CSetup(TSL2561_ADDR_FLOAT);
	
	if(1) {
		state = STATE_FIND;
	}
	
	while(1){
		
		switch(state) {
			case STATE_FIND :
			
				printf("Finding setpoint...\n");
				
				bri = 200;
				
				sprintf(message, "{\"on\":true, \"bri\":%d}", bri);
				
				/* get a curl handle */ 
				curl = curl_easy_init();
				if(curl) {
					curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
					curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.180/api/Nqk1wk-FCruCodDE4P-jxgepEri0c8uGj77A24G8/lights/1/state");  
					curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); /* !!! */
					
					//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

					curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message); /* data goes here */

					res = curl_easy_perform(curl);
					
					/* always cleanup */ 
					curl_easy_cleanup(curl);
					
				}
				
				delay(5000);
				
				lux = getLux(fd);
				printf("Lux: %d\n", lux);
				
				setpoint = lux;
				
				printf("Setpoint: %d\n", setpoint);
				
				state = STATE_CONTROL;
				
				break;
				
			
			case STATE_CONTROL :

				lux = getLux(fd);
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

				/* get a curl handle */ 
				curl = curl_easy_init();
				if(curl) {
					curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
					curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.180/api/Nqk1wk-FCruCodDE4P-jxgepEri0c8uGj77A24G8/lights/1/state");  
					curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); /* !!! */
					
					//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

					curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message); /* data goes here */

					res = curl_easy_perform(curl);
					
					/* always cleanup */ 
					curl_easy_cleanup(curl);
					
				}
				break;
		
		}

		usleep(500000);
	}
	
	curl_global_cleanup();
	return 0;
}
