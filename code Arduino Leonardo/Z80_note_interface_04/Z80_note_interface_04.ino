#include "defines.h"
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
//#include <EEPROM.h>

int8_t _vccstate;
int8_t _i2caddr;

uint8_t serialFlag = 0;

char ourByte = 0;

uint8_t winY = 0;
uint8_t cursorY = 0;
uint8_t cursorX = 0;

uint8_t xPos = 0;
uint8_t yPos = 0;

uint8_t cpuState = 1;					//CPU state (1 = run 0 = single step)

#define menuMax	5						//Max number of menu items

uint8_t rowTest = 8;

#define bufferSize		64				//Size of terminal buffer

uint8_t scBuffer[bufferSize];					//Buffer for terminal mode to print chars to OLED
uint8_t scBuffPointWrite = 0;			//Point the Z80 writes to
uint8_t scBuffPointRead = 0;			//Point the MCU reads from to draw to screen
uint8_t fileName[13];
uint32_t fileSize[6];

uint16_t progressBarTicks;					//How many bytes must be read for the progress bar to advance 1 pixel to the right
	
uint8_t cursorStatus = 1;					//If cursor is on or not
uint8_t columns = 32;						//How wide our display is set at
uint8_t rows = 8;							//Total rows in memory. Window size is 32/16x8
uint16_t blink;								//For to blink da cursor

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

uint16_t viewPointer = 0;
uint16_t memPointers[] = {0x0000, 0x1FFF};
uint16_t pointerJump = 2048;				//How much the pointer moves when you change it
uint8_t menuLeftRight = 1;					//By default this is enabled

uint8_t dPadBounceEnable = 0xFF;			//At the start all buttons require re-trigger
uint8_t dPadBounce = 0;						//The debounce clear bit for each button	

uint8_t rebootFlag = 0;						//Set this so system always starts in reboot mode

uint8_t displayMode = 1;						//0 = terminal
uint8_t whichMenu = 0x80;						//Which menu we are in if in menu mode (such as SD load, RAM dump, setting etc)

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
	digitalWrite(z80WR, 1);			//With pullups
	pinMode(z80RD, INPUT);			//Read
	digitalWrite(z80RD, 1);			//With pullups

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

	attachInterrupt(digitalPinToInterrupt(atmelSelect), access, FALLING); 		//Setup interrupt vector for when Z80 sends bytes to MCU

	OLEDclear();
	OLEDsetXY(0, 0);	

	if (!SD.begin(4)) {
		OLEDtext(F("INSERT SD CARD AND REBOOT"));
		while(1) {}
	}
	else {
		OLEDtext(F("SD BOOT OK"));
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

		delay(1);

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
			case 4:
				RAMviewerMenu();			
			break;	
			case 5:
				CPUcontrolMenu();
			break;				
		}

		if (menuLeftRight) {
			
			if (animation++ == 128) {
				OLEDsetXY(0, 0);
				OLEDchar(28);
				OLEDsetXY(12 << 3, 0);
				OLEDchar(30);				
			}
			
			if (animation == 255) {
				animation = 0;
				OLEDsetXY(0, 0);
				OLEDchar(59);
				OLEDsetXY(12 << 3, 0);
				OLEDchar(61);	
			}		
			
			if (dLeft()) {
				
				if (whichMenu == 0) {
					whichMenu = menuMax | 0x80;
				}
				else {
					whichMenu = (whichMenu - 1) | 0x80;
				}
		
			}

			if (dRight()) {

				if (whichMenu == menuMax) {
					whichMenu = 0x80;
				}
				else {
					whichMenu = (whichMenu + 1) | 0x80;
				}

			}				
		}
	
	}
	else {
		
		if (scBuffer[scBuffPointRead]) {				//Something in buffer?
		
			charPrint(scBuffer[scBuffPointRead]);		//Send it
			scBuffer[scBuffPointRead++] = 0;			//Clear it and advance
			
			if (scBuffPointRead == bufferSize) {		//Go around ring buffer
				scBuffPointRead = 0;
			}
		}
		else {											//Not printing characters? Blink cursor (if enabled)
			if (cursorStatus) {
				if (++blink == 4500) {
					OLEDchar(95);
					OLEDsetXY(cursorX << 2, cursorY);
				}
				if (blink == 9000) {
					OLEDchar(0);
					blink = 0;
					OLEDsetXY(cursorX << 2, cursorY);
				}			
			}		
		}
		
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

	OLEDclear();
	OLEDrow0(0);
	textSize = 1;

	OLEDsetXY(0, 2);
	OLEDchar('>' - 32);
	
	OLEDsetXY(0, 1);
	OLEDfillLine(13, 0x80);			//Draw a line across row 1 (up to logo)
	OLEDtext(F("'()"));				//Bottom of logo	
	
	OLEDsetXY(13 << 3,0);			//Top of logo
	OLEDtext(F("$%&"));

	OLEDsetXY(1 << 3, 0);			//Draw menu title here	
}

void BASICmenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtext(F("BASIC  HELP"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		OLEDsetXY(8, 2);
		OLEDtext(F("W + RUN"));
		OLEDsetXY(8, 3);
		OLEDtext(F("RUN"));
		OLEDsetXY(8, 4);
		OLEDtext(F("BREAK"));
		OLEDsetXY(8, 5);
		OLEDtext(F("LIST"));
		OLEDsetXY(8, 6);
		OLEDtext(F("CR"));		
		OLEDsetXY(8, 7);
		OLEDtext(F("CR"));			
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

		//---------<xxxxxxxxxxx>----
		OLEDtext(F(" SETTINGS"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		OLEDsetXY(0, 2);
		
		OLEDtext(F(" LOAD DEFAULTS"));
		
		//menuText(F("LOAD DEFAULTS"), &menuMap[2][1]);	
		//menuText(F("LOAD START"), &menuMap[3][1]);
		//menuText(F("PNTR JUMP"), &menuMap[7][1]);
	}
	
	//drawHex(memPointers[0], &menuMap[3][15]);
	//drawHex(pointerJump, &menuMap[7][15]);

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
		//---------<xxxxxxxxxxx>----
		OLEDtext(F("RAM DUMPER"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		for (int x = 2 ; x < 6 ; x++) {
			//menuText(F("RAMSLOT1.BIN"), &menuMap[x][1]);
			//menuMap[x][8] = x + 47;		
		}
		
		//menuText(F("START:"), &menuMap[6][5]);
		//menuText(F("  END:"), &menuMap[7][5]);
		
		OLEDsetXY(8, 2);
		OLEDtext(F("FILL RAM"));
		OLEDsetXY(8, 3);
		OLEDtext(F("RAM TO SD"));		
	}
	
	//drawHex(memPointers[0], &menuMap[6][15]);
	//drawHex(memPointers[1], &menuMap[7][15]);

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
			//fileName[x] = menuMap[menuY][1 + x];
		}
		if (menuY == 2) {
			RAMload();
		}
		if (menuY == 3) {
			dumpTest();
		}
		
		//RAMdump();					//Dump selected RAM range to indicated file		
	}		
	
}

void RAMviewerMenu() {
	
	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtext(F("RAM VIEWER"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		viewPointer = 0x0000;
		
		drawRAMcontents();
	}	

	if (dUp() and viewPointer > 47) {
		viewPointer -= 48;
		drawRAMcontents();
		
	}
	if (dDown() and viewPointer < 65487) {
		viewPointer += 48;
		drawRAMcontents();
		
	}

}

void CPUcontrolMenu() {
	
	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtext(F("CPU CONTROL"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		OLEDsetXY(8, 2);
		if (cpuState) {
			OLEDtext(F("PAUSE CPU "));
		}
		else {
			OLEDtext(F("RESUME CPU"));	
		}
		
		OLEDsetXY(8, 3);
		OLEDtext(F("SINGLE STEP"));
		OLEDsetXY(8, 4);
		OLEDtext(F("SPEED= 4MHZ"));
		
		OLEDsetXY(32, 7);
		OLEDtext(F("D:"));
		OLEDsetXY(80, 7);
		OLEDtext(F("A:"));
		
	}	
	
	standardMenuCursor();
	OLEDsetXY(48, 7);
	drawHexByte(byteIn());
	OLEDsetXY(96, 7);
	getCurrentAddress();

	
	if (buttonA()) {			//Execute a CPU control command
		switch(menuY) {
			case 2:
				OLEDsetXY(8, 2);
				if (cpuState) {
					cpuState = 0;
					TCCR4A = 0;				//Fast clock OFF
					PORTE &= 0xFB;
					OLEDtext(F("RESUME CPU"));	
					
				}
				else {
					cpuState = 1;
					PORTE &= 0xFB;
					TCCR4A = B10000010;					//Fast clock ON;
					OLEDtext(F("PAUSE CPU "));	
				}
			break;
			case 3:
				cpuState = 0;			//CPU off
				TCCR4A = 0;				//Fast clock OFF
				PORTE |= 0x04;					//Wait state
				delay(10);
				PORTE &= 0xFB;
				OLEDsetXY(8, 2);
				OLEDtext(F("RESUME CPU"));	
			break;
			case 4:
			
			break;
			case 5:

			break;	
			case 6:
			
			break;			
		}
		
	}
	
}

void drawRAMcontents() {

	requestBus();
	
	delay(5);
	
	TCCR4A = 0;				//Fast clock OFF;
	
	textSize = 0;
	
	dataHiZ();	//Assert bus
	memoryControl(1);			//Take control of memory
	addressControl();	
	
	uint16_t viewP = viewPointer;
	
	for (int y = 2 ; y < 8 ; y++) {
		
		OLEDsetXY(0, y);
		drawHex(viewP);
		OLEDchar(':' - 32);
		OLEDchar(0);
		for (int x = 0 ; x < 8 ; x++) {
		
			if (!(viewP & 0x00FF)) {							//Start of page? Assert page #. This reduces I2C access time
				addressAssertHalf(0x21, viewP >> 8);			//Set high byte page #				
			}
			
			addressAssertHalf(0x20, viewP & 0xFF);			//Set low byte #		
			digitalWrite(z80RD, 0);		//Do read
			drawHexByte(byteIn());
			digitalWrite(z80RD, 1);			
			OLEDchar(0);	
			
			viewP++;

		}

				
	}
	
	textSize = 1;

	memoryControl(0);		//Release memory controls
	addressRelease();		//Release address bus
	
	TCCR4A = B10000010;				//Fast clock ON;	
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go	

}

void SDLoadMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu

		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtext(F(" SD LOADER"));
		
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
				OLEDsetXY(0, menuY);				//Clear the text portion
				OLEDchar(0);					//Draw cursor				
				++menuY;	
				OLEDsetXY(0, menuY);				//Clear the text portion
				OLEDchar('>' - 32);					//Draw cursor				
			}
		}
		
		drawFileSize();
		
	}
	
	if (dUp()) {
		
		if (menuY == 2) {			//If at the top, check if we can scroll up		
			if (firstFile > 1) {
				firstFile--;
				drawFiles();	
			}		
		}
		else {						//Else just move cursor up
			OLEDsetXY(0, menuY);				//Clear the text portion
			OLEDchar(0);					//Draw cursor				
			--menuY;	
			OLEDsetXY(0, menuY);				//Clear the text portion
			OLEDchar('>' - 32);					//Draw cursor		
		}
		
		drawFileSize();
		
	}	
	
	if (buttonA()) {
		drawFiles();		//Update the filename list so the selected filename will be stored to the filename buffer

		OLEDsetXY(0, 1);
		OLEDfillLine(13, 0x80);			//Draw a line across row 1 (up to logo)
		
		OLEDsetXY(0, 1);
		
		progressBarTicks = fileSize[menuY - 2] / 105;			//Figure out progress bar increment
		
		streamLoad();	//Load the selected file on screen
	}	

}

