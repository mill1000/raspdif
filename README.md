# PiPWM
DMA-enabled PWM on arbitrary GPIO for Raspberry Pi 3B.

## Usage
1. Initalize the library & timebase with the desired time resolution and a free DMA channel. 
    * Due to the internal clock dividers the exact resolution may not be achievable. piPwm_initialize will return the actual achieved resolution.
```
// Initalize for 10 us resolution
double deltaT = piPwm_initialize(dma_channel_13, 10e-6);
```
2. Initialize a PWM channel at the desired frequency and pins. Each output channel requires an additional DMA channel.
```
// Initalize PWM channel at 100 Hz on pins 12 & 16
gpio_pin_mask_t pins = 1 << 12 | 1 << 16;
pipwm_channel_t* channel = piPwm_initalizeChannel(dma_channel_14, pins, 100);

// Enable the channel
piPwm_enableChannel(channel, true);
```
3. Set the desired pulse width or duty cycle on the target pins.
   * Only pins configured in piPwm_initalizeChannel will be affected. Additional pins are ignored.
```
// Set pin 12 pulse width to 1.52 ms 
piPwm_setPulseWidth(channel, 1 << 12, 1.52e-3);

// Set pin 16  duty cycle to 50%
piPwm_setDutyCycle(channel, 1 << 16, .5);
```
4. When finished, either release the channel
```
piPwm_releaseChannel(channel);
```
or shutdown the entire library
```
piPwm_shutdown();
```

## Example
PiPWM is intended to be used as library within other projects. An example is provided which can be built with the included Makefile.

### Similar Projects
* [ServoBlaster](https://github.com/richardghirst/PiBits/tree/master/ServoBlaster)
* [pi-blaster](https://github.com/sarfata/pi-blaster)
* [RPIO](https://github.com/metachris/RPIO)
* [pigpio](http://abyz.me.uk/rpi/pigpio/)