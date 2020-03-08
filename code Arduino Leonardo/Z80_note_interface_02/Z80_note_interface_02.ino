#include "defines.h"
#include <SPI.h>
#include <Wire.h>
#include <SD.h>

int8_t _vccstate;
int8_t _i2caddr;

uint8_t serialFlag = 0;

char ourByte = 0;

uint8_t winY = 0;
uint8_t cursorX = 0;
uint8_t cursorY = 0;

uint8_t tileMap[32][8];					//Set character screen memory. Always 8 characters high, but up to 32 wide if in "hi res" mode
uint8_t menuMap[8][16];					//The memory map for menus. Separate from terminal tile map so they don't overwrite each other
uint8_t fileName[13];

uint8_t cursorStatus = 1;					//If cursor is on or not
uint8_t columns = 32;						//How wide our display is set at
uint8_t rows = 8;							//Total rows in memory. Window size is 32/16x8
uint8_t refreshOLED = 0;
uint8_t blink;

#define atmelSelect 	1						//The digital pin the Z80 selects the Atmel IO with
#define	z80Reset		7
#define z80WR			12
#define z80RD			5
#define z80BUSREQ		6
#define z80IRQ			A4

#define reboot			27

#define joyUp			0x01
#define joyDown			0x02
#define joyLeft			0x04
#define joyRight		0x08
#define joyA			0x10
#define joyB			0x20
#define joyMenu			0x40
#define joyStart		0x80

uint8_t dPadBounceEnable = 0xFF;			//At the start all buttons require re-trigger
uint8_t dPadBounce = 0;						//The debounce clear bit for each button	

uint8_t rebootFlag = 0;						//Set this so system always starts in reboot mode

uint8_t displayMode = 1;						//0 = terminal
uint8_t whichMenu = 1;							//Which menu we are in if in menu mode (such as SD load, RAM dump, setting etc)

uint8_t controls;

uint8_t animation = 0;

uint8_t charSetOffset = 32;					//ASCII ofset memory pointer. 0 for 8x8 font, 32 for 4x8 font

#define showTerminal 0
#define showMenu 	 1

uint8_t menuAnimate = 0;
uint8_t menuAnimateRowT = 8;
uint8_t menuAnimateRowM = 0;

uint8_t menuX = 0;					//Menu cursors
uint8_t menuY = 0;
uint8_t menuYmax;					//The maximum far down you can move the menuY cursor when selecting a file
uint8_t firstFile = 0;				//The first file we should display (if we scroll past 6 entires)
uint8_t filesInFolder = 0;			//The total # of files in directory. We display files "firstFile to firstFile + 5"			

uint8_t BASICtype = 0;				//Flag if we're having the MCU type in BASIC commands such as RUN (to start a game without keyboard for instance)

void setup() {

	Serial.begin(115200);					//Start USB serial
	Serial1.begin(115200);					//Start hardware UART
	UCSR1B &= ~(1 << 3);  				//Disable transmit on hardware UART (RX only, for example external serial keyboard)

	Wire.begin();							// I2C Init
	
	OLEDbegin(SSD1306_SWITCHCAPVCC, 0x3C);
	
	TWBR = 6;						//Change prescaler to increase I2C speed to max of 400KHz

	digitalWrite(z80IRQ, 0);		//Low to start (should see if we can make this a pulled-up input for external devices)
	pinMode(z80IRQ, INPUT);			//IRQ to Z80. Start this as an input so the pull-up resistor takes it to HIGH

	digitalWrite(z80Reset, 0);		//Reset z80 right away
	pinMode(z80Reset, OUTPUT);	
	delay(1);
	pinMode(z80Reset, INPUT);		//Release reset

	pinMode(z80WR, INPUT);			//Write
	pinMode(z80RD, INPUT);			//Read

	DDRE |= (1 << 2);				//Slow clock (HWB, not sure if it's attached to a Digital Pin)

	analogWrite(13, 128);			//bootstrap high speed clock

	OCR4A = 4;
	OCR4C = 6;
	PLLFRQ = B01111010;
	TCCR4B = B00000001;
	TCCR4A = B10000010;

	pinMode(11, INPUT);			//Upper nibble of data bus
	pinMode(10, INPUT);
	pinMode(9, INPUT);
	pinMode(8, INPUT);

	pinMode(A0, INPUT);			//Lower nibble of data bus
	pinMode(A1, INPUT);
	pinMode(A2, INPUT);
	pinMode(A3, INPUT);

	addressRelease();

	pinMode(atmelSelect , INPUT);			//Atmel chip select

	textln("Z80 NOTE by Ben Heck");
	display4x8(8);
	delay(500);
	
	if (!SD.begin(4)) {
		text("SD card boot FAIL");
	}
	else {
		text("SD card boot OK");	
	}
	
	display4x8(8);
	delay(500);

	attachInterrupt(digitalPinToInterrupt(atmelSelect), access, FALLING); 		//Setup interrupt vector for when Z80 sends bytes to MCU

	whichMenu = 0x80;				//Set menu 0, with MSB set as flag that menu needs to be drawn
	
	displayModeChangeTo(showMenu, 1);	
	
}

