#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

// Bluetooth development files option
// It is not necessary to:  apt-get install libbluetooth-dev
// uncomment #define BTDEV if libbluetooth-dev is installed
// and you want to add any of its features to the code

// #define BTDEV

// BEFORE COMPILE
// 1. Put LE board addresses in struct devicedata device[] 
// 2. Put characteristic data in struct cticdata motor[]

// compile instructions
// BTDEV not defined     gcc btle.c -o btle
// BTDEV defined         gcc btle.c -lbluetooth -o btle


#ifdef BTDEV
   // libbluetooth-dev installed
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#else
   // these are the definitions supplied by 
   // the above libbluetooth-dev include files
#define BTPROTO_HCI 1
#define SOL_HCI 0
#define HCI_FILTER 2
#define HCIDEVUP _IOW('H',201,int)
#define HCIDEVDOWN _IOW('H',202,int)

struct sockaddr_hci
  {
  unsigned short hci_family;
  unsigned short hci_dev;      
  unsigned short hci_channel;
  };

struct hci_filter
  {
  unsigned int type_mask;
  unsigned int event_mask[2];
  unsigned short opcode;
  };
#endif

void devinfo(void);
int devconnect(int ndevice);
int devdisconnect(int ndevice);
int setdhandle(int ndevice);
int readreply(int replyflag,int numrep,unsigned char *data,int readflag);
int sendcmd(unsigned char *cmd,int len);
int devsock(void);
int writectic(int ndevice,int cticn,unsigned char *data);
int readctic(int ndevice,int cticn,unsigned char *data);
void hexdump(unsigned char *buf, int len);

     // simple user interface functions
void printhelp(void);
int inputdev(void);
int inputctic(int ndevice);
int inputval(void);
int inputint(void);

// LE characteristics

struct cticdata 
  {
  int size;     // number of bytes  (0=no entry)
  int ack;      // 0=no acknowledge  1=acknowledge
  char *name;   // name of characteristic - your choice
  int chandle;  // characteristic handle 
  };

#define NUMCTICS 8

struct cticdata motor[NUMCTICS] = {
{1,0,"Alert" ,0x000B},   // 1 byte, no acknowledge, handle = 000B
{1,0,"Control",0x000E},  // 1 byte, no acknowledge, handle = 000E
{1,0,"PWM hi",0x0010},   // 1 byte, no ack, 0010
{1,0,"PWM lo",0x0012},   // 1 byte, no ack, 0012 
{2,1,"Test",0x0014},     // 2 byte, acknowledge, handle = 0014
{0,0,NULL,0},            // no entry
{0,0,NULL,0},
{0,0,NULL,0},
};

// LE device info

struct devicedata
  {
  int conflag;                // 0=disconnected 1=connected
                 // index of characteristic info in ctic[] 
  unsigned char dhandle[2];   // LE device handle returned by connect  [0]=lo [1]=hi
  char name[16];              // name of LE device - your choice
  unsigned char baddr[6];     // board address hi byte first
  struct cticdata *ctic;      // characteristic info array
  };

#define NUMDEVICES 4

struct devicedata device[NUMDEVICES] = {
{ 0,{0,0},"LE Device", {0x00,0x1E,0xC0,0x2D,0x17,0x7C},motor }, // board address = 00:1E:C0:2D:17:7C 
{ 0,{0,0},"Device B",{0x22,0x22,0x22,0x22,0x22,0x22},motor },
{ 0,{0,0},"Device C",{0x33,0x33,0x33,0x33,0x33,0x33},motor },
{ 0,{0,0},"Device D",{0x44,0x44,0x44,0x44,0x44,0x44},motor },
};

// GLOBAL CONSTANTS

struct globpar 
  {
  int printflag;       // print BT sends and replies
  int hci;             // PI's BT handle
  int handev;          // command strings set with this device's handle  -1=none
  int replyflag;       // 0=wait for expected number of replies  1=wait for time out                      
  int conrep;          // connect expected replies   
  int timout;          // time out for replies seconds
  };
  