void standardMenuCursor() {

	if (dDown() and menuY < 7) {
		OLEDsetXY(0, menuY);
		OLEDchar(0);			//Clear old cursor
		OLEDsetXY(0, ++menuY);
		OLEDchar(30);			//Clear old cursor		
				
	}
	
	if (dUp() and menuY > 2) {
		OLEDsetXY(0, menuY);
		OLEDchar(0);			//Clear old cursor
		OLEDsetXY(0, --menuY);
		OLEDchar(30);			//Clear old cursor					
	}
	
}

void drawFiles() {

	for (int x = 2 ; x < 7 ; x++) {
		OLEDclearRow(x);
	}

	OLEDsetXY(0, menuY);				//Clear the text portion
	OLEDchar('>' - 32);					//Draw cursor
		
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
		
		if (drawRow == 8) {									//Stop drawing filenames
			break;
		}
		
		filesFound++;
		
		OLEDsetXY(8, drawRow);
		
		if (filesFound >= firstFile) {
			if (!entry.isDirectory()) {
				fileSize[drawRow - 2] = entry.size();
				menuTextFile(entry.name());
				if (drawRow == menuY) {					//Is cursor pointing to this entry? Copy filename to buffer in case it's selected
					getFileName(entry.name());
				}
				drawRow++;
			}							
		}
		
		entry.close();

	}	

	root.close();
	
	drawFileSize();
	
}