void loop() {

	if (rebootFlag) {
		switch(rebootFlag) {
			case 1:		
				//streamLoad();
			break;
			case 2:
				RAMdump();
			break;
			case 3:
				//bootUp();
			break;
		}
		rebootFlag = 0;
	}

	if (cursorStatus) {
		if (++blink == 18) {
			tileMap[cursorX][cursorY] = 95;
		}
		if (blink == 36) {
			blink = 0;
			tileMap[cursorX][cursorY] = 0;
		}	
	}

	display4x8(menuAnimateRowT);		
	displayMenu(menuAnimateRowM);

	switch(menuAnimate) {
	
		case 1:
			menuAnimateRowM++;
			if (--menuAnimateRowT == 0) {
				menuAnimate = 0;
			}
		break;
		
		case 2:
			menuAnimateRowM--;
			if (++menuAnimateRowT == 8) {
				menuAnimate = 0;
			}			
		break;
	
	}

	controls = getButtons();

	if (buttonMenu() and !menuAnimate) {
		if (displayMode == showTerminal) {
			displayModeChangeTo(showMenu, 1);
		}
		else {
			displayModeChangeTo(showTerminal, 1);
		}
	}

	if (displayMode == showMenu) {

		switch(whichMenu & 0x7F) {				//The top bit is set when changing menus and tells the system to redraw new menu
			
			case 0:
				BASICmenu();
			break;		
			case 1:
				SDLoadMenu();			
			break;
			case 2:
				RAMDUMPMenu();			
			break;			
			case 3:
				settingsMenu();			
			break;	
			
		}

		
	}

	if (dLeft() and whichMenu > 0) {
	
		whichMenu = (whichMenu - 1) | 0x80;
		
	}

	if (dRight() and whichMenu < 4) {
	
		whichMenu = (whichMenu + 1) | 0x80;

	}

	if (Serial1.available() and !serialFlag) {		//Hardware UART (such as a serial keyboard)

		serialFlag = 1;					//Won't re-trigger until Z80 handles this
		ourByte = Serial1.read();		//Get byte from the hardware UART buffer
		DDRF |= (1 << 1);   			//Turn this pin into an OUTPUT to pull IRQ low

	}

	if (Serial.available() and !serialFlag) {		//USB serial (from a host PC)

		serialFlag = 1;					//Won't re-trigger until Z80 handles this
		ourByte = Serial.read();		//Get byte from the USB buffer
		DDRF |= (1 << 1);   			//Turn this pin into an OUTPUT to pull IRQ low

	}
	
	if (BASICtype) {
	
		ourByte = fileName[BASICtype++];
		
		if (ourByte) {
			serialFlag = 1;					//Won't re-trigger until Z80 handles this
			DDRF |= (1 << 1);   			//Turn this pin into an OUTPUT to pull IRQ low			
		}
		else {
			BASICtype = 0;
		}
		
	}

}

