 //*******************************************************************************************************
//*******************************************************************************************************
//
//      Name:       Cpu.C
//      Purpose:    1802 Processor Emulation
//      Author:     Paul Robson
//      Date:       24th February 2013
//
//*******************************************************************************************************
//*******************************************************************************************************

#include <stdlib.h>
#include "general.h"
#include "cpu.h"
#include "system.h"

#include "macros1802.h"

#define CLOCK_SPEED             (3521280/2)                                         // Clock Frequency (1,760,640Hz)
#define CYCLES_PER_SECOND       (CLOCK_SPEED/8)                                     // There are 8 clocks in each cycle (220,080 Cycles/Second)
#define FRAMES_PER_SECOND       (60)                                                // NTSC Frames Per Second
#define LINES_PER_FRAME         (262)                                               // Lines Per NTSC Frame
#define CYCLES_PER_FRAME        (CYCLES_PER_SECOND/FRAMES_PER_SECOND)               // Cycles per Frame, Complete (3668)
#define CYCLES_PER_LINE         (CYCLES_PER_FRAME/LINES_PER_FRAME)                  // Cycles per Display Line (14)

#define VISIBLE_LINES           (128)                                               // 128 visible lines per frame
#define NON_DISPLAY_LINES       (LINES_PER_FRAME-VISIBLE_LINES)                     // Number of non-display lines per frame. (134)
#define EXEC_CYCLES_PER_FRAME   (NON_DISPLAY_LINES*CYCLES_PER_LINE)                 // Cycles where 1802 not generating video per frame (1876)

// Note: this means that there are 1876*60/2 approximately instructions per second, about 56,280. With an instruction rate of
// approx 8m per second, this means each instruction is limited to 8,000,000 / 56,280 * (128/312.5) about 58 AVR instructions for each
// 1802 instructions.

// State 1 : 1876 cycles till interrupt N1 = 0
// State 2 : 29 cycles with N1 = 1

#define STATE_1_CYCLES          (EXEC_CYCLES_PER_FRAME)
#define STATE_2_CYCLES          (29)

static BYTE8 D,X,P,T;                                                               // 1802 8 bit registers
static BYTE8 DF,IE,Q;                                                               // 1802 1 bit registers
static WORD16 R[16];                                                                // 1802 16 bit registers
static WORD16 _temp;                                                                // Temporary register
static INT16 Cycles;                                                                // Cycles till state switch
static BYTE8 State;                                                                 // Frame position state (NOT 1802 internal state)
static BYTE8 *ramMemory = NULL;                                                     // Pointer to ram Memory
static WORD16 ramMemorySize;                                                        // RAM Memory Size
static BYTE8 *screenMemory = NULL;                                                  // Current Screen Pointer (NULL = off)
static BYTE8 scrollOffset;                                                          // Vertical scroll offset e.g. R0 = $nnXX at 29 cycles
static BYTE8 screenEnabled;                                                         // Screen on (IN 1 on, OUT 1 off)
static BYTE8 keyboardLatch;                                                         // Value stored in Keyboard Select Latch (Cosmac VIP/Studio 2) Keyboard Buffer (Elf 2)
static BYTE8 currentKey;                                                            // Current key pressed (for ELF 2)
static WORD16 ramMask;                                                              // Address Mas for ELF2.

//*******************************************************************************************************
//                          Reset the 1802 and System Handlers
//*******************************************************************************************************

