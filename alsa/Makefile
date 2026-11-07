# ALSA external PCM plugin for raspdif (libasound_module_pcm_raspdif.so)
#
# Needs the ALSA headers (alsa/asoundlib.h, alsa/pcm_external.h), not the
# library: the plugin is loaded into a process that already has libasound, so
# its symbols resolve at load time. Cross build example:
#   make CC=aarch64-linux-gnu-gcc ALSA_INC=/path/to/dir-containing-alsa
# (CC defaults to make's built-in cc.)
ALSA_INC ?= /usr/include
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
TARGET = libasound_module_pcm_raspdif.so

all: $(TARGET)

$(TARGET): pcm_raspdif.c ../include/raspdif_shm.h
	$(CC) $(CFLAGS) -fPIC -shared -I ../include -I $(ALSA_INC) -DPIC pcm_raspdif.c -o $@

clean:
	$(RM) $(TARGET)

.PHONY: all clean