void displayModeChangeTo(uint8_t whatMode, uint8_t doAnimate) {

	displayMode = whatMode;

	switch(whatMode) {
	
		case showTerminal:
			
			charSetOffset = 32;		
			if (doAnimate) {
				menuAnimate = 2;	
				menuAnimateRowT = 0;
				menuAnimateRowM = 8;				
			}
			else {
				menuAnimateRowT = 8;
				menuAnimateRowM = 0;				
			}
					
		break;
		
		case showMenu:		
			charSetOffset = 0;	
			if (doAnimate) {
				menuAnimate = 1;	
				menuAnimateRowT = 8;
				menuAnimateRowM = 0;					
			}
			else {
				menuAnimateRowT = 0;
				menuAnimateRowM = 8;					
			}			
		break;	
		
	}
	
}

void setupMenu() {
	
	menuMap[0][0] = 1;			//Draw left edge
	menuMap[1][0] = 5;

	menuMap[0][1] = 171;		//Draw default arrows
	menuMap[1][1] = 187;	
	menuMap[0][12] = 173;
	menuMap[1][12] = 189;	

	for (int x = 2 ; x < 5 ; x++) {		//Draw Z80 note logo
	
		menuMap[0][11 + x] = x;
		menuMap[1][11 + x] = x + 4;
	
	}
	
	menuClear();
	
	charSetOffset = 0;
	
}

void BASICmenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle("BASIC@HELP");
		
		menuY = 2;
		menuMap[menuY][0] = '>';	
		
		whichMenu &= 0x7F;				//AND off the MSB

		menuText("W + RUN CR", &menuMap[2][1]);
		menuText("RUN CR", &menuMap[3][1]);	
		menuText("BREAK", &menuMap[4][1]);	
		menuText("LIST", &menuMap[5][1]);			
	}

	if (dDown() and menuY < 7) {
		menuMap[menuY][0] = 0;
		menuMap[++menuY][0] = '>';					
	}
	
	if (dUp() and menuY > 2) {
		menuMap[menuY][0] = 0;
		menuMap[--menuY][0] = '>';					
	}
	
	if (buttonA()) {			//Execute a BASIC command
		switch(menuY) {
			case 2:
				BASICcommand("WRUN");
			break;
			case 3:
				BASICcommand("RUN");
			break;
			case 4:
				fileName[1] = 3;		//Break
				fileName[2] = 0;			
			break;
			case 5:
				BASICcommand("LIST");
			break;			
		}
		
		BASICtype = 1;							//Flag to start typing in bytes
		displayModeChangeTo(showTerminal, 0);	//Switch to BASIC immediately no animation
		
	}		

}

void settingsMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle("@SETTINGS@");
		
		menuY = 2;
		menuMap[menuY][0] = '>';	
		
		whichMenu &= 0x7F;				//AND off the MSB
	
	}

}

void RAMDUMPMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle("RAM_DUMPER");
		
		menuY = 2;
		menuMap[menuY][0] = '>';	
		
		whichMenu &= 0x7F;				//AND off the MSB
	
	}
	
}

void SDLoadMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu

		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle("@SD@@LOAD@");
		
		firstFile = 1;					//To start, the first file list should be the first one found on the card (no scroll)
		menuY = 2;						//Reset selection cursor			
		drawFiles();	
		whichMenu &= 0x7F;				//AND off the MSB
	
	}

	if (dDown()) {
		if (menuY == 7 and menuYmax > menuY) {		//Did we reach the botton and are there more entries? Scroll down
			firstFile++;
			drawFiles();
		}
		else {
			if (menuY < menuYmax) {
				menuMap[menuY][0] = 0;
				menuMap[++menuY][0] = '>';					
			}
		}
	}
	
	if (dUp()) {
		if (menuY == 2) {			//If at the top, check if we can scroll up		
			if (firstFile > 1) {
				firstFile--;
				drawFiles();
			}		
		}
		else {						//Else just move cursor up
			menuMap[menuY][0] = 0;
			menuMap[--menuY][0] = '>';		
		}
	}	
	
	if (buttonA()) {
		for (int x = 0 ; x < 12 ; x++) {
			fileName[x] = menuMap[menuY][1 + x];
		}
		streamLoad();	//Load the selected file on screen
	}	

}

void menuTitle(const char *str) {

	uint8_t countPos = 2;
	
	while (countPos < 12 and *str) {
		
		char theLetter = *str++ - 64;			//Zero offset the letters

		if (theLetter > 15) {
			theLetter += 16;
		}

		menuMap[0][countPos] =  theLetter + 128;
		menuMap[1][countPos++] = theLetter + 144;

	}

}

