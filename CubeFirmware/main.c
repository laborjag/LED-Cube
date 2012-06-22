/*
 * main.c
 *
 * Copyright 2011 Thomas Buck <xythobuz@me.com>
 * Copyright 2011 Max Nuding <max.nuding@gmail.com>
 * Copyright 2011 Felix Bäder <baeder.felix@gmail.com>
 *
 * This file is part of LED-Cube.
 *
 * LED-Cube is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LED-Cube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LED-Cube.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef F_CPU
#define F_CPU 16000000L
#endif
 
#define OK 0x42
#define ERROR 0x23

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdlib.h>
#include <avr/wdt.h>

#include "serial.h"
#include "cube.h"
#include "time.h"
#include "audio.h"
#include "mem.h"
#include "memLayer.h"
#include "twi.h"
#include "strings.h"
#include "visualizer.h"
#include "animations.h"
#include "transmit.h"

#define NOERROR 0
// Audio does not answer
#define AUDIOERROR 1
// Memory does not answer
#define MEMORYERROR 2
// Memory not writeable
#define MEMORYWRITEERROR 4
// x = errorcode, e = error definition, not NOERROR
#define ISERROR(x, e) ((x) & (e))

// Length of an idle animation frame, 24 -> 1 second
#define IDLELENGTH 48

void serialHandler(char c);
uint8_t audioModeSelected(void);
void printErrors(uint8_t e);
uint8_t selfTest(void);
void printTime(void);

// #include "snake.c"

uint8_t shouldRestart = 0;
uint8_t refreshAnimationCount = 1;
uint8_t lastButtonState = 0;
uint8_t maxButtonState = 0;
char buffer[11];

#include "builtInFrames.c"

uint8_t DebugDone = 0; // Bit 0: 10s int. count, Bit 1: idle switch
					   // Bit 2: state changed, disable idle

int main(void) {
	uint8_t *audioData = NULL;
	uint8_t *imageData = NULL;
	uint8_t i, length = 0;
	uint16_t count;
	uint64_t lastChecked;
	uint8_t idleCounter = 0;
	uint32_t temp;

	MCUCSR = 0;
	wdt_disable();

	DDRA = 0xFF; // Latch Data Bus as Output
	DDRD = 0xFC; DDRB = 24; // Mosfets as Output
	DDRC = 0xFC; DDRB |= 6; // Latch Enable as Output
	DDRB &= ~(1 << PB0); // Pushbutton as Input

	initCube();
	serialInit(25, 8, NONE, 1);
	i2c_init();
	initSystemTimer();
	sei(); // Enable Interrupts

	setImage(defaultImageCube); // Display something

	wdt_enable(WDTO_1S); // Watchdog reset after 1 second

	serialWriteString(getString(2)); // "Initialized: "

	i = selfTest();
	if (i) {
		serialWriteString(getString(1)); // Selftest error
		serialWriteString(itoa(i, buffer, 2));
		serialWrite('\n');
		printErrors(i);
	}

	serialWriteString(getString(0)); // Version

	maxButtonState = numberOfVisualizations() + 1; // All visualizations and anim mode

	audioModeSelected();
	lastChecked = getSystemTime();

	i = 0; // Image count
	count = getAnimationCount();

	while (1) {
		// Reset if requested
		if (!shouldRestart) {
			wdt_reset();
		}

		if(lastButtonState >= 1) {
			// Get Audio Data and visualize it.
			// Visualization id is in (lastButtonState - 1)
			if (isFinished()) {
				audioData = getAudioData(); // Not malloc'ed => Don't free
				if (audioData != NULL) {
					runVisualization(audioData, lastButtonState - 1);
				}
			}
		} else {
			if (refreshAnimationCount) {
				// Get animation count stored in FRAM via TWI, if needed
				count = getAnimationCount();
				refreshAnimationCount = 0;
				i = 0;
			}

			if (count > 0) { // We have frames stored
				if (isFinished() > length) {
					// Load next image
					if (i < (count - 1)) {
						i++;
					} else {
						i = 0;
					}

					imageData = getFrame(i);
					length = imageData[64];
					setImage(imageData);
					free(imageData);
				}
			} else { // No frames available
				if (!(DebugDone & 4)) { // Idle animation allowed
					if (DebugDone & 2) {
						if (idleCounter < numOfAnimations()) {
							executeAnimation(idleCounter++);
						} else {
							idleCounter = 0;
							DebugDone &= ~(2); // Show frames again
						}
					} else {
						// Show idle frames
						if (isFinished() >= IDLELENGTH) {
							setImage(idleAnimation[idleCounter]);
							if (idleCounter < IDLEANIMATIONCOUNT) {
								idleCounter++;
							} else {
								idleCounter = 0;
								DebugDone |= 2; // Show animation
							}
						}
					}
				}
			}
		}

		if (serialHasChar()) {
			serialHandler((char)(serialGet()));
		}

		// Print fps after one second
		if ((getSystemTime() >= 1000) && ((DebugDone & 1) == 0)) {
			temp = getTriggerCount();
			serialWriteString(ltoa(temp, buffer, 10));
			serialWriteString(getString(27));
			serialWriteString(ltoa((temp / 8), buffer, 10));
			serialWriteString(getString(28));
			DebugDone |= 1;
		}

		if ((getSystemTime() - lastChecked) > 150) { // Check button state every 150ms
			audioModeSelected();
			lastChecked = getSystemTime();
		} 
	}

	close();
	return 0;
}

uint8_t audioModeSelected(void) {
	// Pushbutton: PB0, Low active

	if (!(PINB & (1 << PB0))) {
		// Button pushed
		if (lastButtonState < (maxButtonState - 1)) {
			lastButtonState++;
		} else {
			lastButtonState = 0;
		}

	}
	return lastButtonState;
}

uint8_t selfTest(void) {
	uint8_t result = NOERROR;
	
	uint8_t *data = getAudioData();
	if (data == NULL) {
		result |= AUDIOERROR;
	} else {
		free(data);
	}

	data = memGetBytes(0, 1);
	if (data == NULL) {
		result |= MEMORYERROR;
	} else {
		free(data);
	}

	setGeneralPurposeByte(0, 0x23);
	if (getGeneralPurposeByte(0) != 0x23) {
		result |= MEMORYWRITEERROR;
	}

	return result;
}

void printErrors(uint8_t e) {
	if (ISERROR(e, AUDIOERROR)) {
		serialWriteString(getString(3));
	}
	if (ISERROR(e, MEMORYERROR)) {
		serialWriteString(getString(4));
	}
	if (ISERROR(e, MEMORYWRITEERROR)) {
		serialWriteString(getString(5));
	}
}

void randomAnimation(void) {
	uint8_t *b = (uint8_t *)malloc(64);
	uint8_t x, y, z;
	if (b == NULL) {
		serialWriteString(getString(24));
		return;
	}
	for (x = 0; x < 64; x++) {
		b[x] = 0;
	}
	while(1) {
		setImage(b);
		while(isFinished() == 0);
		x = rand() / 4096;
		y = rand() / 4096;
		z = rand() / 4096;
		b[x + (8 * y)] ^= (1 << z);

		if (serialHasChar()) {
			serialWriteString(getString(25));
			free(b);
			serialHandler(serialGet());
			return;
		}
	}
	free(b);
}

void serialHandler(char c) {
	// Used letters:
	// a, b, c, d, e, g, i, n, q, r, s, t, v, x, y, 0, 1, 2, 3, #
	uint8_t i, y, z;

	switch(c) {
	case OK:
		serialWrite(OK);
		break;

	case 'h': case 'H': case '?':
		serialWriteString(getString(6));
		serialWriteString(getString(7));
		serialWriteString(getString(8));
		serialWriteString(getString(9));
		serialWriteString(getString(10));
		serialWriteString(getString(11));
		serialWriteString(getString(12));
		serialWriteString(getString(13));
		serialWriteString(getString(26));
		break;

	case 'd': case 'D':
		clearMem();
		serialWrite(OK);
		break;

	case 'g': case 'G':
		transmitAnimations();
		break;

	case 's': case 'S':
		recieveAnimations();
		break;

	case 'v': case 'V':
		serialWriteString(getString(0));
		break;

	case 'm': case 'M':
		lastButtonState = !lastButtonState;
		if (lastButtonState) {
			serialWriteString(getString(41));
		} else {
			serialWriteString(getString(40));
		}
		break;

	case 'q': case 'Q':
		shouldRestart = 1;
		serialWriteString(getString(30));
		break;

	case 'r': case 'R':
		randomAnimation();
		break;

	case 't': case 'T':
		printTime();
		break;

	case 'a': case 'A':
		sendAudioData();
		break;

	case 'c': case 'C':
		serialWriteString(itoa(getAnimationCount(), buffer, 10));
		serialWriteString(getString(15));
		break;

	case 'x': case 'X':
		// Get byte, store as animation count
		serialWriteString(getString(16));
		while (!serialHasChar());
		c = serialGet();
		setAnimationCount(c);
		serialWriteString(itoa(c, buffer, 10));
		serialWriteString(getString(17));
		break;

	case 'y': case 'Y':
		setAnimationCount(0x2201);
		serialWriteString(getString(18));
		break;

	case 'e': case 'E':
		c = selfTest();
		serialWriteString(getString(19));
		serialWriteString(itoa(c, buffer, 2));
		serialWrite('\n');
		printErrors(c);
		break;

	case 'n': case 'N':
		// snake();
		break;

	case '0':
		fillBuffer(0);
		DebugDone |= 4;
		break;

	case '1':
		fillBuffer(0xFF);
		DebugDone |= 4;
		break;

	case '3':
		setImage(defaultImageCube);
		DebugDone |= 4;
		break;

	case '2':
		DebugDone |= 4;
		fillBuffer(0);
		for (i = 0; i < 64; i++) {
			defaultImageA[i] = 0;
		}
		while(1) {
			for (i = 0; i < 8; i++) {
				for (y = 0; y < 8; y++) {
					defaultImageA[y + (i * 8)] = 0;
					for (z = 0; z < 8; z++) {
						defaultImageA[y + (i * 8)] |= (1 << z);
						setImage(defaultImageA);
						while (isFinished() == 0) {
							wdt_reset();
							if (serialHasChar()) {
								goto killMeForIt; // Yes I know...
								// But I need to break out of 2 while Loops...
							}
						}
					}
					defaultImageA[y + (i * 8)] = 0;
				}
			}
		}
		break;
killMeForIt:
		serialGet();
		serialWriteString(getString(25));
		break;

	case 'I': case 'i':
		serialWriteString(ltoa(getTriggerCount(), buffer, 10));
		serialWrite('\n');
		break;

	default:
		serialWrite(ERROR);
		break;
	}
	// c was used as temp var and does not contain the char anymore...!
}

void printTime(void) {
	serialWriteString(getString(14));
	serialWriteString(ltoa(getSystemTime(), buffer, 10));
	serialWriteString("ms");
	if (getSystemTime() > 60000) {
		serialWriteString(" (");
		serialWriteString(itoa(getSystemTime() / 60000, buffer, 10));
		serialWriteString(" min)");
	}
	if (getSystemTime() > 1000) {
		serialWriteString(" (");
		serialWriteString(itoa(getSystemTime() / 1000, buffer, 10));
		itoa(getSystemTime() % 1000, buffer, 10);
		if (buffer[0] != '\0')
			serialWrite('.');
		if (buffer[2] == '\0') 
			serialWrite('0');
		if (buffer[1] == '\0')
			serialWrite('0');
		if (buffer[0] != '\0')
			serialWriteString(buffer);
		serialWriteString("s)\n");
	} else {
		serialWrite('\n');
	}
}
