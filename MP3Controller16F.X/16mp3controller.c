#include <htc.h>
#include <stdio.h>
#include <pic16f877a.h>
#define _XTAL_FREQ 20000000
__CONFIG(WDTE_OFF & FOSC_HS & CP_OFF & WRT_OFF);

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

#define IR_VOL_PLUS 0b1001000001101111
#define IR_VOL_MINUS 0b1010100001010111
#define IR_OFF 0b1010001001011101
//#define IR_UP__EQ 0b1110000000011111
#define IR_UP__NEXT_FOLDER 0b1100001000111101
#define IR_DOWN__PREV_FOLDER 0b0000001011111101
#define IR_DOWN__RPT 0b1001100001100111
#define IR_MUTE 0b1110001000011101
#define IR_R 0b1100001000111101
#define IR_L 0b0000001011111101
#define IR_SRC__MODE 0b0110001010011101
#define IR_OK__PLAY_PAUSE 0b0010001011011101


int bp_ir_cmd;
int ir_loop = 0;
unsigned char irWaitTimes[74] =  {180, /*prepare*/ 88, 11, /*begin*/ 11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,33,11,33,11,33,11,33,11,33,11,33,11,33,11,33,11,/*cmd*/11,11,11,11,33,11,11,11,11,11,11,11,33,11,11,11, 33,11,33,11,11,11,33,11,33,11,33,11,11,11,33,11, 200, 200, 200, 200, 180, 44, 11};
unsigned char irWaitStates[74] = {  0, /*prepare*/  1,  0, /*begin*/  1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,/*cmd*/ 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,  1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,   1,   1,   1,   1,   0,  1,  0};
unsigned char irWaitStatesR[74]= {  1, /*prepare*/  1,  1, /*begin*/  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/*cmd*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,   1,   1,   1,   1,   0,  1,  0};

int repeat = 0;
int timer2Clicks = 0;
int timer2Idx    = 0;

void interrupt isr(void)
{
    if ((T0IE) && (T0IF))
    {
       T0IF = 0;
    }

    if ((TMR2IE) && (TMR2IF))
    {
       timer2Clicks++;
       //PORTDbits.RD1 = !PORTDbits.RD1;
       if(repeat == 1)
       {
           PORTDbits.RD1 = !irWaitStatesR[timer2Idx];
       }
       else
       {
           PORTDbits.RD1 = !irWaitStates[timer2Idx];
       }

       if(timer2Clicks >= irWaitTimes[timer2Idx])
       {
           timer2Idx++;
           timer2Clicks = 0;
       }

       if(timer2Idx >= 74)
       {
           //printf("0");
           PORTDbits.RD1 = !1;
           timer2Idx = 0;
           timer2Clicks = 0;

           ir_loop--;
           if(ir_loop > 0)
           {
               //printf("1");
               repeat = 1;
           }
           else if(ir_loop <= 0)
           {
               //printf("2");
               repeat = 0;
               TMR2ON = 0;
           }
           else
           {
               //printf("3");
               TMR2ON = 0;
           }
       }

       TMR2IF = 0;
    }
}

void putch(unsigned char byte)
{
    while (TXIF == 0); // spin until done transmitting
    TXREG = byte;
}

// BAUDRG is calculated as  = Fosc / (16 * Desired Baudrate) - 1
// So, 4MHz / (16 * 19200) - 1 = 12 (approx.)
//#define BAUDRG 12           // 115.2Kbps from 24MHz or 19.2Kbps from 4MHz (BRG16 = 0)
// !!! For 20MHZ -> [ 20 000 000 / (16 * 115200) ] - 1 = 9.85 ~ 10
// !!! For 20MHZ -> [ 20 000 000 / (16 * 19200) ] - 1 = 9.85 ~ 10
#define BAUDRG 64           // 19200bps from 19.6608MHz (BRG16 = 0)

bit enteredLowState = 0;
int prevTMR = 0;

int isInside(int lowLimit, int hiLimit, int width)
{
    if (lowLimit < width && width < hiLimit)
    {
       return 1;
    }

    return 0;
}


void pushCommand(int command)
{
    if(TMR2ON == 0)
    {
       int idxMain = 35;
       int idx = 15;
       for(idx;idx >= 0; idx--)
       {
           if(CHECK_BIT(command, idx))
           {
               //printf("1");
               irWaitTimes[idxMain] = 33;
           }
           else
           {
               //printf("0");
               irWaitTimes[idxMain] = 11;
           }
           idxMain = idxMain + 2;
       }
       TMR2ON = 1;
       TMR2 = 0;
    }
}

int fillRadioStations = 0;
void handlePulse(int pulseWidth)
{
    //printf("+%d \r\n", pulseWidth);
    //set command - this is the end
    //rel   2000 us - 1250 => 1187 - 1313 RES_CMD_VOL_UP
    //V+    2600 us - 1625 => 1562 - 1688 RES_CMD_VOL_UP
    //V-    3200 us - 2000 => 1937 - 2063 RES_CMD_VOL_DOWN
    //UP    3800 us - 2375 => 2312 - 2438 RES_CMD_ALBUM_UP
    //DOWN  4400 us - 2750 => 2687 - 2813 RES_CMD_ALBUM_DOWN
    //MUTE  5000 us - 3125 => 3062 - 3188 RES_CMD_ATT
    //RIGHT 5600 us - 3500 => 3437 - 3563 RES_CMD_SEEK_UP
    //LEFT  6200 us - 3875 => 3812 - 3938 RES_CMD_SEEK_DOWN
    //SRC   6800 us - 4250 => 4187 - 4313 RES_CMD_SRC
    //OK    7400 us - 4625 => 4562 - 4688 RES_CMD_SEL
    //SRC+  8000 us - 5000 => 4937 - 5063 RES_CMD_OFF (was mode)

    if (isInside(1562, 1688, pulseWidth))
    {
       bp_ir_cmd = IR_VOL_PLUS;
       pushCommand(IR_VOL_PLUS);//V+
    }
    else if (isInside(1937, 2063, pulseWidth))
    {
       bp_ir_cmd = IR_VOL_MINUS;
       pushCommand(IR_VOL_MINUS);//V-
    }
    else if (isInside(2312, 2438, pulseWidth))
    {
       bp_ir_cmd = IR_UP__NEXT_FOLDER;
       ir_loop = 4;
       //pushCommand(IR_UP__EQ);//Up == Next folder
    }
    else if (isInside(2687, 2813, pulseWidth))
    {
       bp_ir_cmd = IR_DOWN__PREV_FOLDER;
       ir_loop = 4;
       //pushCommand(IR_DOWN__PREV_FOLDER);//Down == Prev folder
       //pushCommand(IR_DOWN_SCN);//Down == SCN
       //pushCommand(IR_DOWN_RPT);//Down == RPT
    }
    else if (isInside(3062, 3188, pulseWidth))
    {
       fillRadioStations = fillRadioStations + 1;
       if(fillRadioStations == 3)
       {
           fillRadioStations = 0;
           bp_ir_cmd = IR_DOWN__RPT;
           //printf("R\r\n");
       }
       else
       {
           bp_ir_cmd = IR_MUTE;
       }
       //pushCommand(IR_MUTE);//Mute
    }
    else if (isInside(3437, 3563, pulseWidth))
    {
       bp_ir_cmd = IR_R;
       //pushCommand(IR_R);//R
    }
    else if (isInside(3812, 3938, pulseWidth))
    {
       bp_ir_cmd = IR_L;
       //pushCommand(IR_L);//R
    }
    else if (isInside(4187, 4313, pulseWidth))
    {
       bp_ir_cmd = IR_SRC__MODE;
       //pushCommand(IR_SRC_MODE);//SRC == Mode
    }
    else if (isInside(4562, 4688, pulseWidth))
    {
       bp_ir_cmd = IR_OK__PLAY_PAUSE;
       //pushCommand(IR_OK__PLAY_PAUSE);//OK == Play/Pause
    }
    else if (isInside(4937, 5063, pulseWidth) && bp_ir_cmd != IR_OFF)
    {
       bp_ir_cmd = IR_OFF;
       pushCommand(IR_OFF);//Long SRC = OFF
       //printf("o\r\n");
       PORTBbits.RB5 = !PORTBbits.RB5;
       PORTBbits.RB4 = !PORTBbits.RB4;
    }
    else if(isInside(1094, 1400, pulseWidth))
    {
       //release if not OFF, VOL+, VOL-
       if(IR_OFF != bp_ir_cmd
       && IR_VOL_PLUS != bp_ir_cmd
       && IR_VOL_MINUS != bp_ir_cmd
       /*&& IR_R != bp_ir_cmd*/
       /*&& IR_L != bp_ir_cmd*/)
       {
           pushCommand(bp_ir_cmd);
       }

       if(bp_ir_cmd != IR_MUTE)
       {
           fillRadioStations = 0;
       }
       //printf("r%d\r\n", fillRadioStations);

       bp_ir_cmd = 0;
    }

}