struct globpar gpar;

               // command strings
unsigned char hciopen[32] = {1,0x0D,0x20,0x19,0x60,0,0x60,0,0,0,0x7C,0x17,0x2D,0xC0,0x1E,0,0,0x18,0,0x28,0,0,0,0x2A,0,0,0,0,0}; // len 29
unsigned char hciclose[8] = {1,6,4,3,0x40,0,0x13};  // len 7
unsigned char hciwrite[32] = {2,0x40,0,8,0,4,0,4,0,0x52,0x0B,0,0};  // len 13 if 1 byte
              //  [1][2]=device handle  [9]=opcode  [10][11]=characteristic handle  [12]=data - up to 20 bytes
unsigned char hciread[16] = {2,0x40,0,7,0,3,0,4,0,0x0A,0x12,0};  // len 12   [9]=0A read req [10][11]=handle of characteristic

int main()
  {
  int speed,ndevice,dirn,cticn,cticval;
  unsigned char cmd,cmds[8],dat[8],rdat[32];
  
     // global parameters you choose
  gpar.printflag = 1;   // 0=no traffic prints  1=print bluetooth traffic
  gpar.replyflag = 0;   // 0=wait for specific number of replies  1=time out
  gpar.conrep = 2;     // expected number of replies for connect
  gpar.timout = 1;      // reply wait time out = 1 second

    // global parameters set by code
  gpar.hci = -1;         // hci socket
  gpar.handev = -1;      // handles set for this device number 

  
  if(devsock() == 0)    // open HCI socket gpar.hci 
    {
    printf("Socket open ERROR\n");  
    return(0);
    }
  
  ndevice = 0;  // device index
  cticn = 0;    // characteristic index
  cticval = 0;  // characteristic value

  printhelp();
  
  do
    {
    printf("Input command: ");
    scanf("%s",cmds);
    cmd = cmds[0];
 
       // further inputs
          
    if(cmd == 'c' || cmd == 'd' || cmd == 'w' || cmd == 'r')
      {
      ndevice = inputdev();
      if(cmd == 'w' || cmd == 'r')
        {
        cticn = inputctic(ndevice);
        if(cmd == 'w')
          cticval = inputval();
        }  
      }
      
      // execute command
      
    if(cmd == 'h')            // help
      printhelp();
    else if(cmd == 'c')            // connect device
      devconnect(ndevice);
    else if(cmd == 'd')       // disconnect device
      devdisconnect(ndevice);
    else if(cmd == 'w')       // write characteristic
      {
      if(setdhandle(ndevice) != 0)  // set device handles in command strings
        {
        dat[0] = (unsigned char)(cticval & 0xFF);         // convert to unsigned char array
        dat[1] = (unsigned char)((cticval >> 8) & 0xFF);  // in case 2 byte
        writectic(ndevice,cticn,dat);  
        }   
      }
    else if(cmd == 'r')
      {
      if(setdhandle(ndevice) != 0)
        readctic(ndevice,cticn,rdat);
      }
    else if(cmd == 'i')      // print all device info
      devinfo();
    else if(cmd != 'q')
      printf("Invalid command\n");
    }
  while(cmd != 'q');
  
  close(gpar.hci);  // close HCI socket
  
  return(0);
  }

/*************** PRINT DEVICE INFO ********************/

void devinfo()
  {
  int n,k;
  struct devicedata *tdp;
  struct cticdata *cp;
  
  for(n = 0 ; n < NUMDEVICES ; ++n)
    {
    tdp = &device[n];
    printf("DEVICE %d  %s  Connect=%d\n",n,tdp->name,tdp->conflag);
    printf("  ADDR = ");
    for(k = 0 ; k < 6 ; ++k)
      printf("%02X ",tdp->baddr[k]);
    if(tdp->conflag != 0)
      printf("   Handle=%02X%02X",tdp->dhandle[1],tdp->dhandle[0]);
    printf("\n");
    for(k = 0 ; k < NUMCTICS ; ++k)
      {
      cp = &tdp->ctic[k];
      if(cp->size > 0)
        printf("  %s - Handle %04X Size %d Ack %d\n",cp->name,cp->chandle,cp->size,cp->ack);
      }
    }
  }
  
