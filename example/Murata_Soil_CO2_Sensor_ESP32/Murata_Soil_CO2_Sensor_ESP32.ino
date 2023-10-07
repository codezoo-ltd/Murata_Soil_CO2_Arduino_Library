//VDD 3.0V ~ 5.0V
//EN  (0.8 x VDD) ~ VDD
//VINT 3.3V	 TXD, RXD Using I/O 3.3V (carefully)
//TXD : VINT - 0.45 ~
//RXD : 0.7 x VINT ~ VINT + 0.3

#include "TYPE1SC.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <U8x8lib.h>

#define DebugSerial Serial
#define M1Serial Serial2

#define PWR_PIN 5
#define RST_PIN 18
#define WAKEUP_PIN 19
#define EXT_ANT 4

#define CO2_ADC 35
#define SOIL_EN	25

#define REPORT_TIME 5000    /* 5 seconds */

HardwareSerial soilSensor(1); // use ESP32 UART1 (connected Soil Sensor)

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);
TYPE1SC TYPE1SC(M1Serial, DebugSerial, PWR_PIN, RST_PIN, WAKEUP_PIN);

float TEMP = 0.0;
float EC_BULK = 0.0;//It is a value suitable for measurement of ions in the water.
float VWC = 0.0;
float EC_PORE = 0.0;//It is a value suitable for measurement of ions in the soil.
float co2_ppm = 0.0;

uint16_t temperature = 0;
uint16_t ec_water = 0;
uint16_t moisture = 0;
uint16_t ec_soil = 0;

uint16_t CRC16(int size, uint8_t* data)
{
	uint16_t cr = 0xFFFF;

	for (int i = 0; i < size; i++) {
		cr = cr ^ data[i];

		for (int j = 0; j < 8; j++) {
			if ((cr & 0x0001) == 0x0001) {
				cr >>= 1;
				cr ^= 0xA001;
			}
			else {
				cr >>= 1;
			}
		}
	}

	return cr;
}

uint8_t SendMsg[8];
uint8_t RecvMsg[24];
uint8_t msg[] = {0x02, 0x07, 0x01, 0x01};
uint8_t msg2[] = {0x01, 0x08, 0x01};
uint8_t msg3[] = {0x01, 0x13, 0x10};
uint16_t crc = 0;
uint8_t crc16_upper = 0;
uint8_t crc16_lower = 0;
int readSize = 0;

/* OLED Display Initialization */
#define U8LOG_WIDTH 16
#define U8LOG_HEIGHT 8
uint8_t u8log_buffer[U8LOG_WIDTH * U8LOG_HEIGHT];
U8X8LOG u8x8log;

/* EXT_ANT_ON 0 : Use an internal antenna.
   EXT_ANT_ON 1 : Use an external antenna.
 */
#define EXT_ANT_ON 0

void extAntenna() {
	if (EXT_ANT_ON) {
		pinMode(EXT_ANT, OUTPUT);
		digitalWrite(EXT_ANT, HIGH);
		delay(100);
	}
}

void setup() {
	u8x8.begin();
	u8x8.setFont(u8x8_font_chroma48medium8_r);

	u8x8log.begin(u8x8, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer);
	u8x8log.setRedrawMode(
			1); // 0: Update screen with newline, 1: Update screen for every char

	u8x8log.print("OLED Display Init!!!\n");
	delay(1000);

	//soilSensor Enable
	pinMode(SOIL_EN, OUTPUT);
	digitalWrite(SOIL_EN, HIGH);
	delay(1000);

	u8x8log.print("Soil Sensor Init!!!\n");

	soilSensor.begin(9600, SERIAL_8N1, 33, 32); // RXD1 : 33, TXD1 : 32
	Serial.begin(115200);
	M1Serial.begin(115200);

	/* Board Reset */
	TYPE1SC.reset();
	delay(2000);

	/* TYPE1SC Module Initialization */
	if (TYPE1SC.init()) {
		DebugSerial.println("TYPE1SC Module Error!!!");
		u8x8log.print("TYPE1SC Module Error!!!\n");
	}

	/* Network Regsistraiton Check */
	while (TYPE1SC.canConnect() != 0) {
		DebugSerial.println("Network not Ready !!!");
		u8x8log.print("Network not Ready!!!\n");
		delay(2000);
	}

	DebugSerial.println("TYPE1SC Module Ready!!!");
	u8x8log.print("TYPE1SC Module Ready!!!\n");

	analogReadResolution(12); //ADC 12bit Setup

	DebugSerial.println("Warm Up Progress..");
	u8x8log.print("Warm Up Progress..\n");
	//180secs Warmup time, During warm-up, 
	//a voltage value of 3000ppm(5V) is output
	delay(180000);  
	DebugSerial.println("Now CO2 Sensing Start..");
	u8x8log.print("Now CO2 Sensing Start..\n");
}

