// ================================================================
// F28335 SPWM on ePWM1A with sine frequency controlled by ADCINA0
//
// What this does:
//   - ePWM1A outputs SPWM
//   - ADCINA0 is sampled continuously
//   - ADC value sets sine-wave frequency
//   - Carrier PWM frequency remains fixed
//
// Example mapping:
//   ADC 0V    -> 10 Hz sine
//   ADC 3.0V  -> 200 Hz sine
//
// You can change:
//   PWM_CARRIER_FREQ_HZ
//   SINE_FREQ_MIN_HZ
//   SINE_FREQ_MAX_HZ
//
// Device: TMS320F28335
// ================================================================

#include "DSP28x_Project.h"
#include <math.h>

// ---------------- USER SETTINGS ----------------
#define PWM_CARRIER_FREQ_HZ   20000.0f   // Fixed PWM carrier = 20 kHz
#define SINE_FREQ_MIN_HZ      10.0f      // Minimum sine output frequency
#define SINE_FREQ_MAX_HZ      200.0f     // Maximum sine output frequency

#define ADC_MAX_COUNT         4095.0f
#define ADC_REF_VOLT          3.0f       // F28335 ADC reference usually 3.0V

#define SINE_TABLE_SIZE       256
#define SYSCLK_HZ             150000000.0f

// ePWM time-base clock assumption:
// HSPCLKDIV = 1, CLKDIV = 1  => TBCLK = SYSCLK
#define TBCLK_HZ              SYSCLK_HZ

// Up-down count mode:
// PWM freq = TBCLK / (2 * TBPRD)
#define TBPRD_VALUE           ((Uint16)(TBCLK_HZ / (2.0f * PWM_CARRIER_FREQ_HZ)))

// ---------------- GLOBALS ----------------
volatile Uint16 adc_result = 0;
volatile float  adc_voltage = 0.0f;
volatile float  sine_freq_hz = 10.0f;

volatile float  phase_acc = 0.0f;
volatile float  phase_step = 0.0f;

float sine_table[SINE_TABLE_SIZE];

// ---------------- FUNCTION PROTOTYPES ----------------
void InitSystem(void);
void InitEPwm1_SPWM(void);
void InitADC_A0(void);
void BuildSineTable(void);
interrupt void epwm1_isr(void);

// ================================================================
// main
// ================================================================
int main(void)
{
    InitSystem();
    BuildSineTable();
    InitEPwm1_SPWM();
    InitADC_A0();

    EINT;   // Enable Global interrupt INTM
    ERTM;   // Enable Global realtime interrupt DBGM

    while(1)
    {
        // Main loop can remain empty
        // Everything is handled in ePWM1 ISR
    }
}

// ================================================================
// System init
// ================================================================
void InitSystem(void)
{
    InitSysCtrl();

    DINT;
    InitPieCtrl();

    IER = 0x0000;
    IFR = 0x0000;
    InitPieVectTable();

    EALLOW;
    PieVectTable.EPWM1_INT = &epwm1_isr;
    EDIS;
}

// ================================================================
// Build sine LUT: values normalized from 0.0 to 1.0
// ================================================================
void BuildSineTable(void)
{
    Uint16 i;
    for(i = 0; i < SINE_TABLE_SIZE; i++)
    {
        float angle = 2.0f * 3.14159265358979f * ((float)i / (float)SINE_TABLE_SIZE);
        sine_table[i] = 0.5f + 0.5f * sinf(angle);   // shift to 0...1
    }
}

// ================================================================
// ePWM1 setup for SPWM output on ePWM1A
// ================================================================
void InitEPwm1_SPWM(void)
{
    EALLOW;

    // GPIO0 = EPWM1A
    GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 1;
    GpioCtrlRegs.GPADIR.bit.GPIO0  = 1;

    EDIS;

    // Time-base
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // symmetric PWM
    EPwm1Regs.TBCTL.bit.PHSEN   = TB_DISABLE;
    EPwm1Regs.TBCTL.bit.PRDLD   = TB_SHADOW;
    EPwm1Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_DISABLE;
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1;
    EPwm1Regs.TBCTL.bit.CLKDIV    = TB_DIV1;

    EPwm1Regs.TBPRD = TBPRD_VALUE;
    EPwm1Regs.TBPHS.half.TBPHS = 0;
    EPwm1Regs.TBCTR = 0;

    // Compare
    EPwm1Regs.CMPA.half.CMPA = TBPRD_VALUE / 2; // Start at 50%
    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO;

    // Action qualifier: PWM on A
    EPwm1Regs.AQCTLA.bit.CAU = AQ_CLEAR; // clear on up-count compare
    EPwm1Regs.AQCTLA.bit.CAD = AQ_SET;   // set on down-count compare

    // Interrupt on every PWM cycle
    EPwm1Regs.ETSEL.bit.INTSEL = ET_CTR_ZERO;
    EPwm1Regs.ETSEL.bit.INTEN  = 1;
    EPwm1Regs.ETPS.bit.INTPRD  = ET_1ST;

    // Enable PIE interrupt group 3 for ePWM1
    PieCtrlRegs.PIEIER3.bit.INTx1 = 1;
    IER |= M_INT3;
}