/***********  CONNECT DEVICE index ndevice *******************/

int devconnect(int ndevice)
  {
  int n,retval;
  struct devicedata *dp;
  
  if(ndevice < 0 || ndevice >= NUMDEVICES)
    {
    printf("Invalid device\n");
    return(0);
    }
    
  dp = &device[ndevice];
  if(dp->conflag != 0)
    {
    printf("Already connected\n");
    return(0);
    }

     // copy board address to command string  low byte first
  for(n = 0 ; n < 6 ; ++n)
    hciopen[n+10] = dp->baddr[5-n];
  
  if(sendcmd(hciopen,29) == 0)
    return(0);
   
   /***** CONNECT REPLIES *****************
   This is set up to exit readreply when it times out:
      readreply(1,  ....second parameter number of expected replies ignored... 
    
   If you see a consistent number of replies (say 2)
   modify replyflag and gpar.conrep to exit on that number:
   
      readreply(0,2,dp->dhandle,1)
        
   This will eliminate the time out wait and will be faster.
  
   When experimenting with commands, call readreply(1,..
   to see all the replies when the number is uncertain  
  **************************************/
    
  retval = readreply(1,gpar.conrep,dp->dhandle,1);  // will read device handle
  if(retval == 0)
    {
    printf("Time out - failed\n",retval);
    return(0);
    }
  if(retval == 1)
    {
    printf("Fail - no handle\n");
    return(0);
    }
  printf("Connect OK\n");
    
  dp->conflag = 1;
  
       
  return(1);
  }

/***********  DISCONNECT *******************/

int devdisconnect(int ndevice)
  {
  int n;
  struct devicedata *dp;
  
  dp = &device[ndevice];
  if(dp->conflag == 0)
    {
    printf("Already disconnected\n");
    return(0);
    }
    
  if(setdhandle(ndevice) == 0)   
    return(0);                  
    
  if(sendcmd(hciclose,7) == 0)
    return(0);
    
  if(readreply(gpar.replyflag,2,NULL,0) == 0)  
    return(0);

  dp->conflag = 0;
  gpar.handev = -1;    

  printf("Disconnected\n");       
  return(1);
  }


/************* SET DEVICE HANDLE in command strings *********/

int setdhandle(int ndevice)
  {
  struct devicedata *dp;
 
  if(gpar.handev == ndevice)
    return(1);   // device handles already set
  
  if(ndevice < 0 || ndevice >= NUMDEVICES)
    {
    printf("Invalid device\n");
    return(0);
    }
    
  dp = &device[ndevice];
  if(dp->conflag == 0)
    {
    printf("Not connected\n");
    return(0);
    }
     
  hciwrite[1] = dp->dhandle[0];
  hciwrite[2] = dp->dhandle[1];

  hciread[1] = dp->dhandle[0];
  hciread[2] = dp->dhandle[1];
  
  hciclose[4] = dp->dhandle[0];
  hciclose[5] = dp->dhandle[1];

  gpar.handev = ndevice;   // device handles set for this device number
  return(1);
  }
  
/***********  WRITE CHARACTERISTIC *****************
must set up hciwrite first with device handle via setdhandle(ndevice)
ndevice = device index in device[ndevice]
cticn = characteristic index in device[ndevice].ctic[cticn]
data = array of bytes to write - low byte first 
*****************/