void CPU_Reset(BYTE8 *ramMemoryAddress,WORD16 ramSize)
{
    if (ramMemoryAddress != NULL)                                                   // If RAM not yet allocated
    {
        ramMemorySize = ramSize;                                                    // Remember size
        ramMemory = ramMemoryAddress;                                               // Remember address
        ramMask = 1;                                                                // Calculate the RAM mask.
        while (ramMask < ramMemorySize) ramMask = ramMask << 1;
        ramMask--;                                                                  // From (say) $2000 to $1FFF
    }

    X = P = Q = R[0] = 0;                                                           // Reset 1802 - Clear X,P,Q,R0
    IE = 1;                                                                         // Set IE to 1
    DF = DF & 1;                                                                    // Make DF a valid value as it is 1-bit.

    State = 1;                                                                      // State 1
    Cycles = STATE_1_CYCLES;                                                        // Run this many cycles.
    screenEnabled = FALSE;

    #ifdef IS_COSMACVIP                                                             // On VIP the Monitor ROM is put at $0000 on reset.
    D = 8;                                                                          // Fix up to run Monitor ROM
    R[0] = 0x0008;                                                                  // Avoids implementation of U6A
    P = 2;                                                                          // (see boot.ods)
    R[2] = 0x800A;                                                                  // This is the system status at
    X = 2;                                                                          // $800A
    #endif
}

//*******************************************************************************************************
//                                 Macros to Read/Write memory
//*******************************************************************************************************

#define READ(a)     CPU_ReadMemory(a)
#define WRITE(a,d)  CPU_WriteMemory(a,d)

//*******************************************************************************************************
//   Macros for fetching 1 + 2 BYTE8 operands, Note 2 BYTE8 fetch stores in _temp, 1 BYTE8 returns value
//*******************************************************************************************************

#define FETCH2()    (CPU_ReadMemory(R[P]++))
#define FETCH3()    { _temp = CPU_ReadMemory(R[P]++);_temp = (_temp << 8) | CPU_ReadMemory(R[P]++); }

//*******************************************************************************************************
//                      Macros translating Hardware I/O to hardwareHandler calls
//*******************************************************************************************************

#define READEFLAG(n)    CPU_ReadEFlag(n)
#define UPDATEIO(p,d)   CPU_OutputHandler(p,d)
#define INPUTIO(p)      CPU_InputHandler(p)

static BYTE8 CPU_ReadEFlag(BYTE8 flag)
{
    BYTE8 retVal = 0;
    switch (flag)
    {
        case 1:                                                                     // EF1 detects not in display
            retVal = 1;                                                             // Permanently set to '1' so BN1 in interrupts always fails
            break;
        case 3:                                                                     // EF3 detects keypressed on VIP and Elf but differently.
            #ifdef IS_COSMACVIP
            retVal = SYSTEM_Command(HWC_READKEYBOARD,keyboardLatch);                // Read the keystroke - if down return 1.
            #ifdef COSMAC_BOOTS_MONITOR
            if (R[P] == 0x8024 && keyboardLatch == 0x0C) retVal = 1;                // Fudges the monitor to run whatever you do.
            #endif
            #endif
            #ifdef IS_ELF
            retVal = (currentKey != 0xFF);                                          // ELF : Any key down
            #endif // IS_ELF
            #ifdef IS_STUDIO2
            SYSTEM_Command(HWC_SETKEYPAD,1);
            retVal = SYSTEM_Command(HWC_READKEYBOARD,keyboardLatch);
            #endif
            break;
        case 4:                                                                     // EF4 is !IN Button
            #ifdef IS_ELF
            retVal = (SYSTEM_Command(HWC_READIKEY,0) != 0) ? 0 : 1;                 // Return 0 if I is pressed, 1 otherwise.
            #endif
            #ifdef IS_STUDIO2
            SYSTEM_Command(HWC_SETKEYPAD,2);
            retVal = SYSTEM_Command(HWC_READKEYBOARD,keyboardLatch);
            #endif
            break;
            break;
    }
    return retVal;
}

static BYTE8 CPU_InputHandler(BYTE8 portID)
{
    BYTE8 retVal = 0;
    switch (portID)
    {
        case 1:                                                                     // IN 1 turns the display on.
            screenEnabled = TRUE;
            break;
        case 4:                                                                     // IN 4 reads the keypad latch on the ELF
            #ifdef IS_ELF
            retVal = keyboardLatch;
            #endif // IS_ELF
            break;
    }
    return retVal;
}

