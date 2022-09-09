# Custom Firmware for Elecrow Watering Kit 2.1

I purchased the [Elecrow Watering Kit 2.1](https://www.elecrow.com/arduino-automatic-smart-plant-watering-kit.html)
from [Amazon](https://www.amazon.com/Elecrow-Watering-Moisture-Gardening-Automatic/dp/B07LCNKC6N). 
The controller board for this kit has an integrated Arduino Leonardo.

This kit was a great starting point but I've since made some changes from the original code.

## Acknowledgements ##

A copy of the Elecrow code in the `orig/` folder for comparison purposes. The firmware that was provided by Elecrow has several issues, not the least of which are graphical gitches on the display.

This work is based off of the excelent fixes and additions made by:
* A [version of the firmware](https://github.com/liutyi/elecrow-watering-kit-2-li)
modified by [liutyi](https://wiki.liutyi.info/display/ARDUINO/Arduino+Automatic+Smart+Plant+Watering+Kit+2.0a) that fixed graphical gitches.
* A [modified version of the firmware](https://github.com/kdorff/watering-kit) modified by kdorff that does significant code cleanup and also adds many new features such as MQTT support as well as help/advice on physical installation.

This code is based on kdorff's modifications but have been modified for my personal usage.  kdorff's contributions were numerous, so in an effort to keep the contributions separated I decided to keep his README here as `kdorff-README.md`.

## Goals ##
Personal restrictions / caveats / comments:
* I'm only worried about a single plant
* I don't really care about the flower graphic nor the boot logo.  IMHO all they do are take up storage memory.
* The button is pretty useless.
* The MQTT metrics push to an external datastore is nice, but requires additional hardware.
* The pot that I'm using has a hole in the bottom to prevent over-watering.  A few things that I have noticed is that whenever the pump activates, the water is added too quickly to the pot, causing most of it to drain out of the bottom.  I _think_ this is because of the time delay between water injection, the distribution of that water into the soil, and the detection of that moisture at the sensor.

## Changes from upstream ##

* `moisture-calibration`: Added MIN/MAX display outputs.
* `moisture-calibration`: Added dedicated README.md / usage guide.
* Updated WET/DRY values to be arrays instead of single values.
* Added a `set-RTC` app to set the real-time clock data.
* Remove flower graphics and splash screen.
* Use one of the sensors as an overflow detector.
* Added a max "on" time for the pump and a forced cool-down period.
* Added an in-memory metrics store for moisture levels.  Graph to the display.

## To-Do  ##
* Track / graph when pump is running.