int writectic(int ndevice,int cticn,unsigned char *data)
  {
  struct cticdata *cp;  // characteristic info structure
  int n,chandle,size,ack;
    
  cp = &device[ndevice].ctic[cticn];
  chandle = cp->chandle;  // characteristic handle
  size = cp->size;        // number of bytes
  ack = cp->ack;          // acknowledge        
           
                          // set characteristic handle
  hciwrite[10] = (unsigned char)(chandle & 0xFF);
  hciwrite[11] = (unsigned char)((chandle >> 8) & 0xFF);
  for(n = 0 ; n < size ; ++n)     // set data
    hciwrite[12+n] = data[n];     // low byte first

  hciwrite[3] = size+7;
  hciwrite[5] = size+3;
  
  if(ack == 0)           // no acknowledge 
    hciwrite[9] = 0x52;  // write command opcode
  else                   // acknowledge
    hciwrite[9] = 0x12;  // write request opcode

  printf("Write %s %s =",device[ndevice].name,cp->name);    
  for(n = 0 ; n < size ; ++n)
    printf(" %02X",hciwrite[n+12]);
  printf("\n");
       
  if(sendcmd(hciwrite,12+size) == 0)   // send 13 for 1 byte
    return(0);
    
  if(readreply(gpar.replyflag,1+ack,NULL,0) == 0)    // 2 replies if acknowledge  
    return(0);   
 
  return(1);
  }


/***********  READ CHARACTERISTIC *****************
ndevice = device index in device[ndevice]
cticn = characteristic index in device[ndevice].ctic[cticn]
data = array of bytes to receive read - low byte first 
*****************/

int readctic(int ndevice,int cticn,unsigned char *data)
  {
  struct cticdata *cp;  // characteristic info structure
  int n,chandle,size,ack;
  unsigned char dat[32];
 
  if(setdhandle(ndevice) == 0)
    return(0);
    
  cp = &device[ndevice].ctic[cticn];
  chandle = cp->chandle;  // characteristic handle
  size = cp->size;        // number of bytes      
           
                          // set characteristic handle
  hciread[10] = (unsigned char)(chandle & 0xFF);
  hciread[11] = (unsigned char)((chandle >> 8) & 0xFF);
  
  for(n = 0 ; n < size ; ++n)
    data[n] = 0;   // in case read fails
         
  if(sendcmd(hciread,12) == 0)   
    return(0);
    
  if(readreply(gpar.replyflag,2,data,2) != 3)   // 2 replies    
    {
    printf("Failed to read %s %s\n",device[ndevice].name,cp->name);   
    return(0);   
    }

  printf("Read %s %s =",device[ndevice].name,cp->name);    
  for(n = size-1 ; n >= 0 ; --n)  // print hi byte first
    printf(" %02X",data[n]);
  printf("\n");
 
  return(1);
  }




  
/*************** SEND COMMAND *********
cmd = string to send
len = number of bytes to send
**************************************/

int sendcmd(unsigned char *cmd,int len)
  {
  int nwrit,ntogo;
  unsigned char *s;
  time_t tim0;
   
     
  s = cmd;  
  
  if(gpar.printflag != 0)
    {
    printf("< CMD");
    if(cmd[0] == 2)
      printf(": Opcode %02X\n",cmd[9]);
    else if(cmd[0] == 1)
      printf(": OGF=%02X OCF=%02X\n",(cmd[2] >> 2) & 0x3F,((cmd[2] << 8) & 0x300) + cmd[1]);
    hexdump(s,len); 
    }
    
  ntogo = len;

  tim0 = time(NULL);
  do
    {
    nwrit = write(gpar.hci,s,ntogo);
    if(nwrit > 0)
      {
      ntogo -= nwrit;
      s += nwrit;
      }  
    if(time(NULL) - tim0 > 3)
      {
      printf("Send CMD timeout\n");
      return(0);
      }
    }
  while(ntogo != 0);

  return(1);
  }


