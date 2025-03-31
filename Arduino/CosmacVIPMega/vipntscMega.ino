// *****************************************************************************************************************
// *****************************************************************************************************************
//
//										Arduino version of VIP/Elf/Studio 2
//										===================================
//
//										 Written by Paul Robson March 2013
//                     Modified by Walter Miraglia
//                     Modified by Costas Skordis March 2025
//                     Added 10 preloaded games accessed by Reset followed by the key number
//                     
//                     Key    Name
//                     0      UFO
//                     1      Sum Fun
//                     2      Brix
//                     3      Slide
//                     4      Animal Race
//                     5      Bowling
//                     6      Sword Fighter
//                     7      Dog Fight
//                     8      Chip 8
//                     9      Round Up
//                     A      Androids
//                     D      Sequence
//                     E      Lander
//                     F      Space
//
//	Note:	this requires the TVout1802 library which is a significantly modified version of "TVout"
//
// *****************************************************************************************************************
// *****************************************************************************************************************

#include "cpu.h"							
#include <TVout1802.h>
#include <avr/pgmspace.h>
#include "binary_data.h"
#include "cpu.c"					  	 // The 1802 CPU / Hardware code

#define ARDUINO_VERSION        // Conditional Compile (read pgm mem etc.)
#define QPIN		    53 				 // I/O Pin for Q (stable line not beeper)
#define TONEPIN	  	10			   // Same pin as in TV1802/spec/hardware_setup.h
#define ELF_IN_PIN	12		     // Elf input line (switch to ground) not used.

#define PITCH1        200			   // Fail Tone.

#ifdef IS_STUDIO2						   // Set the pitch for S2/VIP.
#define PITCH0       400			   // Elf should use the other line.
#else
#define PITCH0       2945
#endif

//
//	The beeper bin is determined by the TVout1802 file spec/hardware_setup.h - if you look here and go down to the "2560"
//	section you'll see this is set to PortB4 (OCA2) (10). http://arduino.cc/en/Hacking/PinMapping168
//
//	The keypad connections are determined by rowpins and colpins further down. Using a stock keypad (123A/456B) that is commonly 
//	available. The keypad is connected to PortC.  The columns are the first four pins holding it upright, the rows the last four hence C1,C2,C3,C4,R1,R2,R3,R4 (37,36,35,34,33,32,31,30)
//
//	PortA7 (29) is used for VideoOut by TVout1802 which is why the keyboard isn't connected to pins 0-7.
//	PortB5 (OCA1) (11) is used for the VideoSync.
//
//	Pin 11 must be used (OCR1A). The Video signal can use any Pin 7 when fast mode is enabled. 
//	
//      Pin 

//  Computer's RAM Memory which is up to 1.5k (328) and 6.5k (2650). The Cosmac VIP monitor is modified so it allows RAM
//  in steps of 256 bytes not 1k. The Elf II masks the address upper bits as far as it can. The Studio 2 only has 512 bytes
//  of memory so it doesn't care.
//
//	The __attribute__ .... bit stops it being cleared on a hard reset. At power up it is filled with random bytes.

unsigned char vipMemory[0x1900]  __attribute__ ((section(".noinit")));	                                        // set to 0x1900 which is just over 6k for arduino mega			
																					
TVout1802 TV;													// TVOut object modified for 64x32 display.

// *****************************************************************************************************************
//									Load code saved in progMem into ram memory
// *****************************************************************************************************************

static void RAMUpload(const unsigned char *codePointer,int size,int address)
{
  while (size-- > 0) 											                  // While more to copy
	{
		vipMemory[address] = pgm_read_byte_near(codePointer);	 // Copy into RAM space.
		codePointer++;
		address++;
	}
}

// *****************************************************************************************************************
//										          Keypad configuration
// *****************************************************************************************************************

const byte rows = 4; 												// four rows
const byte cols = 4; 												// three columns

byte keyLoc[16][2] = {  { 3,1 }, { 0,0 }, { 0,1 }, { 0,2 },							// Keypad locations of 0-9A-F
		        { 1,0 }, { 1,1 }, { 1,2 }, { 2,0 },
		        { 2,1 }, { 2,2 }, { 3,0 }, { 3,2 },
			{ 0,3 }, { 1,3 }, { 2,3 }, { 3,3 } };

byte isPressed[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };							// Flags for each key pressed.

byte rowPins[rows] = { 33,32,31,30 }; 										// connect to the row pinouts of the keypad Port C
byte colPins[cols] = { 37,36,35,34 }; 										// connect to the column pinouts of the keypad Port C

// *****************************************************************************************************************
//										  Check if all keys are pressed
// *****************************************************************************************************************

void CheckAllKeys() 
{
	for (byte r = 0; r < rows; r++) pinMode(rowPins[r],INPUT_PULLUP);		// Set all rows to Input/Pullup
	for (byte key = 0;key < 16;key++)									                  // Scan all keys.
	{
		byte c;
		for (c = 0; c < cols;c++) pinMode(colPins[c],INPUT);						  // Input on all Columns
		c = keyLoc[key][1];										                            // Get the column to check
		pinMode(colPins[c],OUTPUT);
		digitalWrite(colPins[c], LOW);									                  // Make it low (as inputs pulled up)
		isPressed[key] = !digitalRead(rowPins[keyLoc[key][0]]);						// Key is pressed if Row 0 is low.
		digitalWrite(colPins[c],HIGH);									                  // Set it back to high
	}
}

// *****************************************************************************************************************
//												Hardware Interface
// *****************************************************************************************************************

static BYTE8 toneState = 0;											// Tone currently on/off ?
static BYTE8 selectedKeypad = 1;										// Currently selected keypad

