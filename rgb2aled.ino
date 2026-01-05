/**
 * WS2812B two-pixel ARGB to RGB controller
 *
 * Captures two pixels (24 bits each, 48 bits total) from WS2812B data stream
 * and outputs the corresponding RGB values to two sets of 3 PWM pins each.
 * Then passes through all subsequent pixels in "burst mode", up to 200 pixels
 * total.
 *
 * The timing is carefully tuned for an Arduino Nano (ATmega328P @ 16MHz).
 * It cannot support more than 200 pixels due to timing constraints; even
 * switching the timer counter to 16 bits from 8 bits causes timing issues.
 * This is the primary limiting factor for longer strips.
 *
 * Components:
 * - Arduino Nano (ATmega328P @ 16MHz)
 * - 6x N-Channel Logic Level MOSFETs (e.g., IRLZ44N)
 * - 6x Resistors (10k Ohm) - 1k..100k acceptable
 * - 2x 5V Common-Anode RGB LEDs (LEDs to drive with this controller)
 *
 * Wiring expected:
 * - 5V -> Arduino 5V and LED Anodes
 * - GND -> Arduino GND and MOSFET Sources
 * - WS2812B Data In -> Arduino D7
 * - Arduino D2 -> WS2812B Data Out (daisy chain to rest of strip)
 * - Arduino D9 -> MOSFET 1 Gate -> 10k Resistor -> GND
 * - Arduino D10 -> MOSFET 2 Gate -> 10k Resistor -> GND
 * - Arduino D11 -> MOSFET 3 Gate -> 10k Resistor -> GND
 * - Arduino D3 -> MOSFET 4 Gate -> 10k Resistor -> GND
 * - Arduino D5 -> MOSFET 5 Gate -> 10k Resistor -> GND
 * - Arduino D6 -> MOSFET 6 Gate -> 10k Resistor -> GND
 * - MOSFET 1 Drain: LED 1 RED Cathode
 * - MOSFET 2 Drain: LED 1 GREEN Cathode
 * - MOSFET 3 Drain: LED 1 BLUE Cathode
 * - MOSFET 4 Drain: LED 2 RED Cathode
 * - MOSFET 5 Drain: LED 2 GREEN Cathode
 * - MOSFET 6 Drain: LED 2 BLUE Cathode
 *
 * Note that 5V and GND must be common through the full circuit, but if you are
 * building on a breadboard you should connect the 5V wires directly from the
 * input to the output connector (or patch additional power into the strip
 * after this device) and ensure you are using sufficient gauge wires.
 * **Breadboard rails are only rated for 1A.**
 *
 * Estimated max current draw, assuming 2 100mA RGB LEDs at full brightness and
 * no passthrough: 200mA (LEDs) + 30mA (Arduino Nano) = 230mA / 0.23A total, or
 * roughly 1.15W.
 *
 * Estimated max current draw, assuming 2 100mA RGB LEDs plus 200 passthrough
 * WS2812B LEDs (60mA each) at full brightness: 200mA (LEDs) + 30mA (Arduino
 * Nano) + 12,000mA (passthrough) = 12,230mA / 12.23A total, or roughly 61.15W.
 */

#include <avr/io.h>

// --- CONFIGURATION FLAG ---
// Uncomment for 2 LEDs (48-bit capture). 
// Comment out for 1 LED (24-bit capture + early pass-through).
#define TWO_LEDS

