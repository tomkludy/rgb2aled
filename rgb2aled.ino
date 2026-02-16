/**
 * WS2812B Hyper-Optimized 1/2 Pixel Controller
 * -------------------------------------------
 * - Target: ATmega328P @ 16MHz (1 cycle = 62.5ns)
 * - Sampling Window: 437.5ns - 562.5ns (Cycles 7-9)
 * - Relay: 1:1 pulse preservation with constant 6-cycle latency.
 * 
 * Captures two pixels (24 bits each, 48 bits total) from WS2812B data stream
 * and outputs the corresponding RGB values to two sets of 3 PWM pins each.
 * Then passes through all subsequent pixels in "burst relay mode".
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
 * - Arduino D3 -> MOSFET 4 Gate -> 10k Resistor -> GND (optional - for 2 LEDs)
 * - Arduino D5 -> MOSFET 5 Gate -> 10k Resistor -> GND (optional - for 2 LEDs)
 * - Arduino D6 -> MOSFET 6 Gate -> 10k Resistor -> GND (optional - for 2 LEDs)
 * - MOSFET 1 Drain: LED 1 RED Cathode
 * - MOSFET 2 Drain: LED 1 GREEN Cathode
 * - MOSFET 3 Drain: LED 1 BLUE Cathode
 * - MOSFET 4 Drain: LED 2 RED Cathode (optional - for 2 LEDs)
 * - MOSFET 5 Drain: LED 2 GREEN Cathode (optional - for 2 LEDs)
 * - MOSFET 6 Drain: LED 2 BLUE Cathode (optional - for 2 LEDs)
 *
 * Estimated max current draw, assuming 2 100mA RGB LEDs at full brightness and
 * no passthrough: 200mA (LEDs) + 30mA (Arduino Nano) = 230mA / 0.23A total, or
 * roughly 1.15W.
 *
 * Note that 5V and GND must be common through the full circuit, but if you are
 * building on a breadboard you should patch additional power into the strip
 * after this device and ensure you are using sufficient gauge wires. ALEDs are
 * typically rated for 60mA at full brightness. So to keep under 1A total, you
 * should not connect more than ~12 additional LEDs to the daisy chain output of
 * this controller without additional power injection.
 * 
 * 
 * rgb2aled  © 2026 by Tom Kludy is licensed under CC BY 4.0. To view a copy of
 * this license, visit https://creativecommons.org/licenses/by/4.0/
 */

#include <avr/io.h>

// Comment the following for 24-bit capture (1 LED).
#define TWO_LEDS

// There is a prototype variant of the board that uses D6 for data in and D7 for data out.
// I do not want to rewire it or maintain two separate codebases, so this define allows
// swapping the input and output pins as needed.
//#define ALTERNATE_DATA_PINS
#ifdef ALTERNATE_DATA_PINS
  #undef TWO_LEDS // ALTERNATE_DATA_PINS only supports 1 LED mode because D6 is used for data in.
  #define DATA_IN    6
  #define DATA_OUT   7
#else
  #define DATA_IN    7
  #define DATA_OUT   2
#endif

void setup() {
  noInterrupts();
  
  // Disable Arduino default timer interrupts
  TIMSK0 = 0; TIMSK1 = 0; TIMSK2 = 0;

  // Pin Direction Setup
  DDRB |= (1 << PB1) | (1 << PB2) | (1 << PB3);   // LED 1 (D9, D10, D11)
  DDRD |= (1 << DDD3) | (1 << DDD5) | (1 << DDD6); // LED 2 (D3, D5, D6)
  
  DDRD &= ~(1 << DATA_IN); // Input (Data In)
  DDRD |= (1 << DATA_OUT); // Output (Relayed Data Out)

  // PWM Timer Setup (Inverting Mode for Common Anode)
  TCCR0A = (1 << COM0A1) | (1 << COM0A0) | (1 << COM0B1) | (1 << COM0B0) | (1 << WGM01) | (1 << WGM00);
  TCCR0B = (1 << CS00);
  TCCR1A = (1 << COM1A1) | (1 << COM1A0) | (1 << COM1B1) | (1 << COM1B0) | (1 << WGM10);
  TCCR1B = (1 << WGM12) | (1 << CS10);
  TCCR2A = (1 << COM2A1) | (1 << COM2A0) | (1 << COM2B1) | (1 << COM2B0) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << CS20);
  
  // Initialize LEDs to OFF (Common Anode 255 = Off)
  OCR0A = 255; OCR0B = 255; OCR1A = 255; OCR1B = 255; OCR2A = 255; OCR2B = 255;
}

