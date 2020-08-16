#include "defines.h"
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <EEPROM.h>

int8_t _vccstate;
int8_t _i2caddr;

uint8_t serialFlag = 0;

char ourByte = 0;

uint8_t winY = 0;
uint8_t cursorY = 0;
uint8_t cursorX = 0;

uint8_t rowTest = 8;

//uint8_t tileMap[32][8];					//Set character screen memory. Always 8 characters high, but up to 32 wide if in "hi res" mode
uint8_t menuMap[8][32];					//The memory map for menus. Separate from terminal tile map so they don't overwrite each other
uint8_t fileName[13];

uint8_t cursorStatus = 1;					//If cursor is on or not
uint8_t columns = 32;						//How wide our display is set at
uint8_t rows = 8;							//Total rows in memory. Window size is 32/16x8
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

uint16_t memPointers[] = {0x0000, 0x7FFF};
uint16_t pointerJump = 2048;				//How much the pointer moves when you change it
uint8_t menuLeftRight = 1;					//By default this is enabled

uint8_t dPadBounceEnable = 0xFF;			//At the start all buttons require re-trigger
uint8_t dPadBounce = 0;						//The debounce clear bit for each button	

uint8_t rebootFlag = 0;						//Set this so system always starts in reboot mode

uint8_t displayMode = 1;						//0 = terminal
uint8_t whichMenu = 0x81;						//Which menu we are in if in menu mode (such as SD load, RAM dump, setting etc)

uint8_t controls;

uint8_t animation = 0;

uint8_t charSetOffset = 32;					//ASCII ofset memory pointer. 0 for 8x8 font, 32 for 4x8 font

#define showTerminal 0
#define showMenu 	 1

uint8_t menuX = 0;					//Menu cursors
uint8_t menuY = 0;
uint8_t menuYmax;					//The maximum far down you can move the menuY cursor when selecting a file
uint8_t firstFile = 0;				//The first file we should display (if we scroll past 6 entires)
uint8_t filesInFolder = 0;			//The total # of files in directory. We display files "firstFile to firstFile + 5"			

uint8_t BASICtype = 0;				//Flag if we're having the MCU type in BASIC commands such as RUN (to start a game without keyboard for instance)

uint8_t textSize = 0;				//0 = small 1 = large