void drawFileSize() {

	cursorY = 1;
	OLEDsetXY(0, 1);
	textSize = 0;
	OLEDtext(F("File size="));
	OLEDsetXY(40, 1);
	drawHex(fileSize[menuY - 2] - 1);		//Keep it to load range space not actual # of bytes (thus a 655356 byte file appears as FFFF)
	textSize = 1;	

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

void streamLoad() {

	rebootFlag = 0;
	
	requestBus();
	
	delay(5);
	
	TCCR4A = 0;				//Fast clock OFF;
	
	File whichFile = SD.open(fileName);

	OLEDsetXY(2 << 3, 0);			//Draw menu title here	
	//---------<xxxxxxxxxxx>----
	OLEDtext(F("LOADING..."));

	memoryControl(1);			//Take control of memory
	dataOut();	//Assert bus
	uint16_t errors = 0;
	addressControl();

	uint16_t progressBar = 0;
	uint8_t progressBarX = 0;
		
	uint16_t memPointer = memPointers[0];	//Starting address
	
	while(whichFile.available()) {

		uint8_t toWrite = whichFile.read();

		if ((memPointer & 0x00FF) == 0) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #
			Serial.print("Loading page ");
			Serial.println(memPointer >> 8);
		}

		addressAssertHalf(0x20, memPointer++ & 0xFF);			//Set low byte #		

		//delayMicroseconds(5);

		uint8_t byteChecked = 1;
		
		while(byteChecked) {
			
			dataOut();	//Assert bus
			byteOut(toWrite);										//Assert data
			digitalWrite(z80WR, 0);		//Strobe the write		
			delayMicroseconds(2);			
			digitalWrite(z80WR, 1);
			delayMicroseconds(2);
			
			dataHiZ();
			
			delayMicroseconds(2);

			digitalWrite(z80RD, 0);		//Check if byte is correct
			delayMicroseconds(3);
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

		if (progressBar++ == progressBarTicks) {			//Update progress bar
			OLEDsetXY(progressBarX++, 1);
			Wire.beginTransmission(_i2caddr);
			Wire.write(0x40);
			Wire.write(0xFF);        //Draw a sloped progress bar so it looks cool
			Wire.write(0x9F);
			Wire.write(0x83);
			Wire.endTransmission();
			progressBar = 0;
		
		}
	
	}

	whichFile.close();

	displayModeChangeTo(showTerminal);
	whichMenu |= 0x80;					//Flag to reload last used menu
	TCCR4A = B10000010;					//Fast clock ON;		
	startZ80();	

}

void dumpTest() {

	requestBus();
	
	TCCR4A = 0;				//Fast clock OFF;

	if (SD.exists("TEST.BIN")) {			//If file exists erase it and start over
		SD.remove("TEST.BIN");
	}
	
	File whichFile = SD.open("TEST.BIN", FILE_WRITE);

	//menuClear();
	//menuText(F("DUMPING"), &menuMap[5][4]);
	//displayMenu(8);
	
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

void RAMload() {

	memoryControl(1);			//Take control of memory
	dataOut();	//Assert bus
	addressControl();

	uint16_t progressBar = 0;
	uint8_t progressBarX = 0;
		
	uint16_t memPointer = memPointers[0];	//Starting address
	
	
	while(memPointer < 2096) {

		if ((memPointer & 0x00FF) == 0) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #
		}

		addressAssertHalf(0x20, memPointer & 0xFF);			//Set low byte #		

		dataOut();	//Assert bus
		byteOut(memPointer++ & 0xFF);										//Assert data
		digitalWrite(z80WR, 0);		//Strobe the write		
		digitalWrite(z80WR, 1);
			
	}

}


void RAMdump() {

	requestBus();
	
	TCCR4A = 0;				//Fast clock OFF;
	
	if (SD.exists(fileName)) {			//If file exists erase it and start over
		SD.remove(fileName);
	}

	File whichFile = SD.open(fileName, FILE_WRITE);

	//menuClear();
	//menuText(F("DUMPING"), &menuMap[5][4]);
	//displayMenu(8);
	
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
	
	//Serial.println(F("Requesting Z80 bus"));
	
	digitalWrite(z80BUSREQ, 0);			//Request bus

	delay(5);
	
}

void memoryControl(uint8_t state) {

	if (state) {					//1 = Take control of memory

		digitalWrite(z80WR, 1);		//Ensure it's high
		pinMode(z80WR, OUTPUT);

		digitalWrite(z80RD, 1);			
		pinMode(z80RD, OUTPUT);

	}
	else {							//0 = release memory

		pinMode(z80WR, INPUT);			//Write
		digitalWrite(z80WR, 1);			//With pullups
		pinMode(z80RD, INPUT);			//Read
		digitalWrite(z80RD, 1);			//With pullups
	
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

		if (cpuState) {
			TCCR4A = B10000010;		//Turn fast clock back on
		}

		serialFlag = 0;			//Clear flag for serial bytes
	
	}
	else {						//Else it's a write (to us)

		char RX = (PINB & 0xF0) | (PINF >> 4);		//Get the data the Z80 is writing to us

		PORTE |= 0x04;					//Wait state
		PORTE &= 0xFB;

		PORTE |= 0x04;					//T3 state
		PORTE &= 0xFB;

		if (cpuState) {
			TCCR4A = B10000010;		//Turn fast clock back on
		}
		
		if (displayMode == showTerminal) {

			scBuffer[scBuffPointWrite++] = RX; 			//Load byte into buffer
			
			if (scBuffPointWrite == bufferSize) {		//Go around ring buffer
				scBuffPointWrite = 0;
			}			
			
		}
		Serial.write(RX); 		//Send data along as USB serial

	}
	
}

void charPrint(uint8_t theChar) {

	//If not opcode or payload then it's just normal text data. Print in on the OLED

	if (cursorStatus) {
		blink = 4400;			//Set this so cursor appears on next line immediately
		OLEDchar(0);
		OLEDsetXY(cursorX << 2, cursorY);		
	}	

	if (theChar < 31) {				//Did we get an opcode byte and not currently loading payload bytes?

		execOpCode(theChar);					//Execute immediately
		return;								//Exit function	
	
		/*
		if (payLoadSize[theChar] == 0) {			//No payload bytes required?
			execOpCode(theChar);					//Execute immediately
			return;								//Exit function	
		}
		else {								//Payload bytes required?
			opCode = theChar;				//Set opCode loading flag			
			payLoadPointer = 0;				//Reset the buffer pointer
			return;							//Exit function					
		}
		
		*/
	
	}

	OLEDchar(theChar - 32);	

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

		OLEDclearRow(winY);						//Erase line

		if (++winY == rows) {								//Did we reach the end of the buffer?
			winY = 0;										//Reset back to beginning
		}
		
		OLEDrow0(winY << 3);
	}	

	OLEDsetXY(cursorX << 2, cursorY);
	
}