/************** READ REPLY ***********************
replyflag = 0  wait for expected number of replies
            1  wait for time out to see all replies when number unknown 
numrep = expected number of replies for replyflag=0 

data = destination for 2 byte device handle from connect reply (readflag=1)
       destination for characteristic value if read (readflag=2)
       NULL if readflag=0     
       
readflag = 0   nothing to read from replies
           1   read device handle from connect reply
           2   read characteristic from read reply 
           3   read scan LE advertising report reply
           4   info request reply                    
return 1=OK   2=connect OK, got device handle in data 
       3=got characteristic value in data
       0=time out when waiting for reply count
****************************/

int readreply(int replyflag,int numrep,unsigned char *data,int readflag)
  {
  unsigned char b0,buf[512];   
  int len,blen,wantlen,retval;
  int n,k,n0;
  time_t tim0;

        
  tim0 = time(NULL);
  n = 0;   // number of reply
   
  blen = 0;         // existing buffer length
  wantlen = 8192;   // expected messaage length - new message flag 
  retval = 1;       // non-connect OK return   
   
  do   
    {
    // next message may loop with data in buf
    do       // wait for complete message
      { 
      if(blen != 0 && wantlen == 8192)   // find expected messaage length
        {
        b0 = buf[0];
        if(!(b0==1 || b0==2 || b0==4))
          {   
          printf("Unknown packet type\n");
          // clear reads and exit
          do
            {
            len = read(gpar.hci,buf,sizeof(buf));
            }
          while(time(NULL) - tim0 <= 3);
          return(0);
          }
        if(b0 == 1 && blen > 3)
          wantlen = buf[3] + 4;
        else if(b0 == 2 && blen > 4)
          wantlen = buf[3] + (buf[4] << 8) + 5;
        else if(b0 == 4 && blen > 2)
          wantlen = buf[2] + 3;
        }
       
      if(blen < wantlen)   // need more        
        {   
        do       // read block of data - may be less than or more than one line
          {
          len = read(gpar.hci,&buf[blen],sizeof(buf)-blen);
        
          if(time(NULL) - tim0 > gpar.timout+1)
            {                                   // timed out
            if(replyflag == 0 && retval != 2)   // counting replies and not connecting 
              {
              printf("Timed out waiting for %d replies\n",numrep);
              return(0);       // time out is error
              }
            if(retval == 2)
              printf("Seen %d connect replies - see notes in devconnect()\n",n);
            return(retval);   //  time out exit - OK return
            }
          }
        while(len < 0);
        blen += len;  // new length of buffer
        }                 
      }    
    while(blen < wantlen);
       
               // got a complete message length = wantlen 
               // buffer length blen may be larger
               
    if(readflag == 1 && data != NULL)   // reply from connect - looking for device handle 
      {
      if(buf[0] == 4 && buf[1] == 0x3E && buf[3] == 1 && buf[4] == 0)   // event 3E subevent 1  status 0
        {   // Vol 2 Part E 7.7.65
            // connect OK
        data[0] = buf[5];  // read returned device handle
        data[1] = buf[6];
        retval = 2;       // connect OK return even if missed expected reply count
        }
      }  // end connect


    if(readflag == 3 && data != NULL)   // reply from scan - looking for board address 
      {
      if(buf[0] == 4 && buf[1] == 0x3E && buf[3] == 2)   // event 3E subevent 2 
        {   // Vol 2 Part E 7.7.65.2
        n0 = data[0]*6 + 1;
        for(k = 0 ; k < 6 ; ++k)
          data[n0+k] = buf[k+7];  // read returned board address
        ++data[0];  // count of addresses
        }
      }  // end connect


    if(readflag == 2 && data != NULL)   // reply from read - looking for characteristic value 
      {
      if(buf[0] == 2 && buf[9] == 0x0B)   // read reply 
        {
        if(buf[5] == 1)
          printf("No data returned - characteristic not set in BLE device\n");
        else
          {         
          for(k = 0 ; k < buf[5] - 1 && k < 20 ; ++k)   // number of bytes=buf[5]-1  max 20
            data[k] = buf[10+k];  // read returned bytes
          retval = 3;             // characteristic value in data
          }
        }
      }  // end connect


    if(readflag == 4 && data != NULL)   // reply from info req - looking for characteristics 
      {
      if(buf[0] == 2 && buf[9] == 0x05)   // req info reply 
        {
        data[1] = buf[10];   // 1=2byte 2=16 byte    UUID
        if(buf[10] == 2)
          k = 18;   // 16 byte UUID
        else
          k = 4;    // 2 byte UUID
        n0 = buf[5] + (buf[6] << 8) - 2;  // length of data
        data[0] = n0/k;  // number of handle/uuid pairs         
        for(k = 0 ; k < n0 && k < 126 ; ++k)   // number of bytes=buf[5]-1  max 20
          data[k+2] = buf[11+k];  // read returned bytes
        }
      }  // end connect

      
    ++n;  // reply count  
         
    if(gpar.printflag != 0)
      { 
      b0 = buf[0];
      if(b0 == 4)
        {
        printf("%d > Event: %02X\n",n,buf[1]);
        if(buf[1] == 0x0E && buf[2] == 4 && buf[3] == 1 && buf[6] != 0)
          printf("**** ERROR return **** CMD %02X %02X\n",buf[4],buf[5]);
        }
      else if(b0 == 2)
        printf("%d > ACL Opcode %02X\n",n,buf[9]);
      else
        printf("%d > Unknown\n",n);
      hexdump(buf,wantlen);
      }
    
    if(blen == wantlen)
      {    // have got exact message length
      blen = 0;
      }
    else
      {   // have got part of next message as well
          // wipe last message length wantlen - copy next down
          // starts at buf[wantlen] ends at buf[blen-1]
          // copy to buf[0]             
      for(k = wantlen ; k < blen ; ++k)
        buf[k-wantlen] = buf[k];       
      blen -= wantlen;
      }

    wantlen = 8192;  // new message flag 

    }  // next message
  while(n < numrep || replyflag != 0 || blen != 0);
   
  return(retval);
  } 


