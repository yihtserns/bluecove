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
package com.intel.bluetooth;

import java.util.Hashtable;
import java.util.Vector;

import javax.bluetooth.BluetoothStateException;
import javax.bluetooth.DiscoveryListener;
import javax.bluetooth.RemoteDevice;
import javax.bluetooth.ServiceRecord;
import javax.bluetooth.UUID;

class SearchServicesThread extends Thread {

	private static int transIDGenerator = 0;

	private static Hashtable threads = new Hashtable();

	private SearchServicesRunnable stack;

	private int transID;

	private int[] attrSet;

	private Vector servicesRecords = new Vector();

	UUID[] uuidSet;

	private RemoteDevice device;

	private DiscoveryListener listener;

	private BluetoothStateException startException;

	private boolean started = false;

	private boolean finished = false;

	private boolean terminated = false;

	private Object serviceSearchStartedEvent = new Object();

	private SearchServicesThread(SearchServicesRunnable stack, int[] attrSet, UUID[] uuidSet, RemoteDevice device,
			DiscoveryListener listener) {
		super("SearchServicesThread");
		this.stack = stack;
		this.transID = (++transIDGenerator);
		this.attrSet = attrSet;
		this.listener = listener;
		this.uuidSet = uuidSet;
		this.device = device;
	}

	/**
	 * Start Services Search and wait for startException or
	 * searchServicesStartedCallback
	 */
	static int startSearchServices(SearchServicesRunnable stack, int[] attrSet, UUID[] uuidSet, RemoteDevice device,
			DiscoveryListener listener) throws BluetoothStateException {
		SearchServicesThread t = (new SearchServicesThread(stack, attrSet, uuidSet, device, listener));
		// In case the BTStack hangs, exit JVM anyway
		UtilsJavaSE.threadSetDaemon(t);
		synchronized (t.serviceSearchStartedEvent) {
			t.start();
			while (!t.started && !t.finished) {
				try {
					t.serviceSearchStartedEvent.wait();
				} catch (InterruptedException e) {
					return 0;
				}
				if (t.startException != null) {
					throw t.startException;
				}
			}
		}
		if (t.started) {
			threads.put(new Integer(t.getTransID()), t);
			return t.getTransID();
		} else {
			// This is arguable accoding to JSR-82 we can probably return 0...
			throw new BluetoothStateException();
		}
	}

	static SearchServicesThread getServiceSearchThread(int transID) {
		return (SearchServicesThread) threads.get(new Integer(transID));
	}

	public void run() {
		int respCode = DiscoveryListener.SERVICE_SEARCH_ERROR;
		try {
			respCode = stack.runSearchServices(this, attrSet, uuidSet, device, listener);
		} catch (BluetoothStateException e) {
			startException = e;
			return;
		} finally {
			finished = true;
			threads.remove(new Integer(getTransID()));
			synchronized (serviceSearchStartedEvent) {
				serviceSearchStartedEvent.notifyAll();
			}
			DebugLog.debug("runSearchServices ends", getTransID());
			if (started) {
				Utils.j2meUsagePatternDellay();
				listener.serviceSearchCompleted(getTransID(), respCode);
			}
		}
	}

	public void searchServicesStartedCallback() {
		DebugLog.debug("searchServicesStartedCallback", getTransID());
		started = true;
		synchronized (serviceSearchStartedEvent) {
			serviceSearchStartedEvent.notifyAll();
		}
	}

	int getTransID() {
		return this.transID;
	}

	boolean setTerminated() {
		if (isTerminated()) {
			return false;
		}
		terminated = true;
		threads.remove(new Integer(getTransID()));
		return true;
	}

	boolean isTerminated() {
		return terminated;
	}

	RemoteDevice getDevice() {
		return this.device;
	}

	DiscoveryListener getListener() {
		return this.listener;
	}

	void addServicesRecords(ServiceRecord servRecord) {
		this.servicesRecords.add(servRecord);
	}

	Vector getServicesRecords() {
		return this.servicesRecords;
	}

	public int[] getAttrSet() {
		final int[] requiredAttrIDs = new int[] { BluetoothConsts.ServiceRecordHandle,
				BluetoothConsts.ServiceClassIDList, BluetoothConsts.ServiceRecordState, BluetoothConsts.ServiceID,
				BluetoothConsts.ProtocolDescriptorList };
		if (this.attrSet == null) {
			return requiredAttrIDs;
		}
		// Append unique attributes from attrSet
		int len = requiredAttrIDs.length + this.attrSet.length;
		for (int i = 0; i < this.attrSet.length; i++) {
			for (int k = 0; k < requiredAttrIDs.length; k++) {
				if (requiredAttrIDs[k] == this.attrSet[i]) {
					len--;
					break;
				}
			}
		}

		int[] allIDs = new int[len];
		System.arraycopy(requiredAttrIDs, 0, allIDs, 0, requiredAttrIDs.length);
		int appendPosition = requiredAttrIDs.length;
		nextAttribute: for (int i = 0; i < this.attrSet.length; i++) {
			for (int k = 0; k < requiredAttrIDs.length; k++) {
				if (requiredAttrIDs[k] == this.attrSet[i]) {
					continue nextAttribute;
				}
			}
			allIDs[appendPosition] = this.attrSet[i];
			appendPosition++;
		}
		return allIDs;
	}

}