BYTE8 SYSTEM_Command(BYTE8 cmd,BYTE8 param)
{
	BYTE8 retVal = 0;
	switch(cmd)
	{
		case  HWC_READKEYBOARD:										      // Read the keypad 
			retVal = isPressed[param & 0x0F];							// I only have one keypad so shared for S2
			break;    

		case  HWC_UPDATEQ:             
			digitalWrite(QPIN,(param != 0) ? HIGH : LOW);						// Update the Q Pin
			if (param != toneState) 								// Has it changed state ?
			{
				toneState = param;								    // If so save new state and set the beeper accordingly.
				if (toneState != 0) {
          tone(TONEPIN,PITCH1);delay(200);noTone(TONEPIN);	
        }
        else {noTone(TONEPIN);}
			} 											       // 2,945 Hz is from CCT Diagram values on
			break;    										// NE555V in RCA Cosmac VIP.

		case  HWC_FRAMESYNC:
			setDisplayPointer(CPU_GetScreenMemoryAddress());					// Set the display pointer to "whatever it is"
			setScrollOffset(CPU_GetScreenScrollOffset());						// Set the current scrolling offset.
			CheckAllKeys();										// Rescan the keyboard.
			break;    

		case HWC_READIKEY:										// IN key is inverse of pin because
			retVal = (digitalRead(ELF_IN_PIN) == LOW);						// its a pull  up, switch to ground.

			break;

		case HWC_UPDATELED:										// If the 8 Leds / 2x7 segments are ever
			break;											// implemented code goes here.

		case HWC_SETKEYPAD:										// Studio 2 has two keypads, this chooses the
			selectedKeypad = param;									// currently selected one (for HWC_READKEYBOARD)
			break;											// Not fully implemented here :)
	}
	return retVal;
}

// *****************************************************************************************************************
//											  Very simple Uploader
// *****************************************************************************************************************

void UploadData()
{
	byte current = 0;												// Current read byte
	byte checksum = 0;											// Checksum value.
	int address = 0;												// Current write address
	byte ch = ' ';													// Current character.
  while (ch != '$')												// Keep going until finished.
	{
		if (Serial.available() > 0)				  	// If a byte available.
		{
 		  digitalWrite(QPIN, HIGH);
      ch=Serial.read();
      
      if (ch >= '0' && ch <= '9') current = (current << 4) | (ch - '0');		  // Handle 0-9
			if (ch >= 'A' && ch <= 'F') current = (current << 4) | (ch - 'A' + 10);	// Handle A-F
			
      if (ch == '@') checksum = 0;				      // Handle @ (zero checksum)        
      //if (ch == '~') {RAMUpload(chip8,512,0);}  // Handle ~ load chip8
      if (ch == '+')											     // Handle + (write byte)
			{
				checksum += current;
				vipMemory[address++] = current;		
			}
			if (ch == '=' && current != checksum)								// Handle failed = i.e. bad checksum
			{
		  	tone(TONEPIN,200);delay(1200);noTone(TONEPIN);	  // Higher fail tone.
				while (1) {}									                    // And stop.
			}
      digitalWrite(QPIN, LOW);
    }
	}
}
//	The errr...... protocol. 
//
//	0-9A-F	Shift into LSB of current byte.
//	+		Write to memory, add to checksum, bump pointer.
//	@		Zero checksum.
//	=		Test checksum = current byte, fail if wrong.
//	$		End Upload
//  #   Copy CHIP8 first
// *****************************************************************************************************************
//									Generated PROGMEM data for uploading into RAM
// *****************************************************************************************************************



void setup() 
{
	CheckAllKeys();													      // Copy state of keys into isPressed[]
   	pinMode(QPIN,OUTPUT);												// Set Q Pin to output.
  	pinMode(ELF_IN_PIN,INPUT_PULLUP);						// IN button pin to input with pull up on


    if (isPressed[0]) 
    {
      RAMUpload(ufo,736,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);								 
    }

  	if (isPressed[1]) 
    {
      RAMUpload(sumfun,688,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);								 
    }
    if (isPressed[2]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(brix,280,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);	
    }
    if (isPressed[3]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(slide,388,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[4]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(animal,1536,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[5]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(bowling,1536,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[6]) 
    {
      RAMUpload(sword,2560,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[7]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(dogfight,927,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    } 
    if (isPressed[8]) 
    {
      RAMUpload(chip8,512,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[9]) 
    {
      RAMUpload(roundup,2559,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[10]) 
    {
      RAMUpload(androids,2048,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[11]) 										      // Is the 'B (TR)' key (bottom right) pressed ?
  	{ 														              // (on a standard 4x4 it will actually be "D"!)
  		tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);								// Short low tone : data connected
      Serial.begin(9600);                      // Start serial I/O
      Serial.setTimeout(1);
      delay(1000);      									
		  setDisplayPointer(vipMemory);												
		  UploadData();												    // Call the uploader.
		  Serial.end();  											    // End serial I/O
		  CheckAllKeys();											    // Recheck all keys for C-Boot e.g. VIP Monitor
  	}
    if (isPressed[13]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(sequence,324,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
    if (isPressed[14]) 
    {
      RAMUpload(lander,2304,0);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }
  if (isPressed[15]) 
    {
      RAMUpload(chip8,512,0);
      RAMUpload(spaceinv,1020,512);
      tone(TONEPIN,PITCH0);delay(200);noTone(TONEPIN);
    }

  	TV.begin(NTSC,8,2);								  	   // Initialise TVout (8,2) smallest working size
  	CPU_Reset(vipMemory,sizeof(vipMemory));	 // Initialise CPU
}

// *****************************************************************************************************************
//															Main loop
// *****************************************************************************************************************

void loop() 
{
  	CPU_Execute();
}
