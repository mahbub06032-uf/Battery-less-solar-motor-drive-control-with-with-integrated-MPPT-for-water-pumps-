# F28335 SPWM with ADC-Controlled Modulating Frequency

This project generates **Sinusoidal PWM (SPWM)** on **TMS320F28335 ePWM1A**, where the **modulating sine frequency changes according to ADCINA0 input voltage**.

---

## Features

- Generates SPWM on **ePWM1A (GPIO0)**
- Reads analog voltage from **ADCINA0**
- Maps ADC voltage to sine-wave frequency
- Fixed PWM carrier frequency
- Real-time modulation frequency control

---

## Functional Principle

The PWM consists of two frequencies:

### Carrier Frequency (Fixed)
A high-frequency PWM switching signal.

Example:

- 20 kHz fixed carrier

### Modulating Frequency (Variable)
A sine reference frequency that changes according to ADC input.

Example:

- 0 V ADC input → 10 Hz sine
- 3 V ADC input → 200 Hz sine

---

## Frequency Mapping

ADC input voltage controls sine frequency:

| ADC Voltage | Output Sine Frequency |
|------------|------------------------|
| 0 V        | 28 Hz                  |
| 1.5 V      | 34 Hz                 |
| 3.0 V      | 40 Hz                 |

Linear mapping:

```text
Sine Frequency = Fmin + (ADC / FullScale) × (Fmax − Fmin)