/************** OPEN HCI SOCKET ******
channel  0=channel 0 - HCI sends extra commands
         1=user channel - no extra commands
          
return 0=fail
       1= OK and sets gpar.hci= socket handle
*************************************/       

int devsock()
  {
  int dd;
  struct sockaddr_hci sa;
  struct hci_filter flt;

  
         // AF_BLUETOOTH=31
  dd = socket(31, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_HCI);

  if(dd < 0)
    {
    printf("Socket open error\n");
    return(0);
    }

  flt.type_mask = 0x14;  
  flt.event_mask[0] = 0xFFFFFFFF;
  flt.event_mask[1] = 0xFFFFFFFF;
  flt.opcode = 0;
  /** same as ****
  hci_filter_clear(&flt);
  hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
  hci_filter_set_ptype(HCI_ACLDATA_PKT, &flt);
  hci_filter_all_events(&flt);
  ***************/
        
  if(setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0)
    {
    printf("HCI filter setup failed\n");
    close(dd);
    return(0);
    }   


        // same as hciconfig hci0 down
        // must be down to set channel=HCI_CHANNEL_USER   
  if(ioctl(dd,HCIDEVDOWN,0) < 0)   // 0=hci0
    {
    if(errno != EALREADY)
      {
      printf("hci0 not down\n");
      close(dd);
      return(0);
      }
    }
   
  sa.hci_family = 31;   // AF_BLUETOOTH;
  sa.hci_dev = 0;       // hci0
  sa.hci_channel = 1;   // HCI_CHANNEL_USER    
  if(bind(dd,(struct sockaddr *)&sa,sizeof(sa)) < 0)
    {
    printf("Bind failed\n");
    close(dd);
    return(0);
    }
      
  gpar.hci = dd;       
          
  printf("HCI Socket OK\n");
   
  return(1);
  }  


/************** OPEN HCI SOCKET ******
return 0=fail
       1= OK and sets gpar.hci= socket handle
*************************************/       