void setup_timer0()
{
    T0IE = 1;
}

void setup_timer1()
{
    TMR1CS = 0; //select internal source for timer 1 => 1/Fosz
    // select prescaller 8
    T1CKPS1 = 1;
    T1CKPS0 = 1;
    TMR1ON = 0; //enable the timer
    //TMR1 one click is 1,6 us
    TMR1 = 0; //reset timer
    //////////////////////////////////
}

void setup_timer2()
{
    //configure post scaller
    TOUTPS0 = 0;
    TOUTPS1 = 0;
    TOUTPS2 = 0;
    TOUTPS3 = 0;

    //configure prescaller
    T2CKPS1 = 0;
    T2CKPS0 = 0;

    TMR2IE = 1;
    TMR2ON = 0;
    PR2 = 250;
    PEIE = 1;
    //1 timer2 click is 50 uS
}

void configure_communication_ports()
{
    SPBRG = (BAUDRG & 0xFF);
    TXSTA = 0b00100100;
    RCSTA = 0b10010000;
    TRISC = 0b10111111;
    OPTION_REG = 0b11010111;
}

void configure_POR()
{
    if (!nPOR)
    {
       // Power On Reset occurred, bootloader did not capture an autobaud BRG value.
       nPOR = 1; // flag that future MCLR/soft resets are not POR resets
    }
}

void main(void)
{
    configure_POR();
    setup_timer0();
    setup_timer1();
    TRISD = 0b00000000;
    TRISB = 0b00000000;
    setup_timer2();
    configure_communication_ports();


    printf("Mp3 Player Controller 2.91\r\n");
    ei();

    PORTDbits.RD1 = !1;
    PORTBbits.RB4 = 0;
    PORTBbits.RB5 = 0;

    timer2Clicks = 0;

    while (1)
    {
       if (RCIF)
       {
           if (FERR && (RC7 == 0))
           {
               // RXD BREAK state detected, switch back to Bootloader mode.
               di(); // disable interrupts
#asm
               clrf _PCLATH // jump back into bootloader
               goto 0 // (must only be done from main() to avoid call stack buildup)
#endasm
           }
           putch(RCREG);
       }

       if (PORTCbits.RC3 == 0 && !enteredLowState)
       {
           enteredLowState = 1;
           TMR1 = 0;
           TMR1ON = 1;
       }
       else if (PORTCbits.RC3 == 1 && enteredLowState)
       {
           TMR1ON = 0;
           handlePulse(TMR1);
           enteredLowState = 0;
       }
    }
}