static void CPU_OutputHandler(BYTE8 portID,BYTE8 data)
{
    switch (portID)
    {
        case 0:                                                                     // Called with 0 to set Q
            SYSTEM_Command(HWC_UPDATEQ,data);                                       // Update Q Flag via HW Handler
            break;
        case 1:                                                                     // OUT 1 turns the display off
            screenEnabled = FALSE;
            break;
        case 2:                                                                     // OUT 2 sets the keyboard latch (both S2 & VIP)
            #ifdef IS_COSMACVIP
            keyboardLatch = data & 0x0F;                                            // Lower 4 bits only :)
            #endif
            #ifdef IS_STUDIO2
            keyboardLatch = data & 0x0F;                                            // Lower 4 bits only :)
            #endif
            break;
        case 4:                                                                     // OUT 4 sets the LED Display (ELF)
            #ifdef IS_ELF
            SYSTEM_Command(HWC_UPDATELED,data);
            #endif
            break;
    }
}

//*******************************************************************************************************
//                                              Monitor ROM
//*******************************************************************************************************

#ifndef ARDUINO_VERSION                                                             // if not Arduino
#define PROGMEM                                                                     // fix usage of PROGMEM and prog_char
#define prog_uchar BYTE8
#endif // ARDUINO_VERSION

#ifdef IS_COSMACVIP
#include "monitor_rom.h"                                                            // Stock monitor ROM image (almost)
#define MONITOR_SIZE        (512)                                                   // and size
#endif

// Note the stock monitor rom is modified slightly. Location $801B is changed from SMI 4 to SMI 1. The
// function of this change is to allow memory units not in 1k blocks, e.g. the original monitor checks
// $Fxx,$Bxx,$7xx,$3xx whereas this modified one checks $Fxx,$Exx,$Dxx etc. so we can use the roughly
// 1.5k RAM available on a 328 based Arduino.

#ifdef IS_STUDIO2                                                                   // Basic ROM and Games for Studio 2.
#include "studio2_rom.h"
#define MONITOR_SIZE        (2048)
#endif


//*******************************************************************************************************
//                                        Read a BYTE8 in memory
//*******************************************************************************************************

#ifdef IS_COSMACVIP
BYTE8 CPU_ReadMemory(WORD16 address)
{
    if (address < ramMemorySize) return ramMemory[address];
    address -= 0x8000;
    if (address >= 0 && address < MONITOR_SIZE)
    {
        #ifdef ARDUINO_VERSION
        return pgm_read_byte_near(_monitor+address);
        #else
        return _monitor[address];
        #endif // ARDUINO_VERSION
    }
    return 0;
}
#endif

#ifdef IS_ELF
BYTE8 CPU_ReadMemory(WORD16 address)
{
    address &= ramMask;
    if (address < ramMemorySize) return ramMemory[address];
    return 0;
}
#endif

#ifdef IS_STUDIO2
BYTE8 CPU_ReadMemory(WORD16 address)
{
    address &= 0xFFF;
    if (address < 0x800)
    {
        #ifdef ARDUINO_VERSION
        return pgm_read_byte_near(_studio2+address);
        #else
        return _studio2[address];
        #endif // ARDUINO_VERSION
    }
    if (address >= 0x800 && address < 0xA00)
        return ramMemory[address-0x800];
    return 0xFF;
}
#endif

//*******************************************************************************************************
//                                          Write a BYTE8 in memory
//*******************************************************************************************************

#ifdef IS_COSMACVIP
void CPU_WriteMemory(WORD16 address,BYTE8 data)
{
    if (address < ramMemorySize) ramMemory[address] = data;                         // only RAM space is writeable
}
#endif

#ifdef IS_ELF
void CPU_WriteMemory(WORD16 address,BYTE8 data)
{
    address &= ramMask;
    if (address < ramMemorySize) ramMemory[address] = data;                         // only RAM space is writeable
}
#endif

#ifdef IS_STUDIO2
void CPU_WriteMemory(WORD16 address,BYTE8 data)
{
    address = address & 0xFFF;
    if (address >= 0x800 && address < 0xA00) ramMemory[address-0x800] = data;       // only RAM space is writeable
}
#endif