void drawFiles() {

	menuClear();							//Clear the text portion
	menuMap[menuY][0] = '>';				//Draw cursor
		
	uint8_t drawRow = 2;					//What row to draw filename on
	uint8_t filesFound = 0;
	
	menuYmax = 64;

	File root = SD.open("/");
	root.rewindDirectory();	

	while (true) {

		File entry =  root.openNextFile();
		
		if (! entry) {
			menuYmax = drawRow - 1;
			break;
		}
		
		if (drawRow == 8) {
			break;
		}
		
		filesFound++;
		
		if (filesFound >= firstFile) {
			if (entry.isDirectory()) {
				menuMap[drawRow][1] = 94;						//Draw folder icon
				menuText(entry.name(), &menuMap[drawRow++][2]);
			}
			else {
				menuText(entry.name(), &menuMap[drawRow++][1]);
			}					
		
		}
		
		entry.close();

	}	

	root.close();

}

void BASICcommand(const char *str) {

	BASICtype = 1;		//Use as local pointer

	memset(&fileName, 0, 13);		//Make sure filename is zero'd out so terminators work

	while(*str) {
		fileName[BASICtype++] = *str++;
	}
	
	fileName[BASICtype] = 13;		//Carriage return
	
	BASICtype = 1;		//Flag to type this "serially" into Z80 BASIC
	
}

