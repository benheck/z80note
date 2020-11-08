#include "defines.h"
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
//#include <EEPROM.h>

#define _i2caddr		0x3C				//Address of I2C OLED screen

#define atmelSelect 	1						//The digital pin the Z80 selects the Atmel IO with
#define	z80Reset		7
#define z80WR			12
#define z80RD			5
#define z80BUSREQ		6
#define z80IRQ			A4

#define joyUp			0x01
#define joyDown			0x02
#define joyLeft			0x04
#define joyRight		0x08
#define joyA			0x10
#define joyB			0x20
#define joyMenu			0x40
#define joyStart		0x80

#define bufferSize		64				//Size of terminal buffer

#define updateMenu		0x80			//Setting this MSB tells us to redraw the menu
#define showTerminal	0x40			//2nd MSB set means show terminal (so we don't lose place of Menu position)
#define showMenu 	 	1

#define menuMax			5						//Max number of menu screens (for scrolling between them)

#define addressControl	0x00
#define addressRelease	0xFF

#define clockOn			B10000010
#define clockOff		0x00

#define speed1MHZ			0
#define speed2MHZ			1
#define speed4MHZ			2
#define speed6MHZ			3
#define speed8MHZ			4

uint8_t serialFlag = 0;

char ourByte = 0;

uint8_t winY = 0;
uint8_t cursorY = 0;
uint8_t cursorX = 0;

uint8_t cpuState = 1;					//CPU state (1 = run 0 = single step)
uint8_t sqWave = 0;						//For drawing the square wave for single stepping

uint16_t progressBar;
uint8_t progressBarX;

uint8_t scBuffer[bufferSize];					//Buffer for terminal mode to print chars to OLED
uint8_t scBuffPointWrite = 0;			//Point the Z80 writes to
uint8_t scBuffPointRead = 0;			//Point the MCU reads from to draw to screen
uint8_t fileName[13];
uint16_t selectedFileSize;

uint16_t progressBarTicks;					//How many bytes must be read for the progress bar to advance 1 pixel to the right
	
uint8_t cursorStatus = 1;					//If cursor is on or not
uint16_t blink;								//For to blink da cursor

uint16_t viewPointer = 0;					//Pointer for RAM viewer
uint16_t loadStart = 0x0000;				//Starting point in memory for copying SD files into
uint16_t dumpRange[] = {0x0000, 0x1FFF};	//Range for dumping RAM to SD
uint16_t pointerJump = 2048;				//How much the pointer moves when you change it
uint8_t menuLeftRight = 1;					//By default this is enabled

uint8_t dPadBounceEnable = 0xFF;			//At the start all buttons require re-trigger
uint8_t dPadBounce = 0;						//The debounce clear bit for each button	

uint8_t displayMode = 1;						//0 = terminal 1 = menu
uint8_t whichMenu = 0x81;						//Which menu we are in if in menu mode (such as SD load, RAM dump, setting etc) Boots to SD Loader menu

uint8_t controls;					//Samples the joystick for the Atmel

uint8_t animation = 0;

//uint8_t menuX = 0;					
uint8_t menuY = 0;					//Menu cursors
uint8_t menuYmax;					//The maximum far down you can move the menuY cursor when selecting a file
uint8_t firstFile = 0;				//The first file we should display (if we scroll past 6 entires)

uint8_t BASICtype = 0;				//Flag if we're having the MCU type in BASIC commands such as RUN (to start a game without keyboard for instance)

uint8_t textSize = 0;				//0 = small 1 = large

uint8_t z80speedMultipler;			//Stores the current speed (0-4, see clockPrint for the MHZ values)

uint8_t clockPrint[] =  {17, 18, 20};			//For printing out the speed on the display 1, 2, 4, 6 and 8 MHZ
uint8_t clockTimerA[] = {16, 8, 4};
uint8_t clockTimerB[] = {24, 12, 6};