void setup() {
  // --- HARDWARE LOCK ---
  noInterrupts();
  TIMSK0 = 0; // Explicitly kill Timer 0 interrupts (millis/delay)
  TIMSK1 = 0;
  TIMSK2 = 0;

  // LED 1 Pins: D9, D10, D11 (Port B1, B2, B3)
  // LED 2 Pins: D3, D5, D6 (Port D3, D5, D6)
  DDRB |= (1 << PB1) | (1 << PB2) | (1 << PB3);
  DDRD |= (1 << DDD3) | (1 << DDD5) | (1 << DDD6);
  
  // Data Pins: D7 In, D2 Out
  DDRD &= ~(1 << DDD7);
  DDRD |= (1 << DDD2);

  // --- PWM SETUP (INVERTING MODE) ---
  TCCR0A = (1 << COM0A1) | (1 << COM0A0) | (1 << COM0B1) | (1 << COM0B0) | (1 << WGM01) | (1 << WGM00);
  TCCR0B = (1 << CS00);
  TCCR1A = (1 << COM1A1) | (1 << COM1A0) | (1 << COM1B1) | (1 << COM1B0) | (1 << WGM10);
  TCCR1B = (1 << WGM12) | (1 << CS10);
  TCCR2A = (1 << COM2A1) | (1 << COM2A0) | (1 << COM2B1) | (1 << COM2B0) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << CS20);

  // Default OFF
  OCR0A = 255; OCR0B = 255; 
  OCR1A = 255; OCR1B = 255;
  OCR2A = 255; OCR2B = 255;
}

