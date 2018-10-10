/*
 * Originally i though I'd delve into C / C++ and learn all about how
 * not to use String and memory fragmentation and all that. Having no C
 * background and not being pertinent to my work, I chose the path of
 * least resistance (for now).
 *
 * Quite honestly, If this goes any further than a simply POC / hobby
 * project, it could be considered. For now, hail to the String!
 *
 * Trawling the Internet for ideas lead to this implementation of a
 * car tracking solution using the A7 GPS / GSM shield by elecrow
 * implementing the AT command set.
 *
 * To handle the GPS NEMEA sentences, we don't reinvent the wheel,
 * NeoGPS does a fantastic job already and is fast and small (since we'll be using
 * alot of Strings!) : https://github.com/SlashDevin/NeoGPS
 * 
 * The epoch returned is since 2000-01-01 00:00:00 and is 946684800 in 'ticks'
 * since the unix epoch which is 1970-01-01. this should be added to the value
 * returned
 *
 */

//Includes
#include <Arduino.h>
#include <NMEAGPS.h>

//Defines
#define A7 Serial1
#define debug true

//Setup a NeoGPS instance
static NMEAGPS gps;
static gps_fix fix;

/*
 * Parameters
 */
//Create states for our state machine
enum state_t {
	INIT,
	IDLE,
	WAIT_FOR_REG,
	ACTIVATE_GPS,
	GPS_READ_ENABLE,
	GPS_READING,
	GPS_PROCESSING,
	UPLOAD_GPS_DATA,
	STOP
};

//Set the initial state
state_t state = INIT;
//Placeholder for last stime something happened
unsigned long last_state_time;
//Delay so that the module has time to register on the network
const int STARTUP_DELAY = 30000;
const int GPS_READ_DELAY = 10000;

String readA7Serial(){
	String incoming_packed = "";
	char incoming;

	if (A7.available()){
		incoming = A7.read();
		incoming_packed += incoming;
	}
	return incoming_packed;
}

/*
 * Methods
 */
bool waitFor(String rsp1, String rsp2, unsigned long timeout){
	bool response_ok = false;
	String incoming_data = "";
	unsigned long wait_start;

	wait_start = millis();

	do{
		incoming_data += readA7Serial();

	} while
		((incoming_data.indexOf(rsp1) + incoming_data.indexOf(rsp2) == -2) && millis() - wait_start < timeout);

	if(incoming_data.indexOf(rsp1) + incoming_data.indexOf(rsp2) > -2){
		response_ok = true;
	}
	//echo
	if(debug){
		Serial.println(incoming_data);
	}

	return response_ok;
}
bool sendAndWaitResponse(String command, String rsp1, String rsp2, unsigned long timeout){
	/*
	 * The option is given to check for 2 responses in a given string. If only 1 is needed,
	 * set rsp2 = rsp1. Do not set as "" or you will get unexpected results
	 */
	bool response_ok = false;
	String incoming_data = "";
	unsigned long wait_start;

	//Clear the buffer
	while(A7.available()) A7.read();

	//Send the command
	A7.println(command);
	wait_start = millis();

	//Read the incoming data until either timeout or valid response is received.

	do{
		incoming_data += readA7Serial();

	} while
		((incoming_data.indexOf(rsp1) + incoming_data.indexOf(rsp2) == -2) && millis() - wait_start < timeout);

	if(incoming_data.indexOf(rsp1) + incoming_data.indexOf(rsp2) > -2){
		response_ok = true;
	}
	//echo
	if(debug){
		Serial.println(incoming_data);
	}

	return response_ok;
}

void sendHttp(){
	//First attach the MT to the Packet Domain service
	if(sendAndWaitResponse("AT+CGATT=1", "+CTZV", "OK",5000)){
		Serial.println( F("Connected to Packet Domain"));
		//set the context
		sendAndWaitResponse("AT+CGDCONT=1, \"IP\",\"internet\"", "OK","OK",10000);
		delay(3000);
		//assuming everything is ok untill now
		if(sendAndWaitResponse("AT+CGACT=1,1","OK","OK",2500)){
			Serial.println(F("Context activated"));
			//connect to the site
			if(sendAndWaitResponse("AT+CIPSTART=\"TCP\",\"altus.pythonanywhere.com\",80","CONNECT OK","OK",6000)){
				Serial.println(F("Connected to site... sending data"));
				if(sendAndWaitResponse("AT+CIPSEND",">","OK",6000)){
					A7.println("GET / HTTP/1.1");
					//A7.print("\r\n");
					A7.println("HOST: altus.pythonanywhere.com");
					//A7.print("\r\n");
					//A7.print("\r\n");
					A7.println("");
					A7.print("\x1a");
					
				}
				//sendAndWaitResponse("AT+CGACT=0","OK","OK",2500);
				//sendAndWaitResponse("AT+CGATT=0","OK","OK",2500);
			} else {
				Serial.println(F("Unable to connect"));
			}
		}


	}
}

void resetA7(bool hard_reset){
	/*
	 * Reseting the device also resets whatever parameters may
	 * have changed.
	 *
	 * Soft reset doesn't always work if the module is not
	 * responding. If after soft reset a response to AT is still
	 * not OK, then a hard reset is the only option.
	 */
	if(hard_reset){
			//Switch off the device
			digitalWrite(9, HIGH);
			delay(1000);
			digitalWrite(9, LOW);
			//wait a bit before powering on
			delay(2000);
			//Power Up
			digitalWrite(3, HIGH);
			delay(3000);
			digitalWrite(3, LOW);
		}
		else{
			//RST does not provide a OK but rather a CREG
			sendAndWaitResponse("AT+RST=1","+CREG: 1","",10000);
		}
	//set the state back to init
	state = INIT;
}