//*******************************************************************************************************
//                                         Execute one instruction
//*******************************************************************************************************

BYTE8 CPU_Execute()
{
    BYTE8 rState = 0;
    BYTE8 opCode = CPU_ReadMemory(R[P]++);
    Cycles -= 2;                                                                    // 2 x 8 clock Cycles - Fetch and Execute.
    switch(opCode)                                                                  // Execute dependent on the Operation Code
    {
        #include "cpu1802.h"
    }
    if (Cycles < 0)                                                                 // Time for a state switch.
    {
        BYTE8 n,newKey;
        switch(State)
        {
        case 1:                                                                     // Main Frame State Ends
            State = 2;                                                              // Switch to Interrupt Preliminary state
            Cycles = STATE_2_CYCLES;                                                // The 29 cycles between INT and DMAOUT.
            if (screenEnabled)                                                      // If screen is on
            {
                if (CPU_ReadMemory(R[P]) == 0) R[P]++;                              // Come out of IDL for Interrupt.
                INTERRUPT();                                                        // if IE != 0 generate an interrupt.
            }
            break;
        case 2:                                                                     // Interrupt preliminary ends.
            State = 1;                                                              // Switch to Main Frame State
            Cycles = STATE_1_CYCLES;
            #ifdef IS_STUDIO2                                                       // Get screen base pointer - this is the page address hence the
            screenMemory = ramMemory+(R[0] & 0xFF00)-0x800;                         // masking with $FF00
            #else
            screenMemory = ramMemory+(R[0] & 0xFF00);                               // After 29 cycles R0 points to screen RAM (std 64x32 assumed)
            #endif
            scrollOffset = R[0] & 0xFF;                                             // Get the scrolling offset (for things like the car game)
            SYSTEM_Command(HWC_FRAMESYNC,0);                                        // Synchronise.
            newKey = 0xFF;                                                          // Update current key pressed.
            for (n = 0;n < 16;n++)
            {
                if (SYSTEM_Command(HWC_READKEYBOARD,n)) newKey = n;
            }
            if (newKey != currentKey)                                               // Has key status changed ?
            {
                currentKey = newKey;                                                // Update current key
                #ifdef IS_ELF
                if (currentKey != 0xFF)                                             // If it is a new key press
                {
                    keyboardLatch = (keyboardLatch << 4) | currentKey;              // Shift into the keyboard latch
                    keyboardLatch = keyboardLatch & 0xFF;                           // which is an 8 bit value.
                }
                #endif
            }
            break;
        }
        rState = (BYTE8)State;                                                      // Return state as state has switched
        Cycles--;                                                                   // Time out when cycles goes -ve so deduct 1.
    }
    return rState;
}

//*******************************************************************************************************
//                                              Access CPU State
//*******************************************************************************************************

#ifdef CPUSTATECODE

CPU1802STATE *CPU_ReadState(CPU1802STATE *s)
{
    int i;
    s->D = D;s->DF = DF;s->X = X;s->P = P;s->T = T;s->IE = IE;s->Q = Q;
    s->Cycles = Cycles;s->State = State;
    for (i = 0;i < 16;i++) s->R[i] = R[i];
    return s;
}

#endif // CPUSTATECODE

//*******************************************************************************************************
//                         Get Current Screen Memory Base Address (ignoring scrolling)
//*******************************************************************************************************

BYTE8 *CPU_GetScreenMemoryAddress()
{
    if (scrollOffset != 0)
    {
        scrollOffset *= 1;
    }
    return (screenEnabled != 0) ? (BYTE8 *)screenMemory : NULL;
}

//*******************************************************************************************************
//                               Get Current Screen Memory Scrolling Offset
//*******************************************************************************************************

BYTE8 CPU_GetScreenScrollOffset()
{
    return scrollOffset;
}

//*******************************************************************************************************
//                                        Get Program Counter value
//*******************************************************************************************************

WORD16 CPU_ReadProgramCounter()
{
    return R[P];
}
