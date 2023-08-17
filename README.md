# actron-air-wifi

Modernizaion of actronair AM7 wall controller

ActronAir AM7 wall controller is quite dated and in my case the controller is not installed in the location I want the temperature measurement. This is an arduino project using the ESP8266 to allow commands to be sent wirelessly to the controller, so another ESP8266 can monitor the temperature in the room you wish and send commands to the wall controller.

## Parts used

* 2x LOLIN ESP board (one connected to the air conditioning unit, one in the room where you want the temperature measured)
* SSD1306 OLED display
* DHT11 temperature sensor
* 1x BC548
* 1x MCP4726 DAC

## Web based

The web based folder contains one file to be loaded to an ESP hooked to the wall unit (AM7 controller) and another ESP that is hooked to the temperature sensor.
You can run the web based stuff either on a PC or a raspberry pi or something similar.

## Wires

There are 4 wires inside the wall unit that connect to the main supply:

* SENS - sensor line, this is directly connected to a thermistor (around 10kOhm)
* KEY - connected to the keypad, it changes resistance to a different value when keys are pressed. The buttons I'm interested on and measured are: room1 2.5kOhm, room2 5.7kOhm, room3 3.5kOhm, room 4 3.5kOhm, fan mode 1.8kOhm, power 0Ohm (short). If you short the KEY wire to the ground 5mA goes through it (meaning it's 5V across 1kOhm), so one can use a BC548 to gate the current to the appropriate amount.
* COMM - ground
* POWER - 16V and also data line that gets shifted into the LED. The protocol in this wire is quite simple: Data is transmitted every 200ms or so. It starts with a 16V->0V transition, followed by 41 other pulses, a zero or a one is determined by the time between the pulses. Each value is then shifted through the leds in the board.

## Connecting to the ESP boards

### Wall unit to ESP

![Circuit](IMG_0640.jpg?raw=true "Wiring")

### Sensor (room) board

TODO