uint8_t dUp() {

	if (controls & joyUp) {					//Pressed? See if bit is set

		if (dPadBounceEnable & joyUp) {		//Checking for retriggers?
			if (dPadBounce & joyUp) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyUp;			//Bit clear? Set it
			return 1;						//and return status		
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyUp;			//Button not pressed? Clear the bit, allowing a retrigger
		return 0;
	}

}

uint8_t dDown() {

	if (controls & joyDown) {		//Pressed? See if bit is set

		if (dPadBounceEnable & joyDown) {	//Checking for retriggers?
			if (dPadBounce & joyDown) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyDown;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyDown;			//Button not pressed? Clear the bit, allowing a retrigger
		return 0;
	}

}

uint8_t dLeft() {

	if (controls & joyLeft) {		//Pressed? See if bit is set

		if (dPadBounceEnable & joyLeft) {	//Checking for retriggers?
			if (dPadBounce & joyLeft) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyLeft;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyLeft;			//Button not pressed? Clear the bit, allowing a retrigger
	}

}

uint8_t dRight() {

	if (controls & joyRight) {		//Pressed? See if bit is set

		if (dPadBounceEnable & joyRight) {	//Checking for retriggers?
			if (dPadBounce & joyRight) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyRight;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}

	}
	else {
		dPadBounce &= ~joyRight;			//Button not pressed? Clear the bit, allowing a retrigger
	}

}

uint8_t buttonA() {

	if (controls & joyA) {		//Pressed? See if bit is set

		if (dPadBounceEnable & joyA) {	//Checking for retriggers?
			if (dPadBounce & joyA) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyA;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyA;			//Button not pressed? Clear the bit, allowing a retrigger
	}

}

uint8_t buttonB() {

	if (controls & joyB) {		//Pressed? See if bit is set
	
		if (dPadBounceEnable & joyB) {	//Checking for retriggers?
			if (dPadBounce & joyB) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyB;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyB;			//Button not pressed? Clear the bit, allowing a retrigger
	}

}

uint8_t buttonStart() {

	if (controls & joyStart) {		//Pressed? See if bit is set
	
		if (dPadBounceEnable & joyStart) {	//Checking for retriggers?
			if (dPadBounce & joyStart) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyStart;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyStart;			//Button not pressed? Clear the bit, allowing a retrigger
	}

}

uint8_t buttonMenu() {

	if (controls & joyMenu) {		//Pressed? See if bit is set
	
		if (dPadBounceEnable & joyMenu) {	//Checking for retriggers?
			if (dPadBounce & joyMenu) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= joyMenu;			//Bit clear? Set it
			return 1;						//and return status
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~joyMenu;			//Button not pressed? Clear the bit, allowing a retrigger
	}

}

void menuClear() {

	memset(&menuMap[2][0], 0, 96);		//Clear everything under header (0 makes a space as does 32)

}

void streamLoad() {

	rebootFlag = 0;
	
	requestBus();
	
	TCCR4A = 0;				//Fast clock OFF;
	
	File whichFile = SD.open(fileName);

	menuClear();
	
	menuText("LOADING", &menuMap[5][4]);

	displayMenu(8);

	memoryControl(1);			//Take control of memory
	dataOut();	//Assert bus
	uint16_t errors = 0;
	addressControl();
	
	uint16_t memPointer = 0;
	
	while(whichFile.available()) {

		uint8_t toWrite = whichFile.read();
		
		dataOut();	//Assert bus

		if (!(memPointer & 0x00FF)) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #				
		}

		addressAssertHalf(0x20, memPointer++ & 0xFF);			//Set low byte #		
		byteOut(toWrite);	//Assert data			
		digitalWrite(z80WR, 0);		//Strobe the write	
		digitalWrite(z80WR, 1);	
		dataHiZ();		
		digitalWrite(z80RD, 0);		//Do read
		uint8_t toCheck = byteIn();		
		digitalWrite(z80RD, 1);	
		
		if (toCheck != toWrite) {
			errors++;
		}
		
	}

	whichFile.close();

	if (0) { //errors) {
		menuText("ERRORS!", &menuMap[5][4]);
		displayMenu(8);
		whichMenu |= 0x80;
		delay(1000);
	}
	else {
		 TCCR4A = B10000010;				//Fast clock ON;
		
		startZ80();	
		
		whichMenu |= 0x80;						//Flag to redraw menu next time it pops up
		
		displayModeChangeTo(showTerminal, 0);			
	}

}

void RAMdump() {

	requestBus();
	
	TCCR4A = 0;				//Fast clock OFF;
	
	if (SD.exists("RAMSLOT1.BIN")) {			//If file exists erase it and start over
		SD.remove("RAMSLOT1.BIN");
	}

	File whichFile = SD.open("RAMSLOT1.BIN", FILE_WRITE);
	
	Serial.print("Dumping RAM to SD");
	
	memoryControl(1);			//Take control of memory
	dataHiZ();	//Assert bus
	addressControl();
	
	uint32_t memPointer = 0;

	uint16_t progress = 1024;			//One "." every 1024 bytes
	uint16_t progressCounter = 0;
	
	while(memPointer != 65536) {

		if (progressCounter++ == progress) {
			progressCounter = 0;
			Serial.write('.');
		}

		if (!(memPointer & 0x00FF)) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #				
		}
		
		addressAssertHalf(0x20, memPointer & 0xFF);			//Set low byte #		
		digitalWrite(z80RD, 0);		//Do read
		uint8_t toCheck = byteIn();		
		digitalWrite(z80RD, 1);	
		
		whichFile.write(toCheck);	//Write to file
		
		memPointer++;
		
	}

	whichFile.close();
	
	Serial.println("done");
	
	TCCR4A = B10000010;				//Fast clock ON;
	
	memoryControl(0);		//Release memory controls
	dataHiZ();				//Make sure data bus is released
	addressRelease();		//Release address bus
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go	
	
}
	
void startZ80() {

	Serial.println("Resetting Z80");

	memoryControl(0);		//Release memory controls
	dataHiZ();				//Make sure data bus is released
	addressRelease();		//Release address bus

	pinMode(z80Reset, OUTPUT);	//Reset
	delay(1);
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go
	pinMode(z80Reset, INPUT);	//Release reset

}

void requestBus() {
	
	Serial.println("Requesting Z80 bus");
	
	digitalWrite(z80BUSREQ, 0);			//Request bus

	delay(5);
	
}

void memoryControl(uint8_t state) {

	if (state) {					//1 = Take control of memory

		digitalWrite(z80WR, 1);		
		pinMode(z80WR, OUTPUT);

		digitalWrite(z80RD, 1);			
		pinMode(z80RD, OUTPUT);

	}
	else {							//0 = release memory

		pinMode(z80WR, INPUT);
		pinMode(z80RD, INPUT);
	
	}
	
	
}

void access() {

	TCCR4A = 0;				//Fast clock OFF;

	if (PIND & 0x40) {	//High? Then it's a read (from us)
		//read();
		//dataOut();
		DDRB |= 0xF0;					//Set upper nibble as output
		DDRF |= 0xF0;					//Set lower nibble as output
		
		PORTB &= 0x0F;					//Clear top 4 bits of this port
		PORTB |= (ourByte & 0xF0);		//OR in top nibble of data

		PORTF &= 0x0F;					//Clear top 4 bits of this port
		PORTF |= (ourByte << 4);		//Shift data 4 to the left then OR in bottom nibble of data			

		PORTE |= 0x04;					//Wait state
		PORTE &= 0xFB;

		PORTE |= 0x04;					//T3 state
		PORTE &= 0xFB;

		//dataHiZ();
		DDRB &= 0x0F;			//Upper nibble of data bus
		DDRF &= 0x0F;			//Lower nibble of data bus		

		DDRF &= ~(1 << 1);		//Turn this pin back into an input so IRQ goes high to release it
		TCCR4A = B10000010;		//Turn fast clock back on
		serialFlag = 0;			//Clear flag for serial bytes
	
	}
	else {						//Else it's a write (to us)

		char RX = (PINB & 0xF0) | (PINF >> 4);		//Get the data the Z80 is writing to us

		PORTE |= 0x04;					//Wait state
		PORTE &= 0xFB;

		PORTE |= 0x04;					//T3 state
		PORTE &= 0xFB;

		TCCR4A = B10000010;			//Turn fast clock back on

		charPrint(RX);			//Chars to OLED and also control command interpreter
		Serial.write(RX); 		//Send data along as USB serial

	}
	
}

void charPrint(uint8_t theChar) {

	if (cursorStatus) {
		tileMap[cursorX][cursorY] = 0;	//Erase cursor current position
		blink = 14;			//Set this so cursor appears on next line immediately
	}

	/*

	if (opCode) {									//Collecting bytes of payload for the opcode?
		payLoad[payLoadPointer] = theChar;			//Copy the byte into buffer
		if (++payLoadPointer == payLoadSize[opCode]) {		//Did we collect 'em all? Execute payload
			execOpCode(opCode);								//Execute
			opCode = 0;							//Clear opcode
			return;								//Exit function	
		}
	}
	
	*/

	if (theChar < 31) {				//Did we get an opcode byte and not currently loading payload bytes?
		if (payLoadSize[theChar] == 0) {			//No payload bytes required?
			execOpCode(theChar);					//Execute immediately
			return;								//Exit function	
		}
		else {								//Payload bytes required?
			opCode = theChar;				//Set opCode loading flag			
			payLoadPointer = 0;				//Reset the buffer pointer
			return;							//Exit function					
		}
	
	}

	//If not opcode or payload then it's just normal text data. Print in on the OLED

	tileMap[cursorX][cursorY] = theChar - charSetOffset;
	
	if (++cursorX == columns) {
		cursorX = 0;
		advanceLine();
	}					

}

void advanceLine() {					//Advance the Y cursor and see if we need to scroll

	if (++cursorY > 7) {
		cursorY = 0;
	}
	
	if (cursorY == winY) {
		for (int x = 0 ; x < 32 ; x++) {
			tileMap[x][winY] = 0;
		}
		if (++winY == rows) {								//Did we reach the end of the buffer?
			winY = 0;										//Reset back to beginning
		}
	}	

}

void execOpCode(uint8_t whichOpCode) {		//When all payload bytes have been collected vector here to execute the opcode

	switch(whichOpCode) {

		case 1:				//Set terminal display width
			
		break;
	
		case 8:				//Backspace
			if (cursorX > 0) {
				cursorX--;
			}	
		case 10:			//New line (basically ignore, but eat the byte)

		break;
		case 12:			//Clear screen
			memset(&tileMap, 0, 256);
			cursorX = 0;
			cursorY = 0;
			winY = 0;
		break;
		case 13:			//Carriage return (use this to start new line, better terminal compatibility)
			cursorX = 0;
			advanceLine();
		break;
		case 7:				//CTRL-G = RAM dump to disk
			rebootFlag = 2;
		break;		
		case 18:			//CNTRL-R  = reboot
			rebootFlag = 1;
		break;

	}
	
}

void waitKey() {

  while(Serial.available() == 0) {
    delay(1);
  }
  
  while(Serial.available()) {
    Serial.read();
  }
  
}

void byteOut(uint8_t theData) {

	PORTB &= 0x0F;					//Clear top 4 bits of this port
	PORTB |= (theData & 0xF0);		//OR in top nibble of data

	PORTF &= 0x0F;					//Clear top 4 bits of this port
	PORTF |= (theData << 4);		//Shift data 4 to the left then OR in bottom nibble of data	

}

uint8_t byteIn() {

	return (PINB & 0xF0) | (PINF >> 4);

}

void dataHiZ() {			//Allows us to either read data or not affect the bus

	DDRB &= 0x0F;			//Upper nibble of data bus
	DDRF &= 0x0F;			//Lower nibble of data bus
	
}	

void dataOut() {			//Allows us to assert the data bus

	DDRB |= 0xF0;					//Set upper nibble as output
	DDRF |= 0xF0;					//Set lower nibble as output
		
}

void text(const char *str) {

  while (*str) {
    charPrint(*str++); 
  }

}

void menuText(const char *str, uint8_t *menuMemPoint) {

  while (*str) {
	*menuMemPoint++ = *str++;  
  }

}

void textln(const char *str) {

  while (*str) {
    charPrint(*str++); 
  }
  
  charPrint(13);			//Do carriage return

}

void displayMenu(uint8_t showRows) {

	if (animation++ == 7) {
		menuMap[0][1] = 171;
		menuMap[1][1] = 187;	
		menuMap[0][12] = 173;
		menuMap[1][12] = 189;			
	}
	
	if (animation == 14) {
		animation = 0;
		menuMap[0][1] = 172;
		menuMap[1][1] = 188;	
		menuMap[0][12] = 174;
		menuMap[1][12] = 190;	
	}

	uint16_t courseY = 0;                           		  //Get the course Y value for top line of visible screen

	uint8_t thisTile;

  for (uint8_t row = 0 ; row < showRows ; row++) {               //Draw the requested number of rows

	uint8_t courseX = 0;           						 //Find the current coarseX position for this line

    for (uint8_t colB = 0 ; colB < 8 ; colB++) { 		//Draw 8 groups of 2 characters across each line (16 char wide)

		Wire.beginTransmission(_i2caddr);					//Send chars in groups of 2 (16 bytes per transmission)
		Wire.write(0x40);

		thisTile = menuMap[courseY][courseX++];		//Get the tile value

		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font8x8 + (thisTile << 3) + col));             
		}	

		thisTile = menuMap[courseY][courseX++];		//Get the tile value

		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font8x8 + (thisTile << 3) + col));             
		}	
		
		Wire.endTransmission();

    }
	
    courseY++;                                            //Increment coarse pointer to next row
    
  }

}