void echoA7(){
	//Simply echo's (and reads) what's in the buffer
	while(A7.available()){
		Serial.write(A7.read());
	}
}

void initializeA7Params(){
	/*
	 * Storing a user profile is pointless since the configuration table (which specifies
	 * what params can be stored) does not include what is required for our intentions.
	 * Initially thought that the SMS mode could be left as PDU but this would over complicate
	 * things and for our purpose, plain ASCII is good enough!
	 *
	 * ATE[<value>] Control the echo. set to 0
	 * AT+CPMS=?
	 * AT+CMGF=?
	 */

	//Disable echo only if not debugging.

	sendAndWaitResponse("AT+CMEE=2","OK","OK",2000);

	if(!debug){
		if(sendAndWaitResponse("ATE0","OK","OK", 2000)){
			Serial.println( F("Echo disabled") );
		}
	}
	if(sendAndWaitResponse("AT+CPMS=\"SM\",\"SM\",\"SM\"", "OK", "OK", 3000)){
		if(debug){
			Serial.println( F("SMS storage set to SM (SM, SM, SM)") );
		}
	}
	if(sendAndWaitResponse("AT+CMGF=1","OK","OK",2000)){
		if(debug){
			Serial.println(F ("SMS mode set to text (1)") );
		}
	}
}

void setup()
{
	Serial.begin(115200);
	A7.begin(115200);

	resetA7(true);
}

// The loop function is called in an endless loop
void loop()
{
	switch(state){
		case INIT:
			echoA7();
			if(millis() - last_state_time >= STARTUP_DELAY){
				Serial.println("Initializing...");
				initializeA7Params();
				state = WAIT_FOR_REG;
				last_state_time = millis();
			}
			break;
		case WAIT_FOR_REG:
			/*
			 * Wait for a registration sms before doing anything else
			 * Ideally this should be linked to the 
			 */
			if(true){
				//Some stuff happened and now we're here
				state = IDLE;
				last_state_time = millis();
			}
			break;
		case IDLE:
			/*
			 * Here we wait for time to pass so that a new itteration
			 * can begin. do some maintenance here while we wait
			*/
			if(millis() - last_state_time > GPS_READ_DELAY){
				state = GPS_READ_ENABLE;
				last_state_time = millis();
			}
			break;
		case GPS_READ_ENABLE:
			//enable GPS
			if(sendAndWaitResponse("AT+GPS=1", "OK", "OK", 3000)){
				//GPS is enabled
				if(sendAndWaitResponse("AT+GPSRD=1", "OK", "OK", 3000)){
					//GPS Sentences are being received
					Serial.print("\nGetting GPS fix.");
				}
				state = GPS_PROCESSING;
				last_state_time = millis();
			}else{
				//For some reason GPS did not activate
				state = IDLE;
			}
			break;
		case GPS_PROCESSING:
			//read the NMEA data and build the structures
			//echoA7();
			
			while(gps.available(A7)){
				fix = gps.read();

				if(fix.valid.location){
					// We have valid fix data! Stop reading sentences
					if(sendAndWaitResponse("AT+GPSRD=0","OK","OK", 3000)){
						Serial.println(F("\nGot GPS fix!"));

						state = UPLOAD_GPS_DATA;
						last_state_time = millis();
					}
					//Error handling?
				}
				//output status while we wait
				if(millis() - last_state_time >= 1000){
					Serial.print(".");
					last_state_time = millis();
				}
			}
			break;
		case UPLOAD_GPS_DATA:

			if(millis() - last_state_time > 5000){

				Serial.println(F("Location: "));
				Serial.print(fix.latitude(),6);
				Serial.print(F(", "));
				Serial.println(fix.longitude(),6);

				Serial.print(F("Altitude: "));
				Serial.println(fix.alt.whole);
				Serial.print(F("Fix DateTime since Epoch: "));
				Serial.println(fix.dateTime); //This the epoch timestamp in utc

				Serial.print(F("Satellites: "));
				Serial.println(fix.satellites);

				Serial.print(F("Speed: "));
				Serial.println(fix.speed_kph());
				last_state_time = millis();
				
				echoA7();
				state = IDLE;

				//Should GPS be switched off? if running of a small battery it should definately be
			}
			break;

		case STOP:
			echoA7();
			if(millis() - last_state_time > 10000){
				sendHttp();
				if(waitFor("200","Peanuts",10000)){
					Serial.println("Successfully sent data to server");
				}
				sendAndWaitResponse("AT+CIPCLOSE","OK","OK", 3000);
				sendAndWaitResponse("AT+CGACT=0","OK","OK",3000);
				state = WAIT_FOR_REG;		
			}
			
			
			break;
	}

	while(Serial.available()){
		sendAndWaitResponse(Serial.readString(),"","OK",5000);

		// if(sendAndWaitResponse(Serial.readString(), "OK", "OK", 5000)){
		// 	Serial.println("response is good");
		// } else{
		// 	Serial.println("OOOOOPS!");
		// }
	}
}