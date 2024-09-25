//*******************************************************************************************************
//*******************************************************************************************************
//
//      Name:       System.H
//      Purpose:    System HW Interface Header
//      Author:     Paul Robson
//      Date:       14th March 2013
//
//*******************************************************************************************************
//*******************************************************************************************************

#ifndef _SYSTEM_H
#define _SYSTEM_H

#define HWC_READKEYBOARD        (0)
#define HWC_UPDATEQ             (1)
#define HWC_FRAMESYNC           (2)
#define HWC_READIKEY            (3)
#define HWC_UPDATELED           (4)
#define HWC_SETKEYPAD           (5)

BYTE8 SYSTEM_Command(BYTE8 cmd,BYTE8 param);

#endif // _SYSTEM_H