void display4x8(uint8_t showRows) {

	uint16_t courseY = winY;                           		  //Get the course Y value for top line of visible screen

	uint8_t thisTile;

  for (uint8_t row = 0 ; row < showRows ; row++) {               //Draw the requested number of rows

	uint8_t courseX = 0;           						 //Find the current coarseX position for this line

    for (uint8_t colB = 0 ; colB < 8 ; colB++) {		

		Wire.beginTransmission(_i2caddr);					//Send chars in groups of 2 (16 bytes per transmission)
		Wire.write(0x40);

		thisTile = tileMap[courseX++][courseY];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	
 
		thisTile = tileMap[courseX++][courseY];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	

		thisTile = tileMap[courseX++][courseY];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	

		thisTile = tileMap[courseX++][courseY];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	

		Wire.endTransmission();
 
    }
	
	if (++courseY > 7) {
		courseY = 0;
	}
 
  }

}

void addressAssertHalf(uint8_t theDevice, uint8_t theAddress) {			//Puts an address on the IO expanders and controls the bus

	Wire.beginTransmission(theDevice);			//Low byte IO
	Wire.write(0x01);							//Select output register
	Wire.write(theAddress);						//Assert byte
	Wire.endTransmission();

}

uint8_t getButtons() {
	
	Wire.beginTransmission(0x22);				//Control IO
	Wire.write(0x00);							//Select read register
	Wire.endTransmission();
	
	Wire.requestFrom(0x22, 1);    				// request 1 byte from device 0x22

	return ~(Wire.read()); 						// receive a byte as character and return the inverse of it

}