void execOpCode(uint8_t whichOpCode) {		//When all payload bytes have been collected vector here to execute the opcode

	switch(whichOpCode) {

		case 1:				//Set terminal display width
			
		break;
	
		case 8:				//Backspace
			if (cursorX > 0) {
				cursorX--;
				OLEDsetXY(cursorX << 2, cursorY);
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

	winY = 0;
	OLEDclear();
	textSize = 0;

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

void drawHex(uint16_t theValue) {

	for (int x = 0 ; x < 4 ; x++) {
	
		uint8_t theDigit = (theValue >> 12) & 0x0F;

		if (theDigit < 10) {
			OLEDchar(theDigit + 16);	//0-9
		}
		else {
			OLEDchar(theDigit + 23);	//A-F			
		}

		theValue <<= 4;			//Shift one nibble
	
	}

	
}

void drawHexByte(uint16_t theValue) {

	for (int x = 0 ; x < 2 ; x++) {
	
		uint8_t theDigit = (theValue >> 4) & 0x0F;

		if (theDigit < 10) {
			OLEDchar(theDigit + 16);	//0-9
		}
		else {
			OLEDchar(theDigit + 23);	//A-F			
		}

		theValue <<= 4;			//Shift one nibble
	
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

void menuTextFile(const char *str) {

  while (*str) {
	OLEDchar(*str++ - 32);  
  }

}

void getFileName(const char *str) {

	int x = 0;

	while(*str) {
		fileName[x++] = *str++;
		
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

void getCurrentAddress() {
	
	Wire.beginTransmission(0x21);				//Address IO
	Wire.write(0x00);							//Select read register
	Wire.endTransmission();
	
	Wire.requestFrom(0x21, 1);    				//Get address high byte

	drawHexByte(Wire.read());

	Wire.beginTransmission(0x20);				//Address IO
	Wire.write(0x00);							//Select read register
	Wire.endTransmission();
	
	Wire.requestFrom(0x20, 1);    				//Get address low byte

	drawHexByte(Wire.read());	
	
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

	xPos = x;			//Update the values
	yPos = y;
	
}

void OLEDchar(uint8_t theChar) {

	Wire.beginTransmission(_i2caddr);
	Wire.write(0x40);

	if (textSize) {			//Large text?
		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font8x8 + (theChar << 3) + col));             
		}	
		xPos += 8;
	}
	else {					//Small text
	
		if (displayMode == showMenu && cursorY == 1) {		//Drawing small text on the menu line?
			for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
				Wire.write((pgm_read_byte_near(font4x8 + (theChar << 2) + col) >> 1) | 0x80);	//OR in the bottom line             
			}			
		}
		else {
			for (uint8_t col = 0 ; col < 4 ; col++) {           //Send the 4 horizontal lines to the OLED
				Wire.write(pgm_read_byte_near(font4x8 + (theChar << 2) + col));             
			}				
		}
		xPos += 4;

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

void OLEDfillLine(uint8_t numChars, uint8_t fillByte) {

	for (int x = 0 ; x < numChars ; x++) {

		Wire.beginTransmission(_i2caddr);
		Wire.write(0x40);
		
		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 4 horizontal lines to the OLED
			Wire.write(fillByte);             
		}	

		Wire.endTransmission();
		
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

	for (int x = 0 ; x < 16 ; x++) {							//Go slightly past to make sure we nuke all data
		
		Wire.beginTransmission(_i2caddr);					//Send chars in groups of 2 (16 bytes per transmission)
		Wire.write(0x40);	
		
		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(0);             
		}
		
		Wire.endTransmission();
	
	}

}

void OLEDclear() {

	for (int x = 0 ; x < 8 ; x++) {			
		OLEDclearRow(x);
	}
	
	OLEDrow0(0);
	OLEDsetXY(0, 0);	
	cursorX = 0;
	cursorY = 0;
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