void setup() {

	Serial.begin(115200);					//Start USB serial
	Serial1.begin(115200);					//Start hardware UART
	UCSR1B &= ~(1 << 3);  				//Disable transmit on hardware UART (RX only, for example external serial keyboard)

	Wire.begin();							// I2C init
	
	OLEDbegin(SSD1306_SWITCHCAPVCC, 0x3C);	//OLED init
	
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

	OCR4A = 4;						//Setup high speed fast clock to drive Z80
	OCR4C = 6;
	PLLFRQ = B01111010;
	TCCR4B = B00000001;
	TCCR4A = B10000010;

	dataHiZ();

	addressRelease();

	pinMode(atmelSelect , INPUT);			//Atmel chip select

	//PUT IN NO SD WARNING

	attachInterrupt(digitalPinToInterrupt(atmelSelect), access, FALLING); 		//Setup interrupt vector for when Z80 sends bytes to MCU

	OLEDclear();
	OLEDsetXY(0, 0);						//64 pixels to right, 1 row down	

	if (!SD.begin(4)) {
		OLEDtext(F("INSERT SD CARD AND REBOOT"));
		while(1) {}
	}
	else {
		OLEDtext(F("SD BOOT OK"));
		delay(500);
	}
	
	OLEDsetXY(0, 2);						//64 pixels to right, 1 row down
	OLEDtext(F("HELLO WORLD!"));
	
	OLEDsetXY(0, 7);						//64 pixels to right, 1 row down
	OLEDtext(F("HELLO WORLD!"));

	uint8_t offset = 0;

	while(1) {

		OLEDrow0(offset);
	
		offset += 8;
		
		if (offset == 64) {
			offset = 0;
		}
		
		delay(500);
		
		
	}


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

	controls = getButtons();

	if (buttonMenu()) {				//Swap between terminal and menu
		if (displayMode == showTerminal) {
			displayModeChangeTo(showMenu);
		}
		else {
			displayModeChangeTo(showTerminal);
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

		if (menuLeftRight) {
			if (dLeft()) {
				
				if (whichMenu == 0) {
					whichMenu = 3 | 0x80;
				}
				else {
					whichMenu = (whichMenu - 1) | 0x80;
				}
		
			}

			if (dRight()) {

				if (whichMenu == 3) {
					whichMenu = 0x80;
				}
				else {
					whichMenu = (whichMenu + 1) | 0x80;
				}

			}				
		}

		displayMenu(8);
		
	}
	else {
		if (cursorStatus) {
			if (++blink == 18) {
				menuMap[cursorY][cursorX] = 95;
			}
			if (blink == 36) {
				blink = 0;
				menuMap[cursorY][cursorX] = 0;
			}	
		}
		
		display4x8(8);	
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

void displayModeChangeTo(uint8_t whatMode) {

	displayMode = whatMode;

	switch(whatMode) {
	
		case showTerminal:
			clearTerminal();		
			charSetOffset = 32;			
		break;
		
		case showMenu:		
			whichMenu |= 0x80;			//OR in the bit to flag to redraw the menu screen
			charSetOffset = 0;		
		break;	
		
	}
	
}

void setupMenu() {				//Basic background for all menus
	
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
		menuTitle(F("BASIC@HELP"));
		
		menuY = 2;
		menuMap[menuY][0] = '>';	
		
		whichMenu &= 0x7F;				//AND off the MSB

		menuText(F("W + RUN CR"), &menuMap[2][1]);
		menuText(F("RUN CR"), &menuMap[3][1]);	
		menuText(F("BREAK"), &menuMap[4][1]);	
		menuText(F("LIST"), &menuMap[5][1]);	
		menuText(F("CR"), &menuMap[6][1]);	
	
	}

	standardMenuCursor();
	
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
			case 6:
				fileName[1] = 13;		//CR
				fileName[2] = 0;			
			break;			
		}
		
		BASICtype = 1;							//Flag to start typing in bytes
		displayModeChangeTo(showTerminal);	//Switch to BASIC immediately no animation
		
	}		

}

void settingsMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle(F("@SETTINGS@"));
		
		menuY = 2;
		menuMap[menuY][0] = '>';	
		
		whichMenu &= 0x7F;				//AND off the MSB
		
		menuText(F("LOAD DEFAULTS"), &menuMap[2][1]);	
		menuText(F("LOAD START"), &menuMap[3][1]);
		menuText(F("PNTR JUMP"), &menuMap[7][1]);
	}
	
	drawHex(memPointers[0], &menuMap[3][15]);
	drawHex(pointerJump, &menuMap[7][15]);

	standardMenuCursor();	

	if (menuY > 2) {
		menuLeftRight = 0;
		
		if (dLeft()) {
			
			switch(menuY) {

				case 3:				
					memPointers[0] -= pointerJump;				
				break;
				
				case 4:
				
				if (rowTest > 0) {
					rowTest -= 8;
				}
				
				ssd1306_command(SSD1306_SETDISPLAYOFFSET);              // 0xD3
				ssd1306_command(rowTest);                                   // no offset
				
				break;
			
				case 7:
					if (pointerJump > 256) {
						pointerJump /= 2;
					}				
				break;
			
				
			}
			

		}
		
		if (dRight()) {
			
			switch(menuY) {
				
				case 3:				
					memPointers[0] += pointerJump;				
				break;	

				if (rowTest < 64) {
					rowTest += 8;
				}
				
				ssd1306_command(SSD1306_SETDISPLAYOFFSET);              // 0xD3
				ssd1306_command(rowTest);                                   // no offset
				
				break;
				
				case 7:
					if (pointerJump < 0x8000) {
						pointerJump *= 2;
					}		
				break;
			
				
			}			
			
		
		}
		
	}
	else {
		menuLeftRight = 1;
	}

}

void RAMDUMPMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle(F("RAM_DUMPER"));
		
		menuY = 2;
		menuMap[menuY][0] = '>';	

		whichMenu &= 0x7F;				//AND off the MSB

		for (int x = 2 ; x < 6 ; x++) {
			menuText(F("RAMSLOT1.BIN"), &menuMap[x][1]);
			menuMap[x][8] = x + 47;		
		}
		
		menuText(F("START:"), &menuMap[6][5]);
		menuText(F("  END:"), &menuMap[7][5]);
	}
	
	drawHex(memPointers[0], &menuMap[6][15]);
	drawHex(memPointers[1], &menuMap[7][15]);

	standardMenuCursor();

	if (menuY > 5) {
		menuLeftRight = 0;
		

		if (dLeft()) {
			memPointers[menuY - 6] -= pointerJump;
		}
		
		if (dRight()) {
			memPointers[menuY - 6] += pointerJump;			
		}
	}
	else {
		menuLeftRight = 1;
	}
	
	if (buttonA()) {
		for (int x = 0 ; x < 12 ; x++) {
			fileName[x] = menuMap[menuY][1 + x];
		}
		RAMdump();					//Dump selected RAM range to indicated file		
	}		
	
}

void SDLoadMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu

		setupMenu();					//Basic menu text
		//---------xxxxxxxxxx----
		menuTitle(F("@SD@@LOAD@"));
		
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

void standardMenuCursor() {

	if (dDown() and menuY < 7) {
		menuMap[menuY][0] = 0;
		menuMap[++menuY][0] = '>';					
	}
	
	if (dUp() and menuY > 2) {
		menuMap[menuY][0] = 0;
		menuMap[--menuY][0] = '>';					
	}
	
}

void menuTitle(const __FlashStringHelper *ifsh) {

	PGM_P p = reinterpret_cast<PGM_P>(ifsh);			//Get pointer

	uint8_t countPos = 2;

	while (1) {
		
		unsigned char theLetter = pgm_read_byte(p++);			//Get byte from memory
		
		if (countPos == 12 or theLetter == 0) {
			break;
		}
		
		theLetter -= 64;

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
				menuTextFile(entry.name(), &menuMap[drawRow++][2]);
			}
			else {
				menuTextFile(entry.name(), &menuMap[drawRow++][1]);
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

	memset(&menuMap[2][0], 0, 192);		//Clear everything under header (0 makes a space as does 32)

}

void streamLoad() {

	rebootFlag = 0;
	
	requestBus();
	
	TCCR4A = 0;				//Fast clock OFF;
	
	File whichFile = SD.open(fileName);

	menuClear();
	
	menuText(F("LOADING"), &menuMap[5][4]);

	displayMenu(8);

	memoryControl(1);			//Take control of memory
	dataOut();	//Assert bus
	uint16_t errors = 0;
	addressControl();
	
	uint16_t memPointer = memPointers[0];	//Starting address
	
	while(whichFile.available()) {

		uint8_t toWrite = whichFile.read();

		if ((memPointer & 0x00FF) == 0) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #
			Serial.print("Loading page ");
			Serial.println(memPointer >> 8);
		}

		addressAssertHalf(0x20, memPointer++ & 0xFF);			//Set low byte #		

		delayMicroseconds(5);

		uint8_t byteChecked = 1;
		
		while(byteChecked) {
			
			dataOut();	//Assert bus
			
			digitalWrite(z80WR, 0);		//Strobe the write			
			byteOut(toWrite);										//Assert data		
			digitalWrite(z80WR, 1);
			
			dataHiZ();

			digitalWrite(z80RD, 0);		//Check if byte is correct
			delayMicroseconds(1);
			uint8_t toCheck = byteIn();		
			digitalWrite(z80RD, 1);	
			
			if (toCheck != toWrite) {
				Serial.print("Byte ");
				Serial.print(memPointer & 0xFF);
				Serial.println(" error, re-load to RAM...");
				dataOut();	//Assert bus
				delay(1);
			}			
			else {
				byteChecked = 0;
			}
		
		}
	
	}

	whichFile.close();

	if (errors) {
		menuText(F("ERRORS!"), &menuMap[5][4]);
		displayMenu(8);
		whichMenu |= 0x80;
		delay(1000);
	}
	else {
		displayModeChangeTo(showTerminal);
		whichMenu = 0;						//First menu that will open is BASIC help	
		TCCR4A = B10000010;					//Fast clock ON;		
		startZ80();	
	}

}