void addressControl() {

	Wire.beginTransmission(0x20);				//Low byte IO
	Wire.write(0x03);							//Config register	
	Wire.write(0x00);							//Config as OUTPUT
	Wire.endTransmission();

	Wire.beginTransmission(0x21);				//Highbyte IO
	Wire.write(0x03);							//Config register	
	Wire.write(0x00);							//Config as OUTPUT
	Wire.endTransmission();
	
}

void addressRelease() {

	Wire.beginTransmission(0x20);				//Low byte IO
	Wire.write(0x03);							//Config register	
	Wire.write(0xFF);							//Config as INPUT
	Wire.endTransmission();

	Wire.beginTransmission(0x21);				//High byte IO
	Wire.write(0x03);							//Config register	
	Wire.write(0xFF);							//Config as INPUT
	Wire.endTransmission();

}	

void OLEDbegin(uint8_t vccstate, uint8_t i2caddr) {
	
	_vccstate = vccstate;
	_i2caddr = i2caddr;

	// Init sequence
	ssd1306_command(SSD1306_DISPLAYOFF);                    // 0xAE
	ssd1306_command(SSD1306_SETDISPLAYCLOCKDIV);            // 0xD5
	ssd1306_command(0x80);                                  // the suggested ratio 0x80

	ssd1306_command(SSD1306_SETMULTIPLEX);                  // 0xA8
	ssd1306_command(SSD1306_LCDHEIGHT - 1);

	ssd1306_command(SSD1306_SETDISPLAYOFFSET);              // 0xD3
	ssd1306_command(0x0);                                   // no offset
	ssd1306_command(SSD1306_SETSTARTLINE | 0x0);            // line #0
	ssd1306_command(SSD1306_CHARGEPUMP);                    // 0x8D
	if (vccstate == SSD1306_EXTERNALVCC)
	{ ssd1306_command(0x10); }
	else
	{ ssd1306_command(0x14); }
	ssd1306_command(SSD1306_MEMORYMODE);                    // 0x20
	ssd1306_command(0x00);                                  // 0x0 act like ks0108
	ssd1306_command(SSD1306_SEGREMAP | 0x1);
	ssd1306_command(SSD1306_COMSCANDEC);

	ssd1306_command(SSD1306_SETCOMPINS);                    // 0xDA
	ssd1306_command(0x12);
	ssd1306_command(SSD1306_SETCONTRAST);                   // 0x81
	if (vccstate == SSD1306_EXTERNALVCC)
	{ ssd1306_command(0x9F); }
	else
	{ ssd1306_command(0xCF); }


	ssd1306_command(SSD1306_SETPRECHARGE);                  // 0xd9
	if (vccstate == SSD1306_EXTERNALVCC)
	{ ssd1306_command(0x22); }
	else
	{ ssd1306_command(0xF1); }
	ssd1306_command(SSD1306_SETVCOMDETECT);                 // 0xDB
	ssd1306_command(0x40);
	ssd1306_command(SSD1306_DISPLAYALLON_RESUME);           // 0xA4
	ssd1306_command(SSD1306_NORMALDISPLAY);                 // 0xA6

	ssd1306_command(SSD1306_DEACTIVATE_SCROLL);

	ssd1306_command(SSD1306_DISPLAYON);//--turn on oled panel

	ssd1306_command(SSD1306_COLUMNADDR);
	ssd1306_command(0);   // Column start address (0 = reset)
	ssd1306_command(SSD1306_LCDWIDTH-1); // Column end address (127 = reset)

	ssd1306_command(SSD1306_PAGEADDR);
	ssd1306_command(0); // Page start address (0 = reset)

	ssd1306_command(7); // Page end address 


  
}

void ssd1306_command(uint8_t c) {

    Wire.beginTransmission(_i2caddr);
    Wire.write(0x00);
    Wire.write(c);
    Wire.endTransmission();

}
