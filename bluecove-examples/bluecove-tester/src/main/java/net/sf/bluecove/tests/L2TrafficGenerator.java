/**
 *  BlueCove - Java library for Bluetooth
 *  Copyright (C) 2006-2008 Vlad Skarzhevskyy
 * 
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  @version $Id$
 */
package net.sf.bluecove.tests;

import java.io.IOException;

import net.sf.bluecove.CommunicationTesterL2CAP;
import net.sf.bluecove.Configuration;
import net.sf.bluecove.ConnectionHolderL2CAP;
import net.sf.bluecove.Logger;
import net.sf.bluecove.util.IOUtils;
import net.sf.bluecove.util.TimeStatistic;
import net.sf.bluecove.util.TimeUtils;

/**
 * @author vlads
 * 
 */
public class L2TrafficGenerator {

	public static void trafficGeneratorClientInit(ConnectionHolderL2CAP c, int testType) throws IOException {
		byte sequenceSleep = (byte) (Configuration.tgSleep & 0xFF);
		byte sequenceSize = (byte) (Configuration.tgSize & 0xFF);
		c.channel.send(CommunicationTesterL2CAP.startPrefix(testType, new byte[] { sequenceSleep, sequenceSize }));
	}

	public static void trafficGeneratorWrite(ConnectionHolderL2CAP c, byte[] initialData) throws IOException {
		int sequenceSleep = 100;
		final int sequenceSizeMin = 16;
		int sequenceSize = 77;
		if (initialData != null) {
			if (initialData.length > 1) {
				sequenceSleep = initialData[0] * 10;
			}
			if (initialData.length > 2) {
				sequenceSize = initialData[1];
				if (sequenceSize < sequenceSizeMin) {
					sequenceSize = sequenceSizeMin;
				}
			}
		}

		if (sequenceSleep > 0) {
			Logger.debug("write sleep selected" + sequenceSleep + " msec");
		}
		Logger.debug("write size selected" + sequenceSize + " byte");

		long sequenceSentCount = 0;
		int reportedSize = 0;
		long reported = System.currentTimeMillis();
		try {
			mainLoop: do {
				byte[] data = new byte[sequenceSize];
				for (int i = 1; i < sequenceSize; i++) {
					data[i] = (byte) i;
				}
				IOUtils.long2Bytes(sequenceSentCount, 8, data, 0);
				long sendTime = System.currentTimeMillis();
				IOUtils.long2Bytes(sendTime, 8, data, 8);
				c.channel.send(data);
				sequenceSentCount++;
				reportedSize += sequenceSize;
				c.active();
				long now = System.currentTimeMillis();
				if (now - reported > 5 * 1000) {
					Logger.debug("Sent " + sequenceSentCount + " packet(s) " + TimeUtils.bps(reportedSize, reported));
					reported = now;
					reportedSize = 0;
				}
				if (sequenceSleep > 0) {
					try {
						Thread.sleep(sequenceSleep);
					} catch (InterruptedException e) {
						break mainLoop;
					}
					c.active();
				}
			} while (true);
		} finally {
			Logger.debug("Total " + sequenceSentCount + " packet(s)");
		}
	}

	public static void trafficGeneratorReadStart(final ConnectionHolderL2CAP c, final byte[] initialData) {
		Runnable r = new Runnable() {
			public void run() {
				try {
					trafficGeneratorRead(c, initialData);
				} catch (IOException e) {
					Logger.error("reader", e);
				}
			}
		};
		Thread t = Configuration.cldcStub.createNamedThread(r, "L2tgReciver");
		t.start();
	}

	public static void trafficGeneratorRead(ConnectionHolderL2CAP c, byte[] initialData) throws IOException {
		long sequenceRecivedCount = 0;
		long sequenceRecivedNumberLast = -1;
		long sequenceOutOfOrderCount = 0;
		TimeStatistic delay = new TimeStatistic();
		long reported = System.currentTimeMillis();
		long receiveTimeLast = 0;
		long reportedSize = 0;
		try {
			int receiveMTU = c.channel.getReceiveMTU();
			mainLoop: do {
				while (!c.channel.ready()) {
					try {
						Thread.sleep(100);
					} catch (InterruptedException e) {
						break mainLoop;
					}
				}
				byte[] dataReceived = new byte[receiveMTU];
				int lengthdataReceived = c.channel.receive(dataReceived);
				c.active();
				long receiveTime = System.currentTimeMillis();
				sequenceRecivedCount++;
				long sendTime = 0;
				reportedSize += lengthdataReceived;

				if (lengthdataReceived > 8) {
					long sequenceRecivedNumber = IOUtils.bytes2Long(dataReceived, 0, 8);
					if (sequenceRecivedNumberLast + 1 != sequenceRecivedNumber) {
						sequenceOutOfOrderCount++;
					} else if (lengthdataReceived > 18) {
						sendTime = IOUtils.bytes2Long(dataReceived, 8, 8);
					}
					sequenceRecivedNumberLast = sequenceRecivedNumber;
				} else {
					sequenceOutOfOrderCount++;
				}

				if (receiveTimeLast != 0) {
					delay.add(receiveTimeLast - receiveTime);
					receiveTimeLast = receiveTime;
				}

				long now = receiveTime;
				if (now - reported > 5 * 1000) {
					Logger.debug("Received " + sequenceRecivedCount + "/" + sequenceOutOfOrderCount + "(er) packet(s) "
							+ delay.avg() + " msec");
					Logger.debug("Received " + TimeUtils.bps(reportedSize, reported));
					reported = now;
					reportedSize = 0;
				}

			} while (true);
		} finally {
			Logger.debug("Received  " + sequenceRecivedCount + " packet(s)");
			Logger.debug("Misplaced " + sequenceOutOfOrderCount + " packet(s)");
			Logger.debug(" avg interval " + delay.avg() + " msec");
		}
	}
}