void RAMdump() {

	requestBus();
	
	TCCR4A = 0;				//Fast clock OFF;
	
	if (SD.exists(fileName)) {			//If file exists erase it and start over
		SD.remove(fileName);
	}

	File whichFile = SD.open(fileName, FILE_WRITE);

	menuClear();
	menuText(F("DUMPING"), &menuMap[5][4]);
	displayMenu(8);
	
	//Serial.print(F("Dumping RAM to SD"));
	
	memoryControl(1);			//Take control of memory
	dataHiZ();	//Assert bus
	addressControl();
	
	uint16_t memPointer = memPointers[0];	//Starting address
	digitalWrite(z80RD, 0);		//Do read

	while(1) {

		if (!(memPointer & 0x00FF)) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #				
			Serial.print("Dumping page ");
			Serial.println(memPointer >> 8);
		}
		
		addressAssertHalf(0x20, memPointer & 0xFF);			//Set low byte #		
		delayMicroseconds(1);

		uint8_t toCheck = byteIn();			

		whichFile.write(toCheck);	//Write to file
		
		if (memPointer++ == memPointers[1]) {				//Did we do the last byte? Done!
			break;
		}
	
	}
	
	digitalWrite(z80RD, 1);

	whichFile.close();
	
	//Serial.println(F("done"));
	
	TCCR4A = B10000010;				//Fast clock ON;
	
	memoryControl(0);		//Release memory controls
	dataHiZ();				//Make sure data bus is released
	addressRelease();		//Release address bus
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go	

	whichMenu |= 0x80;		//Trigger a menu redraw
	
}
	
void startZ80() {

	Serial.println(F("Resetting Z80"));

	memoryControl(0);		//Release memory controls
	dataHiZ();				//Make sure data bus is released
	addressRelease();		//Release address bus
	pinMode(z80Reset, OUTPUT);	//Reset
	delay(1);
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go
	pinMode(z80Reset, INPUT);	//Release reset

}

void requestBus() {
	
	Serial.println(F("Requesting Z80 bus"));
	
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

		if (displayMode == showTerminal) {
			charPrint(RX);			//Chars to OLED and also control command interpreter	
		}
		Serial.write(RX); 		//Send data along as USB serial

	}
	
}

