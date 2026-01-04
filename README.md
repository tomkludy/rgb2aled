# rgb2aled

## What it does
This project allows you to connect 1 or 2 RGB LEDs to an addressable LED (WS2812) controller, and provides a standard WS2812 passthrough so that you can daisy-chain additional LEDs - or even daisy chain these controllers together.

The LEDs must be:
- 5V
- Common-anode

Note that in its current form this cannot support 12V RGB LEDs nor common-cathode LEDs, though it shouldn't be super difficult to make variations to the design for those.

## Why is it useful
I have been building a virtual pinball machine, following Emil from [Way of the Wrench](https://youtube.com/playlist?list=PLrqlHbqP7FINmGgJoszVvWOOyb8shdfUn&si=c6fxJmt26F5moLLm)'s build guide, but I am not using exactly the same parts. I have a [fire button lockdown bar](https://virtuapin.net/index.php?main_page=product_info&cPath=3&products_id=258&zenid=7tpvet5sqb5fvuqoi35te1doc5) from [Virtuapin.net](virtuapin.net), and I have 4 [RGB leaf buttons](https://www.clevelandsoftwaredesign.com/pinball-parts/p/rgb-true-leaf-button) from [Cleveland Software Design](clevelandsoftwaredesign.com). I was having trouble trying to connect them to my cabinet and control them with DOF but I don't have a 5V RGB LED controller. I foolishly tried connecting one to a 12V RGB controller and the "magic smoke" was released! (don't do that!) Emil's videos sparked the idea to adapt these LEDs to support the WS2812 addressable-LED standard, that way I can connect them to my Wemos lighting controller.

There are probably other use cases for hooking "regular" LEDs up to addressable-LED light strips. If you have another use case let me know!

## Hardware
To support 1 RGB LED, you'll need:
- 1x Arduino Nano. I got a pack of 5 of these for $10 on Amazon.
- 3x resistors - anything from 1kΩ to 100kΩ should work fine. I used a mix of 10kΩ and 5.1kΩ that I had lying around.
- 3x N-type MOSFETs; I used IRLZ44Ns.
- Something to mount it on. I used a 1/2 size breadboard from ElectroCookie.
- Male and female JST 3-pin connectors to connect to a standard WS2812 daisy chain.
- 1x RGB LED connector.
- Various jumper wires and solder.

If you want to support 2 RGB LEDs from one board, add on:
- 3x additional resistors
- 3x additional N-type MOSFETs
- 1x additional RGB LED connector.

Sorry - there's no way to drive more than 2 LEDs from one Arduino, at least not with this design. The Arduino only has 6 PWM output channels and each LED uses 3 of them. To support my 5 total RGB LEDs I just built 3 of these and daisy-chained them.

The board hooks up to a standard addressable-LED strip or controller, acts as 1 or two additional "pixels", and then passes through up to 200 more "pixels" to additional controllers or light strips. The limit of 200 is due to the extremely tight timing required by the WS2812 protocol - the Arduino is only *barely* fast enough to handle this at all, and going over 200 daisy-chained lights caused it to start flickering incorrect colors in my testing due to timing errors.

> [!WARNING]
> Although 200 lights *can* be daisy chained, make sure if you do so that you inject power to the strip immediately after this board in the sequence. Each LED can use up to 60mA power at full brightness, so 200 lights x 60mA = 12 amps of power! A breadboard is usually only rated for 1-2 amps at most. Usually light strips have both an input connector and separate power and ground inputs - hook those directly to your 5V power supply to avoid sending that much current through this controller.

## Building it
The wiring diagram is shown in the PDF. This will fit on a 1/2 size breadboard, though its tight:
- Put the Arduino on columns 1-15, straddling the ravine, USB port facing left. Connect its 5V and GND pins to the appropriate power and ground rails.
- Put 3 resistors on the top, spanning from the GND rail to columns 18, 23, and 28. If you cross the 5V rail, make sure you are not shorting it. Repeat the same connections, same columns, at the bottom if you want to support 2 LEDs.
- Put 3 jumper wires on the top, spanning from the GND rail to columns 20, 25, and 30. Repeat the same connections, same columns, at the bottom if you want to support 2 LEDs.
- Put 3 MOSFETs on the top; columns 18-20, 23-25, 28-30. Put 3 more MOSFETs on the bottom, same columns, if you want to support 2 LEDs. All 6 MOSFETS should have the gate on the left and source on the right.
- Connect Arduino pins D9 (LED 1 red channel), D10 (LED 1 green channel), D11 (LED 1 blue channel) to the 3 MOSFETs on the top row, columns 18, 23, and 28 respectively. (Yes, these are the same columns with the resistors and the MOSFET Gates.) If you want to support 2 LEDs, also connect pins D3 (red), D4 (green), and D6 (blue) to the 3 MOSFETs on the bottom row, again using columns 18, 23, and 28 respectively.
- Connect your data-in (male JST connector center pin) to Arduino pin D7 and data-out (female JST connector center pin) to Arduino pin D2. Connect the power and ground pins of these connectors to the power and ground rails of the breadboard.
- Connect an RGB LED connector to pins 19, 22, and 29 at the top (the MOSFET drains), and connect its common anode to the 5V rail.

Double-check everything before you power on, especially check that you haven't shorted power and ground, and that the ground is common to all components (the timing required by the WS2812 protocol will not work if ground is not correct).

## Software
Next you need to program the Arduino to make it function. Open the Arduino IDE and create a "sketch". Insert the code from the .ino file. If you have only one LED, comment out the "#define TWO_LEDS". Compile and upload it to your Arduino.