void setup() {

	Serial.begin(115200);				//Start USB serial
	Serial1.begin(115200);				//Start hardware UART
	UCSR1B &= ~(1 << 3);  				//Disable transmit on hardware UART (RX only, for example external serial keyboard). TX pin will be used for something else

	Wire.begin();							// I2C init
	
	for (int x = 0 ; x < 32 ; x++) {	//Setup OLED
		ssd1306_command(pgm_read_byte_near(OLEDboot + x));  	
	}	

	TWBR = 6;						//Change prescaler to increase I2C speed to max of 400KHz

	//digitalWrite(z80IRQ, 0);		//Low to start (should see if we can make this a pulled-up input for external devices)
	//pinMode(z80IRQ, INPUT);			//IRQ to Z80. Start this as an input so the pull-up resistor takes it to HIGH (defaults as 0)

	//digitalWrite(z80Reset, 0);		//Reset z80 right away (defaults as 0)
	//pinMode(z80Reset, OUTPUT);	
	//delay(1);
	//pinMode(z80Reset, INPUT);		//Release reset (defaults as 0)
	releaseMemory();				//Release memory (Atmel input state)

	DDRE |= (1 << 2);				//Slow clock (HWB, not sure if it's attached to a Digital Pin)

	//Z80 Clock Driver
	pinMode(13, OUTPUT);			//Set PWM pin as output_iterator
	setZ80speed(speed4MHZ);					//Setup high speed fast clock to drive Z80
	//OCR4A = 4;						//Setup high speed fast clock to drive Z80
	//OCR4C = 6;
	PLLFRQ = B01111010;
	TCCR4B = B00000001;
	fastClockControl(clockOn);					//TCCR4A = B10000010;

	dataHiZ();
	addressControlType(addressRelease);

	//pinMode(atmelSelect , INPUT);			//Atmel chip select (defaults as 0)
	attachInterrupt(digitalPinToInterrupt(atmelSelect), access, FALLING); 		//Setup interrupt vector for when Z80 sends bytes to MCU

	OLEDclear();

	if (!SD.begin(4)) {
		OLEDtextXY(32, 3, F("INSERT SD + REBOOT"));
		while(1) {}
	}
	else {
		OLEDtextXY(42, 3, F("SD BOOT OK"));
		delay(750);
	}

}

