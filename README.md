# raspdif - Raspberry Pi S/PDIF
S/PDIF audio output on Raspberry Pi without a HAT.

raspdif accepts 16 or 24 bit PCM samples from stdin, a file, or a FIFO (named pipe). Samples are encoded and transmitted as S/PDIF data on GPIO21 (Pin 40 on J8). For older boards without a 40-pin header, see [here](#Alternate-GPIO).

## Building
### Prerequisites
* clang - Tested against `clang version 3.8.1-24+rpi1`
* A sane build environment with standard libs.

Just run make. No configuration right now.
```
make
```
Install to default location (`/usr/local/bin`)
```
sudo make install
```

## Usage
Check `raspdif --help` for additional arguments.

There are 3 ways of utilizing raspdif.
### Monitor a FIFO
raspdif can monitor a FIFO and transmit samples written to it. Silence is output if there is no data in the FIFO.

```
mkfifo /tmp/spdif_fifo
sudo raspdif --input /tmp/spdif_fifo
```

Now provide PCM data to to the FIFO in some matter.

#### Examples
Configure [gmrender-resurrect](https://github.com/hzeller/gmrender-resurrect) to output to the FIFO.
```
gmediarender --gstout-audiopipe 'audioresample ! audio/x-raw, rate=44100,format=S16LE ! filesink location=/tmp/spdif_fifo'
```

or construct a gstreamer pipeline manually that outputs to the FIFO.
```
gst-launch-1.0 uridecodebin uri="file://some_audio_file.flac" ! audioconvert ! audioresample ! audio/x-raw, rate=441
00,format=S16LE ! filesink location=/tmp/spdif_fifo
```

FFMPEG can also write directly to the FIFO.
```
ffmpeg -i some_audio_file.flac -f s16le -acodec pcm_s16le /tmp/spdif_fifo
```

### Play a file directly
raspdif can play a file directly. Files must be raw PCM in S16LE or S24LE format.
```
sudo raspdif --input ~/some_pcm_file.pcm
```

### Pipe data via stdin
```
ffmpeg -i some_audio_file.flac -f s16le -acodec pcm_s16le - | sudo raspdif
```

### Set the sample rate
raspdif defaults to a sample rate of 44.1 kHz. Alternate sample rates can be specified on the command line with the `--rate` option. Rates up to 192 kHz have been tested successfully.

### Set the sample format
raspdif supports 16 or 24 bit PCM samples. Use the `--format` option to select between `s16le` and `s24le`.

## Signal Levels
S/PDIF specification calls for .5 V Vpp when 75 Ohm is connected across the output. To achieve these level from the Raspberry Pi's nominal 3.3 V signaling a simple resistive divider can be build with a 390 Ohm resister is series with the output.

![Resistive Divider](raspdif_divider.png)

However, my DAC and receiver have not had an issue receiving the full 3.3 V signal. Use at your own risk.

## Alternate GPIO
raspdif can be reconfigured to output on GPIO31 instead of the default GPIO21. For Rev 2 boards, GPIO31 is located on the P5 header.

To reconfigure for GPIO31 the following changes need to be made.
```diff
  gpio_configuration_t gpioConfig;
  gpioConfig.eventDetect = gpio_event_detect_none;
-  gpioConfig.function = gpio_function_af0;
+  gpioConfig.function = gpio_function_af2;
  gpioConfig.pull = gpio_pull_no_change;
-  gpioConfigureMask(1 << 21 , &gpioConfig);
+  gpioConfigureMask(1 << 31 , &gpioConfig);
```

Thanks to @marcoalexcampos0 for digging into this and testing.