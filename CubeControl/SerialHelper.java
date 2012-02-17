/*
 * SerialHelper.java
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
 /**
 * Implement commands of cube. You can only open one serial port.
 * If you want to communicate with another port, close this one first!
 * @author Thomas Buck
 * @author Max Nuding
 * @author Felix Bäder
 * @version 1.0
 */

import java.util.Date;
import java.util.ArrayList;
import java.util.List;

public class SerialHelper {

	private final short OK = 0x42;
	private final short ERROR = 0x23;

	private Frame frame;

	/**
	 * Create new SerialHelper.
	 * @param serialPort Name of serial port to use.
	 * @param frame Frame to show error messages 
	 * @throws Exception Could not open serial port.
	 */
	public SerialHelper(String serialPort, Frame frame) throws Exception {
		if (HelperUtility.openPort(serialPort) == false) {
			printErrorMessage("Could not open serial port \"" + serialPort + "\"");
			throw new Exception("Could not open serial port \"" + serialPort + "\"");
		}
		this.frame = frame;
	}

	/**
	 * Poll to check if the cube is there...
	 * @return TRUE if cube is connected.
	 */
	public boolean probeForCube() {
		short[] data = new short[1];
		data[0] = OK;
		if (!writeData(data)) {
			printErrorMessage("Timeout Probing for Cube");
			return false;
		}
		data = readData(1);
		if ((data == null) || (data[0] != OK)) {
			printErrorMessage("No response from cube!");
			return false;
		}
		return true;
	}

	/**
	 * Recieve all animations saved in cube.
	 * @return A cubeWorker populated with the new data or null.
	 */
	public cubeWorker getAnimationsFromCube() {
		List<Animation> animations = new ArrayList<Animation>();
		int animationCount, frameCount;
		short[] data, tmp = new short[1];

		// Send download command
		tmp[0] = 'g';
		if (!writeData(tmp)) {
			printErrorMessage("Timout Command");
			return null;
		}
		data = readData(1);
		if ((data == null) || (data[0] != OK)) {
			printErrorMessage("Response Error");
			return null;
		}

		// Get animation count
		data = readData(1);
		if (data == null) {
			printErrorMessage("Response Error");
			return null;
		} else {
			animationCount = data[0];
		}
		tmp[0] = OK;
		if (!writeData(tmp)) {
			printErrorMessage("Timout Response");
			return null;
		}

		// Get animations
		for (int a = 0; a < animationCount; a++) {
			Animation currentAnim = new Animation();

			// Get number of frames
			data = readData(1);
			if (data == null) {
				printErrorMessage("Response Error");
				return null;
			} else {
				frameCount = data[0];
			}
			tmp[0] = OK;
			if (!writeData(tmp)) {
				printErrorMessage("Timout Response");
				return null;
			}

			// Get frames
			for (int f = 0; f < frameCount; f++) {
				AFrame currentFrame = new AFrame();

				// Get frame duration
				data = readData(1);
				if (data == null) {
					printErrorMessage("Response Error");
					return null;
				} else {
					currentFrame.setTime(data[0]);
				}
				tmp[0] = OK;
				if (!writeData(tmp)) {
					printErrorMessage("Timout Response");
					return null;
				}

				// Get frame data
				data = readData(64);
				if (data == null) {
					printErrorMessage("Response Error");
					return null;
				} else {
					currentFrame.setData(data);
				}
				tmp[0] = OK;
				if (!writeData(tmp)) {
					printErrorMessage("Timout Response");
					return null;
				}

				// Add frame to animation
				currentAnim.add(f, currentFrame);
			}

			// Add animation to animations list
			animations.add(a, currentAnim);
		}

		return new cubeWorker(animations, frame);
	}

	/**
	 * Send all animations in a cubeWorker to the Cube.
	 * @param worker cubeWorker that containts data.
	 * @return 0 on success. -1 on error.
	 */
	public int sendAnimationsToCube(cubeWorker worker) {
		short[] data, tmp = new short[1];

		// Send upload command
		tmp[0] = 's';
		if (!writeData(tmp)) {
			printErrorMessage("Timout Command");
			return -1;
		}
		data = readData(1);
		if ((data == null) || (data[0] != OK)) {
			printErrorMessage("Response Error");
			return -1;
		}

		// Send animation count
		tmp[0] = (short)worker.numOfAnimations();
		if (!writeData(tmp)) {
			printErrorMessage("Timeout numOfAnimations");
			return -1;
		}
		data = readData(1);
		if ((data == null) || (data[0] != OK)) {
			printErrorMessage("Response Error");
			return -1;
		}

		// Send animations
		for (int a = 0; a < worker.numOfAnimations(); a++) {
			// Send frame count
			tmp[0] = (short)worker.numOfFrames(a);
			if (!writeData(tmp)) {
				printErrorMessage("Timeout numOfFrames");
				return -1;
			}
			data = readData(1);
			if ((data == null) || (data[0] != OK)) {
				printErrorMessage("Response Error");
				return -1;
			}

			// Send frames
			for (int f = 0; f < worker.numOfFrames(a); f++) {
				// Frame duration
				tmp[0] = worker.getFrameTime(a, f);
				if (!writeData(tmp)) {
					printErrorMessage("Timeout Frame duration");
					return -1;
				}
				data = readData(1);
				if ((data == null) || (data[0] != OK)) {
					printErrorMessage("Response Error");
					return -1;
				}

				// Frame data
				if (!writeData(worker.getFrame(a, f))) {
					printErrorMessage("Timeout Frame");
					return -1;
				}
				data = readData(1);
				if ((data == null) || (data[0] != OK)) {
					printErrorMessage("Response Error");
					return -1;
				}
			}
		}

		// Send finish
		tmp = new short[4];
		tmp[0] = OK;
		tmp[1] = OK;
		tmp[2] = OK;
		tmp[3] = OK;
		if (!writeData(tmp)) {
			printErrorMessage("Timeout Finish");
			return -1;
		}
		data = readData(1);
		if ((data == null) || (data[0] != OK)) {
			printErrorMessage("Response Error");
			return -1;
		}
		return 0;
	}

	/**
	 * Close the serial port again.
	 */
	public void closeSerialPort() {
		HelperUtility.closePort();
	}

	private void printErrorMessage(String s) {
		System.out.println("SerialHelper: " + s);
		frame.errorMessage("Serial Error", s);
	}

	private boolean writeData(short[] data) {
		// write data. return true if success
		long startdate = (new Date()).getTime();

		SerialWriteThread t = new SerialWriteThread(data);
		t.start();
		while (!t.dataWasSent()) {
			if ((new Date()).getTime() >= (startdate + (data.length * 1000))) {
				// More than (length * 1000) milliseconds went by
				return false;
			}
		}
		return true;
	}

	private short[] readData(int length) {
		// return data read or null if timeout
		long startdate = (new Date()).getTime();

		SerialReadThread t = new SerialReadThread(length);
		t.start();
		while (!t.dataIsReady()) {
			if ((new Date()).getTime() >= (startdate + (length * 1000))) {
				// More than (length * 1000) milliseconds went by
				return null;
			}
		}
		return t.getSerialData();
	}
}