# BinToHexFileCLI version 1.0
# Author : Costas Skordis 
# September 2024
# 
# Convert binary to hexadecimal stream to be sent to the Arduino Cosmac VIP Mega.
#
# Adapted and updated for Python 3.1.2 or newer from Paul Robson (paul@robsons.org.uk) Transmit program - a program for communicating with the arduino 1802 emulation to upload binary files.
#
# Requires pyserial, click, time, and colorama installed
# pyserial is at http://pyserial.sourceforge.net 
#
# Operation: Hold down 'B' (MR) whilst selecting COM port. Data will be loaded from $0000
# 
# python BinToVIPCLI.py
#
#
# Requires Python 3.1.2 or newer

# A generator to divide a sequence into chunks of n units.
def SplitBy(seq, n):
    while seq:
        yield seq[:n]
        seq = seq[n:]

def SendData(data):
    Arduino.write(data)
    time.sleep(0.05)
    if ShowMsg:
        print(data,end=' ')

def SendDataByte(byte):
    data=hex(byte).replace('x','')[-2:].rjust(2,'0').upper()
    hex1=data[:1]
    hex2=data[1:]
 
    SendData(hex1.encode('utf-8'))
    SendData(hex2.encode('utf-8'))

    if ShowMsg:
        hexAddress=hex(address).replace('x','')[-4:].rjust(4,'0').upper()
        print(f'{Fore.MAGENTA}{Style.BRIGHT}\t'+str(hexAddress)+': '+str(hex1)+str(hex2))
 
if __name__ == '__main__':
    import os,sys,click,time,serial
    from serial import SerialException
    import serial.tools.list_ports
    from colorama import Fore, Back, Style, init
        
    os.system('cls')
    init(autoreset=True)
    print(f'{Fore.RED}{Style.BRIGHT}Binary To Serial Hex\n')
    print(f'{Fore.GREEN}{Style.BRIGHT}Communication Settings\n')
    
    # Prompts
    ShowMsg=click.confirm(f'{Fore.MAGENTA}{Style.BRIGHT}\nDo you want to show messages?',default=True,show_default=False,prompt_suffix=' <Y>')
    print('\n')
    
    Ports = serial.tools.list_ports.comports()
    Arduino = serial.Serial()

    portList=[]
    for onePort in Ports:
        portList.append(str(onePort))
        print(f'{Fore.CYAN}{Style.BRIGHT}'+str(onePort))
  
    Arduino.port=click.prompt(f'{Fore.YELLOW}{Style.BRIGHT}\nSelect Port:',default='COM1',hide_input=False,show_choices=False,show_default=False,prompt_suffix=' <COM1> : ')
    Arduino.baudrate=int(click.prompt(f'{Fore.YELLOW}{Style.BRIGHT}Baud Rate:',default='115200',type=click.Choice(['300','600','1200','2400','4800','9600','19200','38400','57600', '115200']),hide_input=False,show_choices=False,show_default=False,prompt_suffix=' <115200> : '))
    Arduino.timeout=0

    try:
      if ShowMsg: print(f'{Fore.CYAN}{Style.BRIGHT}\nOpening Port '+Arduino.port)
      Arduino.open()
      Arduino.reset_output_buffer()
      Arduino.flush()
      if ShowMsg: print(f'{Fore.CYAN}{Style.BRIGHT}Waiting 3 seconds for Arduino to wake up\n')
      time.sleep(3)
    except serial.serialutil.SerialException:
      if ShowMsg: print(f'{Fore.RED}{Style.BRIGHT}\nUnable to open port.'+Arduino.port+'\t\t'f'{Back.RED}{Fore.WHITE}{Style.BRIGHT}Terminating Session\n')
      time.sleep(1)
      sys.exit()
    
    print(f'{Fore.GREEN}{Style.BRIGHT}\nSpecify Files\n')
    
    FileName=[]
    while True:
        SourceDirFile = click.prompt(f'{Fore.YELLOW}Binary Filename', default='',hide_input=False,show_choices=False,show_default=False,prompt_suffix='')
        if os.path.isfile(SourceDirFile):
            FileName.append(SourceDirFile)
        else: 
            break

    StudioII=click.confirm(f'{Fore.WHITE}{Style.BRIGHT}\nIs this for the Studio II?',default=False,show_default=False,prompt_suffix=' <N>')
   
    print(f'{Fore.GREEN}{Style.BRIGHT}\n\nProcessing.......\n')          
    
    checksum=0
    address=0
    if ShowMsg: print(f'{Fore.YELLOW}{Style.BRIGHT}\n\nInitializing process with @\n')
    SendData(b'@')                                                                  # Send start sequence
    Data=[] 
    for SourceFile in FileName:
            if ShowMsg: print(f'{Fore.GREEN}{Style.BRIGHT}\nSending Data From '+SourceFile+'\n')             
            with open(SourceFile,'rb') as f:
                bytes = f.read(8192)												# Read all the bytes in.
                f.close()
                for bits in bytes:													# Send bytes out.
                    checksum = (checksum + bits) % 256
                    SendDataByte(bits) 												# Send data
                    SendData(b'+')                                                  
                    address = address + 1                                           # Increment address
                    if StudioII:                                                    # If Studio II, assuming that the first file is the rom, only load the first 1024 bytes
                        if address==1024:                                        
                            break

    if ShowMsg: print(f'{Fore.YELLOW}{Style.BRIGHT}\n\nSending CheckSum\t'+str(checksum))
    SendData(b'=')                                                                  # Send indicator that the checksum is coming
    SendData(checksum)                                                              # Send check sum    
    if ShowMsg: print(f'{Fore.BLUE}{Style.BRIGHT}\n\nTerminating Session with $')
    SendData(b'$')                                                                  # Send terminate communications
    Arduino.close()
    if ShowMsg: print(f'{Fore.GREEN}{Style.BRIGHT}\nCompleted')




 