void loop() {

	controls = readBusExpander(0x22);		//Read the joystick from the bus expander

	if (dpadCheck(joyMenu)) {				//Swap between terminal and menu
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
			
			if (animation++ == 100) {
				OLEDsetXY(0, 0);
				OLEDchar(28);
				OLEDsetXY(12 << 3, 0);
				OLEDchar(30);				
			}
			
			if (animation == 200) {
				animation = 0;
				OLEDsetXY(0, 0);
				OLEDchar(59);
				OLEDsetXY(12 << 3, 0);
				OLEDchar(61);	
			}		
			
			if (dpadCheck(joyLeft)) { //dLeft()) {
				
				if (whichMenu == 0) {
					whichMenu = menuMax | 0x80;
				}
				else {
					whichMenu = (whichMenu - 1) | 0x80;
				}
		
			}

			if (dpadCheck(joyRight)) { //dRight()) {

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
	
	if (BASICtype and !serialFlag) {				//An internal command "typed" from the menu
	
		ourByte = fileName[BASICtype++];
		
		if (ourByte) {						//Character still present? Flag Z80
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
		break;
		
		case showMenu:		
			whichMenu |= 0x80;			//OR in the bit to flag to redraw the menu screen	
		break;	
		
	}
	
}

void setupMenu() {				//Basic background for all menus

	OLEDclear();
	//OLEDrow0(0);
	textSize = 1;

	OLEDsetXY(0, 2);
	OLEDchar('>' - 32);
	
	OLEDsetXY(0, 1);
	OLEDfillLine(13, 0x80);			//Draw a line across row 1 (up to logo)

	OLEDchar(26);					//Bottom of logo	
	OLEDchar(27);
	OLEDchar(60);

	OLEDtextXY(13 << 3,0, F("/?@"));

	//OLEDsetXY(1 << 3, 0);			//Draw menu title here	
}

void BASICmenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtextXY(32, 0, F("BASIC"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		OLEDtextXY(8, 2, F("W"));
		OLEDtextXY(8, 3, F("C"));			
		OLEDtextXY(8, 4, F("RUN"));
		OLEDtextXY(8, 5, F("BREAK"));
		OLEDtextXY(8, 6, F("LIST"));
		OLEDtextXY(8, 7, F("CR"));		
		
	}

	standardMenuCursor();
	
	if (dpadCheck(joyA)) {			//Execute a BASIC command
		fileName[2] = 0;			//2nd character string terminator (since most are 1 char commands)
		switch(menuY) {			
			case 2:
				fileName[1] = 'W';		//W for Warm Start (don't erase RAM)
			break;
			case 3:			
				fileName[1] = 'C';		//C for Cold Start (erase RAM)
			break;
			case 4:
				BASICcommand("RUN");	
			break;
			case 5:
				fileName[1] = 3;		//Break (cntrl-C)	
			break;	
			case 6:
				BASICcommand("LIST");
			break;			
			case 7:
				fileName[1] = 13;		//CR		
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
		OLEDtextXY(24, 0, F("OPTIONS"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		OLEDtextXY(8, 2, F("LOAD DEFAULTS"));
		OLEDtextXY(8, 3, F("LOAD START"));
		OLEDtextXY(8, 4, F("PTR JUMP"));		
		OLEDtextXY(8, 5, F("CLEAR RAM"));
		
	}
	
	OLEDsetXY(95, 3);
	drawHexWord(loadStart);
	OLEDsetXY(95, 4);	
	drawHexWord(pointerJump);

	standardMenuCursor();	

	if (menuY > 2) {
		menuLeftRight = 0;
		
		if (dpadCheck(joyLeft)) {
			
			switch(menuY) {

				case 3:				
					loadStart -= pointerJump;				
				break;
				
				case 4:
					if (pointerJump > 256) {
						pointerJump >>= 1;
					}				
				break;
			
				
			}
			

		}
		
		if (dpadCheck(joyRight)) {
			
			switch(menuY) {
				
				case 3:				
					loadStart += pointerJump;				
				break;	
			
				case 4:
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
	
	if (dpadCheck(joyA)) {			//Execute a CPU control command
		switch(menuY) {
			case 5:
				RAMclear();
			break;	
			case 6:
			
			break;			
		}
		
	}	

}

void RAMDUMPMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtextXY(16, 0, F("RAM DUMP"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		for (int x = 2 ; x < 6 ; x++) {
			OLEDtextXY(8, x, F("^SLOT1.BIN"));
			OLEDsetXY(48, x);
			OLEDchar(x + 15);	//Number the slots on screen
		}
		
		//menuText(F("START:"), &menuMap[6][5]);
		//menuText(F("  END:"), &menuMap[7][5]);
		
		//OLEDsetXY(8, 2);
		OLEDtextXY(8, 6, F("DUMP START"));
		//OLEDsetXY(8, 3);
		OLEDtextXY(8, 7, F("DUMP END"));		
	}
	
	standardMenuCursor();

	OLEDsetXY(95, 6);
	drawHexWord(dumpRange[0]);
	OLEDsetXY(95, 7);
	drawHexWord(dumpRange[1]);
	
	if (menuY > 5) {
		menuLeftRight = 0;
		
		if (dpadCheck(joyLeft)) {
			dumpRange[menuY - 6] -= pointerJump;
		}
		
		if (dpadCheck(joyRight)) {
			dumpRange[menuY - 6] += pointerJump;			
		}
	}
	else {
		menuLeftRight = 1;
	}
	
	if (dpadCheck(joyA)) {
		
		for (int x = 0 ; x < 10 ; x++) {						//Copy slot filename	
			fileName[x] = pgm_read_byte_near(slotFilename + x);
		}
		fileName[4] = menuY + 47;	//Number it based on cursor Y position (change to ASCII)
		RAMdump();					//Dump selected RAM range to indicated file		
	}		
	
}

void RAMviewerMenu() {
	
	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtextXY(16, 0, F("RAM VIEW"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		//viewPointer = 0x0000;
		
		drawRAMcontents();
	}	

	if (dpadCheck(joyUp) and viewPointer > 47) {
		viewPointer -= 48;
		drawRAMcontents();
		
	}
	if (dpadCheck(joyDown) and viewPointer < 65487) {
		viewPointer += 48;
		drawRAMcontents();
		
	}

}

void CPUcontrolMenu() {
	
	if (whichMenu & 0x80) {			//MSB set? Redraw menu
		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtextXY(40, 0, F("Z80"));
		
		menuY = 2;

		whichMenu &= 0x7F;				//AND off the MSB

		//OLEDsetXY(8, 2);
		if (cpuState) {
			OLEDtextXY(8, 2, F("PAUSE"));
		}
		else {
			OLEDtextXY(8, 2, F("RESUME"));	
		}
		
		//OLEDsetXY(8, 3);
		OLEDtextXY(8, 3, F("STEP"));
		//OLEDsetXY(8, 4);
		OLEDtextXY(8, 6, F("SPEED 0MHZ"));

		sqWave = 0;
	
	}	
	
	standardMenuCursor();
	drawBus();

	OLEDsetXY(56, 6);
	OLEDchar(clockPrint[z80speedMultipler]);
	
	if (dpadCheck(joyA)) {			//Execute a CPU control command
		switch(menuY) {
			case 2:
				//OLEDsetXY(8, 2);
				if (cpuState) {
					cpuState = 0;
					fastClockControl(clockOff);				//Fast clock OFF
					PORTE &= 0xFB;
					OLEDtextXY(8, 2, F("RESUME"));	
					
				}
				else {
					cpuState = 1;
					PORTE &= 0xFB;
					fastClockControl(clockOn);					//Fast clock ON;
					OLEDtextXY(8, 2, F("PAUSE "));	
					OLEDsetXY(0, 1);
					OLEDfillLine(13, 0x80);			//Draw a line across row 1 (up to logo)
					sqWave = 0;
				}
			break;
			case 3:
				cpuState = 0;			//CPU step
				fastClockControl(clockOff);				//Fast clock OFF
				PORTE |= 0x04;					//Wait state
				delay(10);
				drawBus();
				PORTE &= 0xFB;
				//OLEDsetXY(8, 2);
				OLEDtextXY(8, 2, F("RESUME"));
				drawWave();

			break;
			case 4:

			break;
			case 5:

			break;	
			case 6:
			
			break;			
		}
		
	}
	
	if (menuY == 6) {
		menuLeftRight = 0;
		

		if (dpadCheck(joyLeft) and z80speedMultipler > 0) {
			setZ80speed(z80speedMultipler - 1);
		}
		
		if (dpadCheck(joyRight) and z80speedMultipler < 2) {
			setZ80speed(z80speedMultipler + 1);		
		}
	}
	else {
		menuLeftRight = 1;
	}	
	
}

void SDLoadMenu() {

	if (whichMenu & 0x80) {			//MSB set? Redraw menu

		setupMenu();					//Basic menu text
		//---------<xxxxxxxxxxx>----
		OLEDtextXY(16, 0, F(" SD LOAD"));
		
		firstFile = 1;					//To start, the first file list should be the first one found on the card (no scroll)
		menuY = 2;						//Reset selection cursor			
		drawFiles();		
		whichMenu &= 0x7F;				//AND off the MSB
	
	}

	if (dpadCheck(joyDown)) {
	
		if (menuY == 7 and menuYmax > menuY) {		//Did we reach the botton and are there more entries? Scroll down
			firstFile++;
				
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
		drawFiles();
		//drawFileSize();
		
	}
	
	if (dpadCheck(joyUp)) {
		
		if (menuY == 2) {			//If at the top, check if we can scroll up		
			if (firstFile > 1) {
				firstFile--;
				//drawFiles();	
			}		
		}
		else {						//Else just move cursor up
			OLEDsetXY(0, menuY);				//Clear the text portion
			OLEDchar(0);					//Draw cursor				
			--menuY;	
			OLEDsetXY(0, menuY);				//Clear the text portion
			OLEDchar('>' - 32);					//Draw cursor		
		}
		drawFiles();
		//drawFileSize();
		
	}	
	
	if (dpadCheck(joyA)) {
		drawFiles();		//Update the filename list so the selected filename will be stored to the filename buffer

		OLEDsetXY(0, 1);
		OLEDfillLine(13, 0x80);			//Draw a line across row 1 (up to logo)
		
		OLEDsetXY(0, 1);
		
		progressBarTicks = selectedFileSize / 105;			//Figure out progress bar increment
		//progressBarTicks = fileSize[menuY - 2] / 105;			//Figure out progress bar increment
		
		streamLoad();	//Load the selected file on screen
	}	

}

void standardMenuCursor() {

	delay(1);					//So it doesn't run too fast :)

	/*

	if (menuLeftRight) {
		
		if (animation++ == 100) {
			OLEDsetXY(0, 0);
			OLEDchar(28);
			OLEDsetXY(12 << 3, 0);
			OLEDchar(30);				
		}
		
		if (animation > 200) {
			animation = 0;
			OLEDsetXY(0, 0);
			OLEDchar(59);
			OLEDsetXY(12 << 3, 0);
			OLEDchar(61);	
		}		
		
		if (dpadCheck(joyLeft)) { //dLeft()) {
			
			if (whichMenu == 1) {					//Roll around to ending menu
				whichMenu = menuMax | 0x80;
			}
			else {
				whichMenu = (whichMenu - 1) | 0x80;
			}
	
		}

		if (dpadCheck(joyRight)) { //dRight()) {

			if (whichMenu == menuMax) {				//Roll around to starting menu
				whichMenu = 0x80;
			}
			else {
				whichMenu = (whichMenu + 1) | 0x80;
			}

		}				
	}
	
	*/

	if (dpadCheck(joyDown) and menuY < 7) {
		OLEDsetXY(0, menuY);
		OLEDchar(0);			//Clear old cursor
		OLEDsetXY(0, ++menuY);
		OLEDchar(30);			//Clear old cursor		
				
	}
	
	if (dpadCheck(joyUp) and menuY > 2) {
		OLEDsetXY(0, menuY);
		OLEDchar(0);			//Clear old cursor
		OLEDsetXY(0, --menuY);
		OLEDchar(30);			//Clear old cursor					
	}
	
}

void drawRAMcontents() {

	requestBus();

	fastClockControl(clockOff);				//Fast clock OFF;
	
	textSize = 0;
	
	//dataHiZ();	//Assert bus
	memoryControl(1);			//Take control of memory
	addressControlType(addressControl);	
	
	uint16_t viewP = viewPointer;
	
	for (int y = 2 ; y < 8 ; y++) {
		
		OLEDsetXY(0, y);
		drawHexWord(viewP);
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

	busRelease();			//Go high-z and release the bus
	fastClockControl(clockOn);				//Fast clock ON;	
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go	

}

void drawBus() {

	OLEDsetXY(72, 7);
	drawHexByte(byteIn());						//Draw data bus
	OLEDchar(0);								//Draw a space							
	drawHexByte(readBusExpander(0x21));			//Get and draw address high byte
	drawHexByte(readBusExpander(0x20));			//Get and draw address high byte
	
}

void drawWave() {

	if (sqWave > 96) {
		sqWave = 0;
		OLEDsetXY(sqWave, 1);
		OLEDfillLine(13, 0x80);			//Draw a line across row 1 (up to logo)
	}

	OLEDsetXY(sqWave, 1);
		
	Wire.beginTransmission(_i2caddr);
	Wire.write(0x40);

	Wire.write(0x80); 	
	Wire.write(0x80); 	
	Wire.write(0xF0); 	
	Wire.write(0x10); 	
	Wire.write(0x10); 	
	Wire.write(0xF0);
		
	Wire.endTransmission();		

	sqWave += 6;		
	
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
				menuTextFile(entry.name());				//Draw filename on screen
				if (drawRow == menuY) {					//Is cursor pointing to this entry? Copy filename to buffer in case it's selected
					getFileName(entry.name());
					selectedFileSize = entry.size();
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

	//cursorY = 1;
	textSize = 0;
	//OLEDsetXY(0, 1);
	OLEDtextXY(32, 1, F("size="));
	drawHexWord(selectedFileSize - 1);
	//drawHexWord(fileSize[menuY - 2] - 1);		//Keep it to load range space not actual # of bytes (thus a 655356 byte file appears as FFFF)
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

uint8_t dpadCheck(uint8_t whichDir) {

	if (controls & whichDir) {					//Pressed? See if bit is set

		if (dPadBounceEnable & whichDir) {		//Checking for retriggers?
			if (dPadBounce & whichDir) {		//Bit still set? No dice
				return 0;
			}
			dPadBounce |= whichDir;			//Bit clear? Set it
			return 1;						//and return status		
		}
		else {
			return 1;
		}
	}
	else {
		dPadBounce &= ~whichDir;			//Button not pressed? Clear the bit, allowing a retrigger
		return 0;
	}
	
}

void streamLoad() {

	requestBus();

	fastClockControl(clockOff);				//Fast clock OFF;
	
	File whichFile = SD.open(fileName);

	OLEDeraseMenu();
	OLEDtextXY(3 << 3, 0, F("LOADING"));

	memoryControl(1);			//Take control of memory
	dataOut();	//Assert bus
	uint16_t errors = 0;
	addressControlType(addressControl);

	progressBar = 0;
	progressBarX = 0;
		
	uint16_t memPointer = loadStart;							//Starting address
	
	while(whichFile.available()) {

		uint8_t toWrite = whichFile.read();

		if ((memPointer & 0x00FF) == 0) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #
			//Serial.print("Loading page ");
			//Serial.println(memPointer >> 8);
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
				//Serial.print("Byte ");
				//Serial.print(memPointer & 0xFF);
				//Serial.println(" error, re-load to RAM...");
				dataOut();	//Assert bus
				delay(1);
			}			
			else {
				byteChecked = 0;
			}
		
		}

		if (progressBar++ == progressBarTicks) {			//Update progress bar
			drawProgressBar();		
		}
	
	}

	whichFile.close();

	displayModeChangeTo(showTerminal);
	whichMenu = 0;							//Jump right to BASIC help if user pushes MENU button
	fastClockControl(clockOn);				//Fast clock ON;		
	startZ80();	

}

void RAMclear() {

	requestBus();

	fastClockControl(clockOff);				//Fast clock OFF;
	
	OLEDeraseMenu();
	OLEDtextXY(3 << 3, 0, F("LOADING"));

	memoryControl(1);			//Take control of memory
	dataOut();	//Assert bus
	uint16_t errors = 0;
	addressControlType(addressControl);

	progressBar = 0;
	progressBarX = 0;
		
	uint16_t memPointer = loadStart;							//Starting address
	
	progressBarTicks = 65535/104;
	
	while(memPointer < 65534) {

		if ((memPointer & 0x00FF) == 0) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #
			//Serial.print("Loading page ");
			//Serial.println(memPointer >> 8);
		}

		addressAssertHalf(0x20, memPointer++ & 0xFF);			//Set low byte #		

	
		dataOut();	//Assert bus
		byteOut(0);										//Assert data
		digitalWrite(z80WR, 0);		//Strobe the write		
		delayMicroseconds(2);			
		digitalWrite(z80WR, 1);
		delayMicroseconds(2);
	
		if (progressBar++ == progressBarTicks) {			//Update progress bar
			drawProgressBar();		
		}
	
	}	
	
}

void RAMdump() {

	requestBus();
	
	fastClockControl(clockOff);				//Fast clock OFF;
	
	if (SD.exists(fileName)) {			//If file exists erase it and start over
		SD.remove(fileName);
	}

	File whichFile = SD.open(fileName, FILE_WRITE);

	OLEDtextXY(3 << 3, 0, F("DUMPING"));

	progressBar = 0;
	progressBarX = 0;
	
	progressBarTicks = (dumpRange[1] - dumpRange[0]) / 105;			//Figure out size for drawing progress bar
	
	memoryControl(1);			//Take control of memory
	dataHiZ();	//Assert bus
	addressControlType(addressControl);
	
	uint16_t memPointer = dumpRange[0];				//Starting address
	digitalWrite(z80RD, 0);		//Do read

	while(1) {

		if (!(memPointer & 0x00FF)) {							//Start of page? Assert page #. This reduces I2C access time
			addressAssertHalf(0x21, memPointer >> 8);			//Set high byte page #				
			//Serial.print("Dumping page ");
			//Serial.println(memPointer >> 8);
		}
		
		addressAssertHalf(0x20, memPointer & 0xFF);			//Set low byte #		
		delayMicroseconds(1);

		uint8_t toCheck = byteIn();			

		whichFile.write(toCheck);	//Write to file
		
		if (memPointer++ == dumpRange[1]) {				//Did we do the last byte? Done!
			break;
		}

		if (progressBar++ == progressBarTicks) {			//Update progress bar
			drawProgressBar();		
		}
	
	}
	
	digitalWrite(z80RD, 1);

	whichFile.close();
	
	//Serial.println(F("done"));
	
	fastClockControl(clockOn);				//Fast clock ON;
	
	busRelease();
	//memoryControl(0);		//Release memory controls
	//dataHiZ();				//Make sure data bus is released
	//addressRelease();		//Release address bus
	
	digitalWrite(z80BUSREQ, 1);	//Release bus request and you're ready to go	

	whichMenu |= 0x80;		//Trigger a menu redraw
	
}

void drawProgressBar() {

	OLEDsetXY(progressBarX++, 1);
	Wire.beginTransmission(_i2caddr);
	Wire.write(0x40);
	Wire.write(0xFF);        //Draw a sloped progress bar so it looks cool
	Wire.write(0x9F);
	Wire.write(0x83);
	Wire.endTransmission();
	progressBar = 0;	
	
}
	
void startZ80() {

	//Serial.println(F("Resetting Z80"));

	busRelease();
	//memoryControl(0);		//Release memory controls
	//dataHiZ();				//Make sure data bus is released
	//addressRelease();		//Release address bus
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

		releaseMemory();
	
	}
	
	
}

void releaseMemory() {

	pinMode(z80WR, INPUT);			//Write
	digitalWrite(z80WR, 1);			//With pullups
	pinMode(z80RD, INPUT);			//Read
	digitalWrite(z80RD, 1);			//With pullups
	
}

void busRelease() {

	memoryControl(0);		//Release memory controls
	dataHiZ();				//Make sure data bus is released
	addressControlType(addressRelease);		//Release address bus	
	
}

void access() {

	fastClockControl(clockOff);				//Fast clock OFF;

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
			fastClockControl(clockOn);		//Turn fast clock back on
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
			fastClockControl(clockOn);		//Turn fast clock back on
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

	//Print in on the OLED

	if (cursorStatus) {
		blink = 4400;			//Set this so cursor appears on next line immediately
		OLEDchar(0);
		OLEDsetXY(cursorX << 2, cursorY);		
	}	

	if (theChar < 31) {				//Did we get an opcode byte and not currently loading payload bytes?
		execOpCode(theChar);					//Execute immediately
		return;								//Exit function	
	}

	OLEDchar(theChar - 32);	

	if (++cursorX == 32) {
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

		if (++winY == 8) {								//Did we reach the end of the buffer?
			winY = 0;										//Reset back to beginning
		}
		
		OLEDrow0(winY << 3);
	}	

	OLEDsetXY(cursorX << 2, cursorY);
	
}

void execOpCode(uint8_t whichOpCode) {		//When all payload bytes have been collected vector here to execute the opcode

	switch(whichOpCode) {


		case 8:				//Backspace
			if (cursorX > 0) {
				cursorX--;
				OLEDsetXY(cursorX << 2, cursorY);
			}	
			
		case 12:			//Clear screen
			clearTerminal();
		break;
		
		case 13:			//Carriage return (use this to start new line, better terminal compatibility)
			cursorX = 0;
			advanceLine();
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

void drawHexWord(uint16_t theValue) {

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
  
	unsigned char c;

	do{
		c = pgm_read_byte(p++);			//Get byte from memory	
		*menuMemPoint++ = c-32;
	} while(c);

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

uint8_t readBusExpander(uint8_t whichDevice) {			//Reads a bus expander, either to get joystick data or see where the program counter is
	
	Wire.beginTransmission(whichDevice);				//Control IO
	Wire.write(0x00);									//Select read register
	Wire.endTransmission();	
	Wire.requestFrom(whichDevice, 1);    				// request 1 byte from device 0x22

	return ~(Wire.read()); 							   // receive a byte as character and return the inverse of it
	
}

void addressControlType(uint8_t whichType) {	//Changes the input/output state of the address bus expanders

	for (int x = 0x20 ; x < 0x22 ; x++) {		
		Wire.beginTransmission(x);				//Low/high byte IO
		Wire.write(0x03);						//Config register	
		Wire.write(whichType);					//Config as OUTPUT
		Wire.endTransmission();	
	}

}

void fastClockControl(uint8_t theStatus) {
	
	TCCR4A = theStatus;
	
}

void setZ80speed(uint8_t whatSpeed) {

	fastClockControl(clockOff);							//Stop the clock while changing speed

	OCR4A = clockTimerA[whatSpeed];						//Setup high speed fast clock to drive Z80
	OCR4C = clockTimerB[whatSpeed];	

	z80speedMultipler = whatSpeed;

	TCNT4 = 0;

	fastClockControl(clockOn);							//Resume clock

	
}

void OLEDsetXY(uint8_t x, uint8_t y) {
	
	Wire.beginTransmission(_i2caddr);
	Wire.write(0x00);
	Wire.write(0xB0 | y);
	Wire.write(0x00 | x & 0x0F);				//Low nibble X pos
	Wire.write(0x10 | x >> 4);					//High nibble X pos	
	Wire.endTransmission();

	//xPos = x;			//Update the values
	//yPos = y;
	
}

void OLEDchar(uint8_t theChar) {

	Wire.beginTransmission(_i2caddr);
	Wire.write(0x40);

	if (textSize) {			//Large text?
		
		if (theChar) {
			theChar -= 13;
		}
		for (uint8_t col = 0 ; col < 8 ; col++) {           //Send the 8 horizontal lines to the OLED
			Wire.write(pgm_read_byte_near(font8x8 + (theChar << 3) + col));             
		}	
		//xPos += 8;
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
		//xPos += 4;

	}

	Wire.endTransmission();

}

void OLEDtextXY(uint8_t x, uint8_t y, const __FlashStringHelper *ifsh) {

	Wire.beginTransmission(_i2caddr);
	Wire.write(0x00);
	Wire.write(0xB0 | y);
	Wire.write(0x00 | x & 0x0F);				//Low nibble X pos
	Wire.write(0x10 | x >> 4);					//High nibble X pos	
	Wire.endTransmission();

	//xPos = x;			//Update the values
	//yPos = y;

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

void OLEDeraseMenu() {

	OLEDsetXY(0, 0);
	OLEDfillLine(13, 0);
	
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