void loop() {
	/* GET CO2 value */
	uint16_t value = analogRead(35);
	float adc_value = (((float)value) * 3300 * 2) / 4095 + 250;
	co2_ppm = adc_value/1000 * 600;

_CHECK:
	readSize = 0;

	DebugSerial.println("Setup ----1");
	crc = CRC16(sizeof(msg), msg);
	crc16_upper = (crc >> 8) & 0xFF;
	crc16_lower = crc & 0xFF;

	DebugSerial.print("CRC-16(Upper) 0x");
	DebugSerial.print(crc16_upper, HEX);
	DebugSerial.println("");
	DebugSerial.print("CRC-16(Lower) 0x");
	DebugSerial.print(crc16_lower, HEX);
	DebugSerial.println("");

	memset(SendMsg, 0x0, sizeof(SendMsg));
	memset(RecvMsg, 0x0, sizeof(RecvMsg));

	/* Send Packet */
	soilSensor.write(msg[0]);
	soilSensor.write(msg[1]);
	soilSensor.write(msg[2]);
	soilSensor.write(msg[3]);
	soilSensor.write(crc16_upper);
	soilSensor.write(crc16_lower);

	delay(500);

	/* Read Packet */
	while (soilSensor.available() > 0) {
		// get incoming byte:
		RecvMsg[readSize++] = soilSensor.read();
	}

	DebugSerial.println("Read Packet");
	for (int i = 0; i < readSize; i++)
	{
		DebugSerial.print(RecvMsg[i], HEX);
		DebugSerial.print(" ");
	}
	DebugSerial.println("=======================");

	DebugSerial.println("Setup ----2");

	do {
		memset(SendMsg, 0x0, sizeof(SendMsg));
		memset(RecvMsg, 0x0, sizeof(RecvMsg));

		crc = CRC16(sizeof(msg2), msg2);
		crc16_upper = (crc >> 8) & 0xFF;
		crc16_lower = crc & 0xFF;

		DebugSerial.print("CRC-16(Upper) 0x");
		DebugSerial.print(crc16_upper, HEX);
		DebugSerial.println("");
		DebugSerial.print("CRC-16(Lower) 0x");
		DebugSerial.print(crc16_lower, HEX);
		DebugSerial.println("");

		/* Send Packet */
		soilSensor.write(msg2[0]);
		soilSensor.write(msg2[1]);
		soilSensor.write(msg2[2]);
		soilSensor.write(crc16_upper);
		soilSensor.write(crc16_lower);

		delay(500);

		readSize = 0;

		/* Read Packet */
		while (soilSensor.available() > 0) {
			// get incoming byte:
			RecvMsg[readSize++] = soilSensor.read();
		}

		DebugSerial.println("Read Packet");
		for (int i = 0; i < readSize; i++)
		{
			DebugSerial.print(RecvMsg[i], HEX);
			DebugSerial.print(" ");
		}
		DebugSerial.println("=======================");
		delay(1000);
	} while (RecvMsg[3] == 0x0);

	DebugSerial.println("Read Sensor ----3");
	/* Get Soil Sensor Measurement Value */
	memset(SendMsg, 0x0, sizeof(SendMsg));
	memset(RecvMsg, 0x0, sizeof(RecvMsg));

	crc = CRC16(sizeof(msg3), msg3);
	crc16_upper = (crc >> 8) & 0xFF;
	crc16_lower = crc & 0xFF;

	DebugSerial.print("CRC-16(Upper) 0x");
	DebugSerial.print(crc16_upper, HEX);
	DebugSerial.println("");
	DebugSerial.print("CRC-16(Lower) 0x");
	DebugSerial.print(crc16_lower, HEX);
	DebugSerial.println("");

	/* Send Packet */
	soilSensor.write(msg3[0]);
	soilSensor.write(msg3[1]);
	soilSensor.write(msg3[2]);
	soilSensor.write(crc16_upper);
	soilSensor.write(crc16_lower);

	delay(500);

	readSize = 0;

	/* Read Packet */
	while (soilSensor.available() > 0) {
		// get incoming byte:
		RecvMsg[readSize++] = soilSensor.read();
	}

	DebugSerial.println("Read Packet");
	for (int i = 0; i < readSize; i++)
	{
		DebugSerial.print(RecvMsg[i], HEX);
		DebugSerial.print(" ");
	}
	DebugSerial.println("=======================");
	delay(1000);

	/* Calculate CRC16 */
	crc = CRC16(readSize - 2, RecvMsg);
	crc16_upper = (crc >> 8) & 0xFF;
	crc16_lower = crc & 0xFF;

	DebugSerial.print("CRC-16(Upper) 0x");
	DebugSerial.print(crc16_upper, HEX);
	DebugSerial.println("");
	DebugSerial.print("CRC-16(Lower) 0x");
	DebugSerial.print(crc16_lower, HEX);
	DebugSerial.println("");

	/* CRC16 Check */
	if (crc16_upper == RecvMsg[readSize - 2] &&
			crc16_lower == RecvMsg[readSize - 1]) {
		DebugSerial.println("CRC Check OK!!");
	} else {
		DebugSerial.println("CRC Check Error!!");
		goto _CHECK;
	}

	temperature = RecvMsg[3] | (RecvMsg[4] << 8);
	ec_water = RecvMsg[5] | (RecvMsg[6] << 8);
	moisture = RecvMsg[9] | (RecvMsg[10] << 8);
	ec_soil = RecvMsg[15] | (RecvMsg[16] << 8);

	DebugSerial.println(temperature, HEX);
	DebugSerial.println(ec_water, HEX);
	DebugSerial.println(moisture, HEX);
	DebugSerial.println(ec_soil, HEX);

	if (temperature < 0xFFF) {
		TEMP = temperature * 0.0625;
	} else {
		TEMP = (0x1000 - temperature) * (0.0625 * -1);
	}

	EC_BULK = ec_water * 0.001;
	VWC = moisture * 0.1;
	EC_PORE = ec_soil * 0.001;

	DebugSerial.print("TEMP: ");
	u8x8log.print("TEMP: ");
	DebugSerial.print(TEMP);
	u8x8log.print(TEMP);
	DebugSerial.println("C");
	u8x8log.print("C\n");
	DebugSerial.print("EC_BULK: ");
	u8x8log.print("EC_BULK: ");
	DebugSerial.print(EC_BULK);
	u8x8log.print(EC_BULK);
	DebugSerial.println("dS/m");
	u8x8log.print("dS/m\n");
	DebugSerial.print("VWC: ");
	u8x8log.print("VWC: ");
	DebugSerial.print(VWC);
	u8x8log.print(VWC);
	DebugSerial.println("%");
	u8x8log.print("%d\n");
	DebugSerial.print("EC_PORE: ");
	u8x8log.print("EC_PORE: ");
	DebugSerial.print(EC_PORE);
	u8x8log.print(EC_PORE);
	DebugSerial.println("dS/m");
	u8x8log.print("dS/m\n");

	DebugSerial.print("CO2: ");
	u8x8log.print("CO2: ");
	DebugSerial.print(co2_ppm);
	u8x8log.print(co2_ppm);
	DebugSerial.println("ppm");
	u8x8log.print("ppm\n");

	delay(REPORT_TIME);
}