// ================================================================
// ADC setup for ADCINA0
// ================================================================
void InitADC_A0(void)
{
    InitAdc();

    EALLOW;

    // ADC clock settings
    AdcRegs.ADCTRL3.bit.ADCCLKPS = 6;       // ADC clock prescaler
    AdcRegs.ADCTRL1.bit.CPS = 0;            // Core clock /1
    AdcRegs.ADCTRL1.bit.SEQ_CASC = 1;       // Cascaded sequencer
    AdcRegs.ADCTRL1.bit.CONT_RUN = 0;       // Start manually each ISR
    AdcRegs.ADCTRL1.bit.ACQ_PS = 6;         // Acquisition window

    AdcRegs.ADCTRL2.bit.EPWM_SOCA_SEQ1 = 0; // Not using ePWM SOC here
    AdcRegs.ADCTRL2.bit.INT_ENA_SEQ1 = 0;

    // Convert ADCINA0 as first conversion
    AdcRegs.ADCCHSELSEQ1.bit.CONV00 = 0;    // ADCINA0
    AdcRegs.ADCMAXCONV.bit.MAX_CONV1 = 0;   // Only 1 conversion

    EDIS;
}

// ================================================================
// ePWM1 ISR
// Runs at carrier frequency
// - starts ADC conversion
// - reads previous ADC result
// - updates sine frequency from ADC voltage
// - updates CMPA for SPWM
// ================================================================
interrupt void epwm1_isr(void)
{
    Uint16 index;
    float duty;
    Uint16 cmp_val;

    // -------- Start ADC conversion --------
    AdcRegs.ADCTRL2.bit.RST_SEQ1 = 1;       // Reset SEQ1
    AdcRegs.ADCTRL2.bit.SOC_SEQ1 = 1;       // Start conversion

    // Wait for conversion complete
    while(AdcRegs.ADCST.bit.INT_SEQ1 == 0);

    AdcRegs.ADCST.bit.INT_SEQ1_CLR = 1;

    // Read ADC result (12-bit result is in upper bits)
    adc_result = (AdcRegs.ADCRESULT0 >> 4) & 0x0FFF;

    // Convert ADC to voltage
    adc_voltage = ((float)adc_result / ADC_MAX_COUNT) * ADC_REF_VOLT;

    // Map ADC voltage to sine frequency
    // 0V -> SINE_FREQ_MIN_HZ
    // 3.0V -> SINE_FREQ_MAX_HZ
    sine_freq_hz = SINE_FREQ_MIN_HZ +
                   (adc_voltage / ADC_REF_VOLT) * (SINE_FREQ_MAX_HZ - SINE_FREQ_MIN_HZ);

    // Clamp
    if(sine_freq_hz < SINE_FREQ_MIN_HZ) sine_freq_hz = SINE_FREQ_MIN_HZ;
    if(sine_freq_hz > SINE_FREQ_MAX_HZ) sine_freq_hz = SINE_FREQ_MAX_HZ;

    // Phase increment per ISR
    // ISR rate = carrier frequency in up-down with INT at CTR=ZERO once per full PWM period
    phase_step = ((float)SINE_TABLE_SIZE * sine_freq_hz) / PWM_CARRIER_FREQ_HZ;

    phase_acc += phase_step;
    while(phase_acc >= (float)SINE_TABLE_SIZE)
        phase_acc -= (float)SINE_TABLE_SIZE;

    index = (Uint16)phase_acc;

    // LUT output: 0..1
    duty = sine_table[index];

    // Limit duty slightly to avoid 0% and 100%
    if(duty < 0.02f) duty = 0.02f;
    if(duty > 0.98f) duty = 0.98f;

    // Convert duty to CMPA
    cmp_val = (Uint16)(duty * (float)EPwm1Regs.TBPRD);
    EPwm1Regs.CMPA.half.CMPA = cmp_val;

    // Clear interrupt flags
    EPwm1Regs.ETCLR.bit.INT = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
}