void loop() {
  asm volatile(
    // --- MACROS ---

    // Reads 1 bit: Waits for Rise, Delays to Center, Samples, Waits for Fall
    //
    // Sampling window: 437.5ns - 562.5ns (Cycles 7-9)
    //
    // The Wemos S2 seems to have too short of a high bit pulse width, falling
    // around ~650ns instead of 800ns as expected by WS2812 spec. So we sample
    // earlier than the ideal center. This does run the risk that a LOW bit
    // could be misread as HIGH if the pulse drops late; by the spec, a LOW bit
    // can be as long as 500ns. However the Wemos S2 seems to have a maximum
    // LOW pulse width of around 400ns in practice.
    ".macro READ_BIT reg \n\t"
      // 1. Wait for Rise
      // Best Case (Rise just before sample): 2 cycles latency.
      // Worst Case (Rise just after sample): 4 cycles latency.
      "1: sbis 0x09, %[data_in] \n\t" "rjmp 1b \n\t"
      // [Range: T=2 to T=4] [125ns - 250ns]

      // 2. Delay to Center (4 NOPs + 1 LSL)
      "nop \n\t" "nop \n\t" "nop \n\t" "nop \n\t"
      "lsl \\reg \n\t"        
      // [Range: T=7 to T=9] [437.5ns - 562.5ns] 
      // *** SAMPLING POINT ***
      // WS2812B '0' is HIGH for ~300-400ns. WS2812B '1' is HIGH for ~625-800ns.

      // 3. Sample (2 cycles total)
      // Path 1 (Low): sbic(2) = 2.
      // Path 2 (High): sbic(1) + ori(1) = 2.
      "sbic 0x09, %[data_in] \n\t"     
      "ori \\reg, 1 \n\t"     
      // [Range: T=9 to T=11] [562.5ns - 687.5ns]

      // 4. Wait for Fall (Sync Anchor)
      // Best case (Already Low): sbic(2) = 2 cycles. [Exit T=11-13]
      // Worst case (Wait for edge): sbic(1)+rjmp(2)+sbic(2) = 5 cycles. [Exit T=14-16]
      "2: sbic 0x09, %[data_in] \n\t" "rjmp 2b \n\t"
    ".endm \n\t"

    ".macro READ_BYTE reg \n\t"
      "READ_BIT \\reg \n\t" "READ_BIT \\reg \n\t" "READ_BIT \\reg \n\t" "READ_BIT \\reg \n\t"
      "READ_BIT \\reg \n\t" "READ_BIT \\reg \n\t" "READ_BIT \\reg \n\t" "READ_BIT \\reg \n\t"
    ".endm \n\t"


    // --- 1. INITIAL POWER-ON SYNC ---
    "start_frame_%=: \n\t"
    #ifndef TWO_LEDS
      // since we aren't reading any values for LED 2, we initialize the
      // associated registers to OFF (255 due to inverted signal).
      "ldi r19, 255 \n\t" "ldi r20, 255 \n\t" "ldi r21, 255 \n\t"
    #endif
    "wait_reset_%=: \n\t"
      "ldi r22, 250 \n\t"
    "r_chk_%=: \n\t"
      "sbic 0x09, %[data_in] \n\t" "jmp wait_reset_%= \n\t" 
      "dec r22 \n\t" "brne r_chk_%= \n\t"


    // --- 2. CAPTURE PHASE ---
    "capture_start_%=: \n\t"
    "READ_BYTE r16 \n\t" // G1
    "READ_BYTE r17 \n\t" // R1
    "READ_BYTE r18 \n\t" // B1

    #ifdef TWO_LEDS
      "READ_BYTE r19 \n\t" // G2
      "READ_BYTE r20 \n\t" // R2
      "READ_BYTE r21 \n\t" // B2
    #endif


    // --- 3. RELAY PHASE ---
    "relay_entry_%=: \n\t"
      // Linear timeout: 256 checks * 3 cycles + 4 cycles = 772 cycles (~48us reset window)
      // When LOW: 3 cycles per check (sbis + rjmp)
      // When HIGH occurs at the moment of sbis: 0 cycle delay
      // When HIGH occurs just after sbis: 2 cycles delay (rjmp)
      // Thus, we can be between 0-2 cycles late when detecting HIGH.
      ".rept 256 \n\t"
        "sbis 0x09, %[data_in] \n\t"  // Cycles 1-2: If HIGH, skip next (if LOW, only 1 cycle)
        "rjmp .+6 \n\t"               // Cycles 2-3: If LOW, skip to next iteration

        // The rjmp offset doesn't support labels because we are in a .rept loop.
        // So we need to manually calculate the offset to jump. Each of the
        // instructions is labeled with instruction byte counts for clarity;
        // eg. "[2]" means 2 bytes for instruction(s) on that line. You can
        // then sum those up and make sure the rjmp offset above is correct.

        // We need to keep the bit-start latency to a minimum so we can't spend any
        // extra cycles jumping out of the loop. But we also need to keep the number
        // of instructions in the loop down to avoid "relocation truncated to fit"
        // compilation error.
        "sbi 0x0b, %[data_out] \n\t"  // [2] Cycles 3-4: set HIGH (Pin High at the end of cycle 4)
        "jmp relay_bit_%= \n\t"       // [4] Cycles 5-7: get out of the .rept
      ".endr \n\t"
      "jmp update_now_%= \n\t"        // If we finish 256 reps, it's a RESET. Update LEDs.

    "relay_bit_%=: \n\t"

      // set LOW if 0, clock-synced:  // LOW path  / HIGH path
      "sbis 0x09, %[data_in] \n\t"    // 8         / 8-9   : Sample D7
      "cbi 0x0b, %[data_out] \n\t"    // 9-10      / -     : Set D2 to match D7; 0-bit HIGH duration=6 cycles
      "sbic 0x09, %[data_in] \n\t"    // 11-12     / 10    : Skip next if D7 HIGH
      "rjmp .+0 \n\t"                 // -         / 11-12 : Sync cycle count

      "nop \n\t"                      // Cycle 13

      "cbi 0x0b, %[data_out] \n\t"    // Cycles 14-15: set output LOW (at end of cycle 15); 1-bit HIGH duration=11 cycles
      "jmp relay_entry_%= \n\t"       // Cycles 16-18: Loop back to check for next bit


    // --- 4. UPDATE PHASE ---
    // The only way that we get here is by jumping through 256 relay checks above
    // without seeing any HIGH on the input line. This indicates a reset period.
    //
    // This code executes during the reset gap, when we have a lot of extra time before
    // we expect the next frame to start. (Reset is usually >300ns.) We use this time to do all of the work
    // that we deferred during the capture and relay phases.
    "update_now_%=: \n\t"

    // Invert captured values for Common Anode LEDs
    "com r16 \n\t" "com r17 \n\t" "com r18 \n\t"
    #ifdef TWO_LEDS
      // we only want to invert these if we read them from input;
      // when !TWO_LEDS, we want to leave these untouched so they
      // always contain the OFF value.
      "com r19 \n\t" "com r20 \n\t" "com r21 \n\t"
    #endif

    // Commit to PWM registers
    "sts 0x88, r17 \n\t" // OCR1A (D9)  - LED1 Red
    "sts 0x8A, r16 \n\t" // OCR1B (D10) - LED1 Green
    "sts 0xB3, r18 \n\t" // OCR2A (D11) - LED1 Blue

    "sts 0xB4, r20 \n\t" // OCR2B (D3)  - LED2 Red
    "sts 0x48, r19 \n\t" // OCR0B (D5)  - LED2 Green
    "sts 0x47, r21 \n\t" // OCR0A (D6)  - LED2 Blue

    // Jump directly to capture bits for the next frame.
    "jmp capture_start_%= \n\t" 

    :
    : [data_in] "I" (DATA_IN), [data_out] "I" (DATA_OUT)
    : "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23", "memory"
  );
}
