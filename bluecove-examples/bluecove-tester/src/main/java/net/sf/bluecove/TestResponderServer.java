/**
 *  BlueCove - Java library for Bluetooth
 *  Copyright (C) 2006-2007 Vlad Skarzhevskyy
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
package net.sf.bluecove;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Calendar;
import java.util.Date;
import java.util.Enumeration;
import java.util.Iterator;
import java.util.Vector;

import javax.bluetooth.BluetoothStateException;
import javax.bluetooth.DataElement;
import javax.bluetooth.DiscoveryAgent;
import javax.bluetooth.LocalDevice;
import javax.bluetooth.ServiceRecord;
import javax.bluetooth.ServiceRegistrationException;
import javax.bluetooth.UUID;
import javax.microedition.io.Connector;
import javax.microedition.io.StreamConnection;
import javax.microedition.io.StreamConnectionNotifier;

import net.sf.bluecove.awt.JavaSECommon;
import net.sf.bluecove.util.BluetoothTypesInfo;
import net.sf.bluecove.util.CountStatistic;
import net.sf.bluecove.util.IOUtils;
import net.sf.bluecove.util.StringUtils;
import net.sf.bluecove.util.TimeStatistic;
import net.sf.bluecove.util.TimeUtils;

import junit.framework.Assert;

public class TestResponderServer implements CanShutdown, Runnable {
	
	public static int countSuccess = 0; 
	
	public static TimeStatistic allServerDuration = new TimeStatistic(); 
	
	public static FailureLog failure = new FailureLog("Server failure");
	
	public static int countConnection = 0;
	
	public static int countRunningConnections = 0;
	
	public static int concurrentConnectionsMax = 0;
	
	public Thread thread;
	
	private long lastActivityTime;
	
	private boolean stoped = false;
	
	boolean isRunning = false;
	
	public static boolean discoverable = false;
	
	public static long discoverableStartTime = 0;
	
	public static long connectorOpenTime = 0;
	
	private StreamConnectionNotifier serverConnection;
	
	private TestTimeOutMonitor monitorServer;
	
	private Vector concurrentConnectionThreads = new Vector();
	
	public static CountStatistic concurrentStatistic = new CountStatistic();
	
	public static TimeStatistic connectionDuration = new TimeStatistic(); 
	
	private class ServerConnectionTread extends Thread {
		
		long connectionStartTime;
		
		int concurrentCount = 0;
		
		StreamConnectionHolder c = new StreamConnectionHolder();
		
		boolean isRunning = true;
		
		ServerConnectionTread(StreamConnection conn) {
			//CLDC_1_0 super("ServerConnectionTread" + (++countConnection));
			++countConnection;
			
			c.conn = conn;
			connectionStartTime = System.currentTimeMillis();
			synchronized (concurrentConnectionThreads) {
				concurrentConnectionThreads.addElement(this);
			}
		}
		
		private void concurrentNotify() {
			synchronized (concurrentConnectionThreads) {
				int concurNow = concurrentConnectionThreads.size();
				setConcurrentCount(concurNow);
				if (concurNow > 1) {
					// Update all other working Threads
					for (Enumeration iter = concurrentConnectionThreads.elements(); iter.hasMoreElements();) {
						ServerConnectionTread t = (ServerConnectionTread) iter.nextElement();
						t.setConcurrentCount(concurNow);
					}
				}
			}
		}
		
		private void setConcurrentCount(int concurNow) {
			if (concurrentCount < concurNow) {
				concurrentCount = concurNow;
			}
		}
		
		private void runEcho(InputStream is, char firstChar) {
			int receivedCount = 1;
			StringBuffer buf = new StringBuffer();
			buf.append(firstChar);
			try {
				c.os = c.conn.openOutputStream();
				OutputStream os = c.os;
				int i;
				while ((i = is.read()) != -1) {
					receivedCount ++;
					c.os.write(i);
					char c = (char)i;
					if ((c == '\n') || (buf.length() > 40)) {
						Logger.debug("|" + buf.toString());
						os.flush();
						buf = new StringBuffer();
					} else {
						buf.append(c);
					}
				}
			} catch (Throwable e) {
				Logger.debug("echo error", e);
			} finally {
				if (buf.length() != 0) {
					Logger.debug("|" + buf.toString());
				}
				Logger.debug("echo received " + receivedCount);
			}
		}
		
		public void run() {
			int testType = 0;
			TestStatus testStatus = new TestStatus();
			TestTimeOutMonitor monitorConnection = null;
			try {
				c.is = c.conn.openInputStream();
				
				countRunningConnections ++;
				concurrentNotify();
				if (concurrentConnectionsMax < countRunningConnections) {
					concurrentConnectionsMax = countRunningConnections;
					Logger.info("now connected:" + countRunningConnections);
				}
				
				int isTest = c.is.read();
				if (isTest == -1) {
					Logger.debug("EOF recived");
					return;
				}
				if (isTest != Consts.SEND_TEST_START) {
					Logger.debug("not a test client connected, will echo");
					runEcho(c.is, (char)isTest);
					return;
				}
				testType = c.is.read();

				if (testType == Consts.TEST_SERVER_TERMINATE) {
					Logger.info("Stop requested");
					shutdown();
					return;
				}
				testStatus.setName(testType);
				Logger.debug("run test# " + testType);
				monitorConnection = new TestTimeOutMonitor("test" + testType, c, Configuration.serverTestTimeOutSec);
				c.os = c.conn.openOutputStream();
				c.active();
				CommunicationTester.runTest(testType, true, c.conn, c.is, c.os, testStatus);
				if (!testStatus.streamClosed) {
					Logger.debug("reply OK");
					c.active();
					c.os.write(Consts.SEND_TEST_REPLY_OK);
					c.os.write(testType);
					c.os.flush();
				}
				monitorConnection.finish();
				countSuccess++;
				Logger.debug("Test# " + testType + " " + testStatus.getName() + " ok");
				if (!stoped) {
					try {
						Thread.sleep(Configuration.serverSleepB4ClosingConnection);
					} catch (InterruptedException e) {
					}
				}
			} catch (Throwable e) {
				if (!stoped) {
					failure.addFailure("test " + testType  + " " + testStatus.getName(), e);
				}
				Logger.error("Test# " + testType  + " " + testStatus.getName() + " error", e);
			} finally {
				if (monitorConnection != null) {
					monitorConnection.finish();
				}
				synchronized (concurrentConnectionThreads) {
					concurrentConnectionThreads.removeElement(this);
				}
				countRunningConnections --;
				concurrentStatistic.add(concurrentCount);
				connectionDuration.add(TimeUtils.since(connectionStartTime));
				
				IOUtils.closeQuietly(c.is);
				IOUtils.closeQuietly(c.os);
				IOUtils.closeQuietly(c.conn);
				isRunning = false;
				synchronized (this) {
					notifyAll();
				}
			}
			Logger.info("*Test Success:" + countSuccess + " Failure:" + failure.countFailure);
		}
		
	}
	
	public TestResponderServer() throws BluetoothStateException {
		
		LocalDevice localDevice = LocalDevice.getLocalDevice();
		Logger.info("address:" + localDevice.getBluetoothAddress());
		Logger.info("name:" + localDevice.getFriendlyName());
		Logger.info("class:" + BluetoothTypesInfo.toString(localDevice.getDeviceClass()));
		
		Assert.assertNotNull("BT Address", localDevice.getBluetoothAddress());
		if (!Configuration.windowsCE) {
			Assert.assertNotNull("BT Name", localDevice.getFriendlyName());
		}
		if (StringUtils.isStringSet(LocalDevice.getProperty("bluecove"))) {
			Configuration.isBlueCove = true;
		}
		Configuration.stackWIDCOMM = StringUtils.equalsIgnoreCase("WIDCOMM", LocalDevice.getProperty("bluecove.stack"));
	}
	
	public void run() {
		stoped = false;
		isRunning = true;
		if (!Configuration.serverContinuous) {
			lastActivityTime = System.currentTimeMillis();
			monitorServer = new TestTimeOutMonitor("ServerUp", this, Consts.serverUpTimeOutSec);
		}
		try {
			LocalDevice localDevice = LocalDevice.getLocalDevice();
			if ((localDevice.getDiscoverable() == DiscoveryAgent.NOT_DISCOVERABLE) || (Configuration.testServerForceDiscoverable)) {
				if (!setDiscoverable()) {
					return;
				}
			}
			
			serverConnection = (StreamConnectionNotifier) Connector
					.open("btspp://localhost:"
							+ CommunicationTester.uuid
							+ ";name="
							+ Consts.RESPONDER_SERVERNAME
							//;authenticate=false;encrypt=false
							+ ";authorize=false");

			connectorOpenTime = System.currentTimeMillis();
			Logger.info("ResponderServer started " + TimeUtils.timeNowToString());
			if (Configuration.testServiceAttributes) {
				ServiceRecord record = LocalDevice.getLocalDevice().getRecord(serverConnection);
				if (record == null) {
					Logger.warn("Bluetooth ServiceRecord is null");
				} else {
					String initial = BluetoothTypesInfo.toString(record);
					boolean printAllVersion = true;
					if (printAllVersion) {
						Logger.debug("ServiceRecord\n" + initial);
					}
					buildServiceRecord(record);
					try {
						localDevice.updateRecord(record);
						Logger.debug("ServiceRecord updated\n" + BluetoothTypesInfo.toString(record));
					} catch (Throwable e) {
						if (!printAllVersion) {
							Logger.debug("ServiceRecord\n" + initial);
						}
						Logger.error("Service Record update error", e);
					}
				}
			}
			boolean showServiceRecordOnce = true;
			while (!stoped) {
				if ((countConnection % 5 == 0) && (Configuration.testServiceAttributes)) {
					// Problems on SE
					//updateServiceRecord();
				}
				Logger.info("Accepting connection");
				StreamConnection conn = serverConnection.acceptAndOpen();
				if (!stoped) {
					Logger.info("Received connection");
					if (countConnection % 5 == 0) {
						Logger.debug("Server up time " + TimeUtils.secSince(connectorOpenTime));
						Logger.debug("max concurrent con " + concurrentConnectionsMax);
					}
					if (showServiceRecordOnce) {
						Logger.debug("ServiceRecord\n" + BluetoothTypesInfo.toString(LocalDevice.getLocalDevice().getRecord(serverConnection)));
						showServiceRecordOnce = false;
					}
					lastActivityTime = System.currentTimeMillis();
					ServerConnectionTread t = new ServerConnectionTread(conn);
					t.start();
					if (!Configuration.serverAcceptWhileConnected) {
						while (t.isRunning) {
							 synchronized (t) {
								 try {
									t.wait();
								} catch (InterruptedException e) {
									break;
								}
							 }
						}
					}
				} else {
					IOUtils.closeQuietly(conn);
				}
				Switcher.yield(this);
			}

			closeServer();
		} catch (Throwable e) {
			if (!stoped) {
				Logger.error("Server start error", e);
			}
		} finally {
			Logger.info("Server finished! " + TimeUtils.timeNowToString());
			isRunning = false;
		}
		if (monitorServer != null) {
			monitorServer.finish();
		}
	}
	
	public static long avgServerDurationSec() {
		return allServerDuration.avgSec();
	}

	public boolean hasRunningConnections() {
		return (countRunningConnections > 0);
	}
	
	public long lastActivityTime() {
		return lastActivityTime;
		
	}
	
	public static void clear() {
		countSuccess = 0;
		countConnection = 0;
		concurrentConnectionsMax = 0;
		allServerDuration.clear();
		failure.clear();
		concurrentStatistic.clear();
		connectionDuration.clear(); 
	}
	
	private void closeServer() {
		if (serverConnection != null) {
			synchronized (this) {
				try {
					if (serverConnection != null) {
						serverConnection.close();
					}
					Logger.debug("serverConnection closed");
				} catch (Throwable e) {
					Logger.error("Server stop error", e);
				}
			}
			serverConnection = null;
		}
		setNotDiscoverable();
	}

	public static boolean setDiscoverable() {
		try {
			LocalDevice localDevice = LocalDevice.getLocalDevice();
			localDevice.setDiscoverable(DiscoveryAgent.GIAC);
			Logger.debug("Set Discoverable");
			discoverable = true;
			discoverableStartTime = System.currentTimeMillis();
			return true;
		} catch (Throwable e) {
			Logger.error("Start server error", e);
			return false;
		}
	}
	
	public static void setNotDiscoverable() {
		try {
			allServerDuration.add(TimeUtils.since(discoverableStartTime));
			LocalDevice localDevice = LocalDevice.getLocalDevice();
			localDevice.setDiscoverable(DiscoveryAgent.NOT_DISCOVERABLE);
			Logger.debug("Set Not Discoverable");
			discoverable = false;
		} catch (Throwable e) {
			Logger.error("Stop server error", e);
		}
	}
	
	public void shutdown() {
		Logger.info("shutdownServer");
		stoped = true;
		if (Configuration.cldcStub != null) {
			Configuration.cldcStub.interruptThread(thread);
		}
		closeServer();
	}
	
	public void updateServiceRecord() {
		if (serverConnection == null) {
			return;
		}
		try {
			ServiceRecord record = LocalDevice.getLocalDevice().getRecord(serverConnection);
			if (record != null) {
				updateVariableServiceRecord(record);
				LocalDevice.getLocalDevice().updateRecord(record);
			}
		} catch (Throwable e) {
			Logger.error("updateServiceRecord", e);
		}
	}
	
	private void updateVariableServiceRecord(ServiceRecord record) {
//		long data;
//		
//		Calendar calendar = Calendar.getInstance();
//        calendar.setTime(new Date());
//        data = 1 + calendar.get(Calendar.MINUTE);
//        
//		record.setAttributeValue(Consts.VARIABLE_SERVICE_ATTRIBUTE_BYTES_ID,
//		        new DataElement(DataElement.U_INT_4, data));
	}
	
    private void buildServiceRecord(ServiceRecord record) throws ServiceRegistrationException {
        String id = "";
    	try {
			if (Configuration.testAllServiceAttributes) {
				id = "all";
				ServiceRecordTester.addAllTestServiceAttributes(record);
				return;
			}

    		id = "pub";
			buildServiceRecordPub(record);
			id = "int";
			record.setAttributeValue(Consts.TEST_SERVICE_ATTRIBUTE_INT_ID,
			        new DataElement(Consts.TEST_SERVICE_ATTRIBUTE_INT_TYPE, Consts.TEST_SERVICE_ATTRIBUTE_INT_VALUE));
			id = "long";
			record.setAttributeValue(Consts.TEST_SERVICE_ATTRIBUTE_LONG_ID,
			        new DataElement(Consts.TEST_SERVICE_ATTRIBUTE_LONG_TYPE, Consts.TEST_SERVICE_ATTRIBUTE_LONG_VALUE));
			if (!Configuration.testIgnoreNotWorkingServiceAttributes) {
				id = "str";
				record.setAttributeValue(Consts.TEST_SERVICE_ATTRIBUTE_STR_ID, new DataElement(DataElement.STRING,
						Consts.TEST_SERVICE_ATTRIBUTE_STR_VALUE));
			}
			id = "url";
			record.setAttributeValue(Consts.TEST_SERVICE_ATTRIBUTE_URL_ID,
			        new DataElement(DataElement.URL, Consts.TEST_SERVICE_ATTRIBUTE_URL_VALUE));
			
			id = "bytes";
			record.setAttributeValue(Consts.TEST_SERVICE_ATTRIBUTE_BYTES_ID,
			        new DataElement(Consts.TEST_SERVICE_ATTRIBUTE_BYTES_TYPE, Consts.TEST_SERVICE_ATTRIBUTE_BYTES_VALUE));
			
			id = "variable";
			updateVariableServiceRecord(record);
			
			id = "info";
			record.setAttributeValue(Consts.SERVICE_ATTRIBUTE_BYTES_SERVER_INFO,
					new DataElement(DataElement.URL, ServiceRecordTester.getBTSystemInfo()));
			
			id = "update";
			//LocalDevice.getLocalDevice().updateRecord(record);
			
		} catch (Throwable e) {
			Logger.error("ServiceRecord " + id, e);
		}
    }
    
    public void setAttributeValue(ServiceRecord record, int attrID, DataElement attrValue) {
        try {
            if (!record.setAttributeValue(attrID, attrValue)) {
                Logger.error("SrvReg attrID=" + attrID);
            }
        } catch (Exception e) {
            Logger.error("SrvReg attrID=" + attrID, e);
        }
    }
    
    public void buildServiceRecordPub(ServiceRecord record) throws ServiceRegistrationException {
        final short UUID_PUBLICBROWSE_GROUP = 0x1002;
        final short ATTR_BROWSE_GRP_LIST = 0x0005;
        // Add the service to the 'Public Browse Group'
        DataElement browseClassIDList = new DataElement(DataElement.DATSEQ);
        UUID browseClassUUID = new UUID(UUID_PUBLICBROWSE_GROUP);
        browseClassIDList.addElement(new DataElement(DataElement.UUID, browseClassUUID));
        setAttributeValue(record, ATTR_BROWSE_GRP_LIST, browseClassIDList);
    }
    
	public static void main(String[] args) {
		JavaSECommon.initOnce();
		try {
			(new TestResponderServer()).run();
			if (TestResponderServer.failure.countFailure > 0) {
				System.exit(1);
			} else {
				System.exit(0);
			}
		} catch (Throwable e) {
			Logger.error("start error ", e);
			System.exit(1);
		}
	}


}