void charPrint(uint8_t theChar) {

	//If not opcode or payload then it's just normal text data. Print in on the OLED

	menuMap[cursorY][cursorX] = theChar - charSetOffset;
	
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
		
		memset(&menuMap[winY][0], 0, 32);		//Erase line

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
			clearTerminal();
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

void clearTerminal() {

	memset(&menuMap[0][0], 0, 256);
	cursorX = 0;
	cursorY = 0;
	winY = 0;	

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

void drawHex(uint16_t theValue, uint8_t *menuMemPoint) {

	for (int x = 0 ; x < 4 ; x++) {
	
		uint8_t theDigit = theValue & 0x0F;

		if (theDigit < 10) {
			*menuMemPoint-- = theDigit + 16;
		}
		else {
			*menuMemPoint-- = theDigit + 23;
		}

		theValue >>= 4;			//Shift one nibble
	
	}

	
}

void menuText(const __FlashStringHelper *ifsh, uint8_t *menuMemPoint) {

  PGM_P p = reinterpret_cast<PGM_P>(ifsh);			//Get pointer

  while (1) {
    unsigned char c = pgm_read_byte(p++);			//Get byte from memory
    if (c == 0) {
		break;
	}
	else {
		*menuMemPoint++ = c-32;
	}
  }

}

void menuTextFile(const char *str, uint8_t *menuMemPoint) {

  while (*str) {
	*menuMemPoint++ = *str++;  
  }

}

void displayMenu(uint8_t showRows) {

	if (menuLeftRight) {
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
	}
	else {
			menuMap[0][1] = 128;
			menuMap[1][1] = 144;	
			menuMap[0][12] = 128;
			menuMap[1][12] = 144;			
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

		thisTile = menuMap[courseY][courseX++];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	
 
		thisTile = menuMap[courseY][courseX++];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	

		thisTile = menuMap[courseY][courseX++];		//Get the tile value

		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (thisTile << 2) + col));             
		}	

		thisTile = menuMap[courseY][courseX++];		//Get the tile value

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

void OLEDsetXY(uint8_t x, uint8_t y) {
	
	Wire.beginTransmission(_i2caddr);
	Wire.write(0x00);
	Wire.write(0xB0 | y);
	Wire.write(0x00 | x & 0x0F);				//Low nibble X pos
	Wire.write(0x10 | x >> 4);					//High nibble X pos	
	Wire.endTransmission();

}

void OLEDchar(uint8_t theChar) {

	Wire.beginTransmission(_i2caddr);
	Wire.write(0x40);

	if (textSize) {
		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font8x8 + (theChar << 3) + col));             
		}		
	}
	else {
		for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font4x8 + (theChar << 2) + col));             
		}		
	}

	Wire.endTransmission();

}

void OLEDtext(const __FlashStringHelper *ifsh) {

  PGM_P p = reinterpret_cast<PGM_P>(ifsh);			//Get pointer to FLASH test
	
  while (1) {
    unsigned char c = pgm_read_byte(p++);			//Get byte from memory
    if (c == 0) {
		break;
	}
	else {
		OLEDchar(c-32);								//Send char directly to OLED memory
	}
  }	
	
}

void OLEDrow0(uint8_t whichRow) {

	Wire.beginTransmission(_i2caddr);
	Wire.write(0x00);
	Wire.write(SSD1306_SETDISPLAYOFFSET);
	Wire.write(whichRow);
	Wire.endTransmission();	
	
}

void OLEDclearRow(uint8_t whichRow) {

	OLEDsetXY(0, whichRow);

	for (int x = 0 ; x < 4 ; x++) {
		
		Wire.beginTransmission(_i2caddr);					//Send chars in groups of 2 (16 bytes per transmission)
		Wire.write(0x40);	
		
		for (uint8_t col = 0 ; col < 32 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(0);             
		}
		
		Wire.endTransmission();
	
	}

}

void OLEDclear() {

	OLEDsetXY(0, 0);	
	
	for (int x = 0 ; x < 34 ; x++) {
			
		Wire.beginTransmission(_i2caddr);					//Send chars in groups of 2 (16 bytes per transmission)
		Wire.write(0x40);

		for (uint8_t col = 0 ; col < 32 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(0);             
		}
		
		Wire.endTransmission();

	}

}

void ssd1306_command(uint8_t c) {

	Wire.beginTransmission(_i2caddr);
    Wire.write(0x00);
    Wire.write(c);
    Wire.endTransmission();

}

void eepromLOAD() {
	
	
}

void eepromSAVE() {
	
}