void loop() {
  asm volatile(
    "start_frame_%=: \n\t"

    // wait until we see low signal on D7 for at least 250 iterations. Each iteration takes:
    // - sbic+jmp (when signal low): 3 cycles
    // - dec: 1 cycle
    // - brne: 2 cycles for the first 249 iterations, 1 cycle for the 250th iteration
    // total cycles: 249*(3+1+2) + (3+1+1) = 1499 cycles
    // total time: 1499 cycles * 62.5ns per cycle = 93687.5ns, or 93.69 μsecs
    // WS2812 protocol requires minimum 50 μsecs for reset, but WS2812B uses 300 μsecs.
    // So waiting for 93 μsecs (greater than 50, less than 300) should be good.
    "wait_reset_%=: \n\t"
      "ldi r22, 250 \n\t"
    "r_chk_%=: \n\t"
      "sbic 0x09, 7 \n\t" "jmp wait_reset_%= \n\t" 
      "dec r22 \n\t" "brne r_chk_%= \n\t"

    // --- CAPTURE MACRO (Tuned for 16MHz) ---
    // Timing:
    // - ldi: 1 cycle (only on the first bit of any 8-bit color register)
    // - sbis+rjmp: 3 cycles if D7 is currently low; 2 cycles if it is currently high
    //   - best case: D7 goes high exactly on the cycle when sbis executes: next instruction is 2 cycles after start of high
    //   - worst case: D7 is low when sbis executes and then goes high on the next cycle: next instruction is 4 cycles after start of high
    // - nop (x4): 4 cycles
    // - in: 1 cycle
    //
    // At this point we are somewhere between 7 and 9 cycles (0.43 to 0.56 μsecs) after high was first received.
    // WS2812 protocol is 0.4 μsecs high for a zero, and 0.8 μsecs high for a one. Our range is within that. So the
    // value we just read into r25 will either still be high (for a one) or will have dropped to low (for a zero).
    //
    // - lsl: 1 cycle
    // - sbrc+ori: 2 cycles
    //
    // This puts us at 10 to 12 cycles (0.63 to 0.75 μsecs) into the bit reading. If we are reading a one then we
    // need to wait for it to drop low; if we are reading a zero then its already low.
    //
    // - sbic+rjmp: 3 cycles while D7 is high; 2 cycles if is low
    //   - best case: we are reading a zero so its already low: 2 cycles
    //   - worst case: D7 goes low on the cycle immediately after sbic executes: next instruction is 4 cycles after the start of low
    // - dec: 1 cycle
    // - brne: 2 cycles (first 7 bits) / 1 cycle (8th bit)
    // - com: 1 cycle
    // 
    // In case of a one, the protocol dictates we only have 0.45 μsecs of low signal before the next bit starts. Here
    // we see it can take up to 8 cycles (0.5 μsecs) after sampling the current bit before we are ready to read the
    // next bit, creating a 0.05 μsec "deficit". However if this happens, on the next bit we will look for the high
    // state after at-most 2 additional cycles; or at-most 0.17 μsecs into a bit - still within the 0.4 μsecs expected
    // for a high state even for a zero bit. Then we will sample that next bit either 7 cycles (if its the in the same
    // 8 bit color register) or 8 cycles (if it is in the next color register) after we completed the previous bit.
    // Worst case: 0.55 μsecs, still within the 0.4-0.8 μsecs window for sampling the next bit.
    ".macro CAPT_SHIELD reg \n\t"
      "ldi r24, 8 \n\t"
    "10: sbis 0x09, 7 \n\t" "rjmp 10b \n\t"       // loop until high signal detected on D7
      "nop \n\t" "nop \n\t" "nop \n\t" "nop \n\t" // 4 NOPs for center-sampling. otherwise we might see "ringing" when the signal goes low->high.
      "in r25, 0x09 \n\t" "lsl \\reg \n\t"        // read D7 to r25; left-shift output register making space in the low bit for what we are reading
      "sbrc r25, 7 \n\t" "ori \\reg, 1 \n\t"      // set output register low bit to 0 (D7 low) or 1 (D7 high)
    "11: sbic 0x09, 7 \n\t" "rjmp 11b \n\t"       // loop until low signal detected on D7
      "dec r24 \n\t" "brne 10b \n\t"              // repeat 8 times, reading 8 bits total
      "com \\reg \n\t"                            // ones-complement the output register, inverting it (same as setting it to 255-value)
    ".endm \n\t"

    "CAPT_SHIELD r16 \n\t" "CAPT_SHIELD r17 \n\t" "CAPT_SHIELD r18 \n\t" // Pixel 1

    #ifdef TWO_LEDS
      "CAPT_SHIELD r19 \n\t" "CAPT_SHIELD r20 \n\t" "CAPT_SHIELD r21 \n\t" // Pixel 2
    #endif

    // --- RELAY MACRO ---
    // Now we need to enter a super-fast relay mode to pass through remaining pixels.
    // Remember that we might be starting already with a 0.05 μsec deficit so we need
    // to act fast.
    //
    // Timing:
    // - ldi: 1 cycle - may increase deficit to ~0.12 μsec
    // - sbis+rjmp: 3 cycles if low, 2 cycles if high. If we are in a deficit it will be high.
    //   - worst case: miss start of high by 6 cycles (including deficit) = 0.375 μsecs, barely within 0.4 μsec tolerance!
    // - sbi+nop: 3 cycles
    // - sbis+cbi: 2-3 cycles
    "ldi r22, 200 \n\t"
    "px_loop_%=: \n\t"
      ".macro SMASH_SHIELD \n\t"
        "30: sbis 0x09, 7 \n\t" "rjmp 30b \n\t"    
          "sbi 0x0b, 2 \n\t" "nop \n\t" 
          "sbis 0x09, 7 \n\t" "cbi 0x0b, 2 \n\t"   
        "31: sbic 0x09, 7 \n\t" "rjmp 31b \n\t"    
          "cbi 0x0b, 2 \n\t"                      
      ".endm \n\t"
      "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t"
      "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t"
      "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t"
      "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t"
      "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t"
      "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t" "SMASH_SHIELD \n\t"
      "dec r22 \n\t" 
      "breq update_now_%= \n\t"
      "jmp px_loop_%= \n\t" 
    "update_now_%=: \n\t"

    // --- COMMIT ---
    "sts 0x88, r17 \n\t" // LED1 RED
    "sts 0x8A, r16 \n\t" // LED1 GREEN
    "sts 0xB3, r18 \n\t" // LED1 BLUE
    #ifdef TWO_LEDS
      "sts 0xB4, r20 \n\t" // LED2 RED
      "sts 0x48, r19 \n\t" // LED2 GREEN
      "sts 0x47, r21 \n\t" // LED2 BLUE
    #endif

    "jmp start_frame_%= \n\t"

    : : : "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r24", "r25", "memory"
  );
}