int devsockx()
  {
  int dd;
  struct sockaddr_hci sa;
  struct hci_filter flt;

         // AF_BLUETOOTH=31
  dd = socket(31, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_HCI);

  if(dd < 0)
    {
    printf("Socket open error\n");
    return(0);
    }
     
          //  same as hciconfig hci0 up  
  if(ioctl(dd,HCIDEVUP,0) < 0)   // 0=hci0
    {
    if(errno != EALREADY)
      {
      printf("hci0 not up\n");
      close(dd);
      return(0);
      }
    }
   
  sa.hci_family = 31;   // AF_BLUETOOTH;
  sa.hci_dev = 0;
  sa.hci_channel = 0;    
  if(bind(dd,(struct sockaddr *)&sa,sizeof(sa)) < 0)
    {
    printf("Bind failed\n");
    close(dd);
    return(0);
    }
      
  flt.type_mask = 0x14;  
  flt.event_mask[0] = 0xFFFFFFFF;
  flt.event_mask[1] = 0xFFFFFFFF;
  flt.opcode = 0;
  /** same as ****
  hci_filter_clear(&flt);
  hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
  hci_filter_set_ptype(HCI_ACLDATA_PKT, &flt);
  hci_filter_all_events(&flt);
  ***************/
        
  if(setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0)
    {
    printf("HCI filter setup failed\n");
    close(dd);
    return(0);
    }   
 
  gpar.hci = dd;    
       
  printf("HCI Socket OK\n");
   
  return(1);
  }  

/******** HEX DUMP - print buf in hex format, length = len ********/

void hexdump(unsigned char *buf, int len)
  {
  int i,i0,n;

  if(len <= 0)
    {
    printf("No data\n");
    return;
    }
     
  i = 0;
  do
    {
    i0 = i;
    n = 0;
    printf("  %04X  ",i0);
    do
      {
      if(n == 8)
        printf("- ");
      printf("%02X ",buf[i]);
      ++n;
      ++i;
      }
    while(n < 16 && i < len);
    printf("\n");
    }
  while(i < len);  
  }

/*************** USER INTERFACE *******************/

void printhelp()
  {
  printf("  h = print this help\n");
  printf("  i = print all device info\n");
  printf("  c = connect a device\n");
  printf("  w = write characteristic\n");
  printf("  r = read characteristic\n");
  printf("  d = disconnect a device\n");
  printf("  q = quit\n");
  } 

int inputdev()
  {
  int n;
     
  for(n = 0 ; n < NUMDEVICES ; ++n)
    printf("  DEVICE %d = %s\n",n,device[n].name);
  do
    {
    printf("Enter device number 0-%d: ",NUMDEVICES-1);
    n = inputint();   
    }
  while(n < 0 || n >= NUMDEVICES);

  return(n);
  }
  
int inputctic(int ndevice)
  {
  int n,nx;
    
  for(n = 0 ; n < NUMCTICS && device[ndevice].ctic[n].size > 0 ; ++n)
    {
    nx = n;
    printf("  CHARACTERISTIC %d = %s\n",n,device[ndevice].ctic[n].name);
    }
  do
    {
    printf("Enter characteristic number 0-%d :",nx);
    n = inputint();
    }
  while(n < 0 || n > nx);
  return(n);
  } 
  
int inputval()
  {
  int n;
  
  do
    {
    printf("Enter value 0-255: ");
    n = inputint();
    }
  while(n < 0 || n > 255);
  return(n);
  }  
  
  
int inputint()
  {
  int n,flag;
  char s[64];
 
  do
    { 
    scanf("%s",s);
    flag = 0;
    for(n = 0 ; s[n] != 0 ; ++n)
      {
      if(s[n] < '0' || s[n] > '9')
        flag = 1;
      }
    if(flag == 0)
      n = atoi(s);
    }
  while(flag != 0);
  return(n);
  } 
