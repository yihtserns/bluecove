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

#include "WIDCOMMStack.h"

#ifdef VC6
#define CPP_FILE "WIDCOMMStack.cpp"
#endif

BOOL isWIDCOMMBluetoothStackPresent(JNIEnv *env) {
	HMODULE h = LoadLibrary(WIDCOMM_DLL);
	if (h == NULL) {
		return FALSE;
	}
	FreeLibrary(h);
	return TRUE;
}

#ifdef _BTWLIB

static WIDCOMMStack* stack;
static int openConnections = 0;

void BcAddrToString(wchar_t* addressString, BD_ADDR bd_addr) {
	swprintf_s(addressString, 14, L"%02x%02x%02x%02x%02x%02x",
			 bd_addr[0],
             bd_addr[1],
             bd_addr[2],
             bd_addr[3],
             bd_addr[4],
             bd_addr[5]);
}

jlong BcAddrToLong(BD_ADDR bd_addr) {
	jlong l = 0;
	for (int i = 0; i < BD_ADDR_LEN; i++) {
		l = (l << 8) + bd_addr[i];
	}
	return l;
}

void LongToBcAddr(jlong addr, BD_ADDR bd_addr) {
	for (int i = BD_ADDR_LEN - 1; i >= 0; i--) {
		bd_addr[i] = (UINT8)(addr & 0xFF);
		addr >>= 8;
	}
}

jint DeviceClassToInt(DEV_CLASS devClass) {
	return (((devClass[0] << 8) + devClass[1]) << 8) + devClass[2];
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getLibraryVersion
(JNIEnv *, jobject) {
	return blueCoveVersion();
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_detectBluetoothStack
(JNIEnv *env, jobject) {
	return detectBluetoothStack(env);
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_enableNativeDebug
  (JNIEnv *env, jobject, jclass loggerClass, jboolean on) {
	enableNativeDebug(env, loggerClass, on);
}

WIDCOMMStack::WIDCOMMStack() {
	hEvent = CreateEvent(
            NULL,     // no security attributes
            FALSE,    // auto-reset event
            FALSE,    // initial state is NOT signaled
            NULL);    // object not named
	InitializeCriticalSection(&csCRfCommIf);

	commPool = new ObjectPool(COMMPORTS_POOL_MAX, 1, TRUE);

	discoveryRecHolderCurrent = NULL;
	discoveryRecHolderHold = NULL;
}

DiscoveryRecHolder::DiscoveryRecHolder() {
	sdpDiscoveryRecordsUsed = 0;
}

WIDCOMMStack* createWIDCOMMStack() {
	return new WIDCOMMStack();
}

JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_initializeImpl
(JNIEnv *env, jobject) {
	jboolean rc = JNI_TRUE;
	if (stack == NULL) {
		__try  {
			stack = createWIDCOMMStack();
			if (stack->hEvent == NULL) {
				throwRuntimeException(env, "fails to CreateEvent");
			}
		} __except(GetExceptionCode() == 0xC06D007E) {
			rc = JNI_FALSE;
		}
	}
	return rc;
}

WIDCOMMStack::~WIDCOMMStack() {
	destroy(NULL);
	CloseHandle(hEvent);
	DeleteCriticalSection(&csCRfCommIf);
}

void WIDCOMMStack::destroy(JNIEnv * env) {
	SetEvent(hEvent);
	if (commPool != NULL) {
		delete commPool;
		commPool = NULL;
	}
	if (discoveryRecHolderHold != NULL) {
		delete discoveryRecHolderHold;
		discoveryRecHolderHold = NULL;
	}
	if (discoveryRecHolderCurrent != NULL) {
		delete discoveryRecHolderCurrent;
		discoveryRecHolderCurrent = NULL;
	}
}

void WIDCOMMStack::throwExtendedBluetoothStateException(JNIEnv * env) {
	LPCTSTR msg = NULL;
#ifndef _WIN32_WCE
	WBtRc er = GetExtendedError();
	msg = WBtRcToString(er);
#endif //! _WIN32_WCE
	if (msg != NULL) {
		throwBluetoothStateExceptionExt(env, "WIDCOMM error[%s]", msg);
	} else {
		throwBluetoothStateException(env, "No error code");
	}
}

char* WIDCOMMStack::getExtendedError() {
	static char* noError = "No error code";
    if (stack == NULL) {
		return noError;
	}
	LPCTSTR msg = NULL;
#ifndef _WIN32_WCE
	WBtRc er = stack->GetExtendedError();
	msg = WBtRcToString(er);
#endif //! _WIN32_WCE
	if (msg != NULL) {
		return (char*)msg;
	} else {
		return noError;
	}
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_uninitialize
(JNIEnv *env, jobject) {
	if (stack != NULL) {
		debug("destroy WIDCOMMStack");
		WIDCOMMStack* stackTmp = stack;
		stack = NULL;
		stackTmp->destroy(env);
		delete stackTmp;
	}
}


JNIEXPORT jstring JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getLocalDeviceBluetoothAddress
(JNIEnv *env, jobject peer) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	wchar_t addressString[14];
	#ifndef WIDCOMM_CE30
	struct CBtIf::DEV_VER_INFO info;
	if (!stack->GetLocalDeviceVersionInfo(&info)) {
		stack->throwExtendedBluetoothStateException(env);
		return NULL;
	}
	BcAddrToString(addressString, info.bd_addr);
	#else
	if (!stack->GetLocalDeviceInfo()) {
		stack->throwExtendedBluetoothStateException(env);
		return NULL;
	}
	BcAddrToString(addressString, stack->m_BdAddr);
	#endif
	return env->NewString((jchar*)addressString, (jsize)wcslen(addressString));
}

JNIEXPORT jstring JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getLocalDeviceName
(JNIEnv *env, jobject peer) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	#ifndef WIDCOMM_CE30
	BD_NAME name;
	if (!stack->GetLocalDeviceName(&name)) {
		debugs("WIDCOMM error[%s]", stack->getExtendedError());
		return NULL;
	}
	return env->NewStringUTF((char*)name);
	#else
	return NULL;
	#endif
}

JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_isLocalDevicePowerOn
(JNIEnv *env, jobject peer) {
    if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	#ifndef WIDCOMM_CE30
	return stack->IsDeviceReady();
	#else
	return true;
	#endif
}

JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_isStackServerUp
(JNIEnv *env, jobject peer) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	#ifndef WIDCOMM_CE30
	return stack->IsStackServerUp();
	#else
	return true;
	#endif
}

JNIEXPORT jstring JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getBTWVersionInfo
(JNIEnv *env, jobject peer) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	BT_CHAR p_version[256];
#ifdef _WIN32_WCE
	#ifdef WIDCOMM_CE30
	memcpy(p_version, L"<1.4.0", wcslen(L"<1.4.0"));
	#else
	if (!stack->GetBTWCEVersionInfo(p_version, 256)) {
		return NULL;
	}
	#endif
#else // _WIN32_WCE
	if (!stack->GetBTWVersionInfo(p_version, 256)) {
		return NULL;
	}
#endif // #else // _WIN32_WCE
	return env->NewStringUTF((char*)p_version);
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getDeviceVersion
(JNIEnv *env, jobject) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	#ifndef WIDCOMM_CE30
	CBtIf::DEV_VER_INFO dev_Ver_Info;
	if (!stack->GetLocalDeviceVersionInfo(&dev_Ver_Info)) {
		return -1;
	}
	return dev_Ver_Info.lmp_sub_version;
	#else
	return 0;
	#endif
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getDeviceManufacturer
(JNIEnv *env, jobject) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	#ifndef WIDCOMM_CE30
	CBtIf::DEV_VER_INFO dev_Ver_Info;
	if (!stack->GetLocalDeviceVersionInfo(&dev_Ver_Info)) {
		return -1;
	}
	return dev_Ver_Info.manufacturer;
	#else
	return 0;
	#endif
}

JNIEXPORT jstring JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getRemoteDeviceFriendlyName
(JNIEnv *env, jobject, jlong address) {
	if (stack == NULL) {
		return NULL;
	}
	#ifndef WIDCOMM_CE30
	DEV_CLASS filter_dev_class;
	CBtIf::REM_DEV_INFO dev_info;
	CBtIf::REM_DEV_INFO_RETURN_CODE rc = stack->GetRemoteDeviceInfo(filter_dev_class, &dev_info);
	while (rc == CBtIf::REM_DEV_INFO_SUCCESS) {
		jlong a = BcAddrToLong(dev_info.bda);
		if (a == address) {
			return env->NewStringUTF((char*)dev_info.bd_name);
		}
		rc = stack->GetNextRemoteDeviceInfo(&dev_info);
	}
	#endif
	return NULL;
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getDeviceClassImpl
(JNIEnv *env, jobject) {
	return getDeviceClassByOS(env);
}

// --- Device Inquiry

void WIDCOMMStack::OnDeviceResponded(BD_ADDR bda, DEV_CLASS devClass, BD_NAME bdName, BOOL bConnected) {
	if (stack == NULL) {
		return;
	}
	int nextDevice = deviceRespondedIdx + 1;
	if (nextDevice >= DEVICE_FOUND_MAX) {
		nextDevice = 0;
	}
	deviceResponded[nextDevice].deviceAddr = BcAddrToLong(bda);
    deviceResponded[nextDevice].deviceClass = DeviceClassToInt(devClass);
	memcpy(deviceResponded[nextDevice].bdName, bdName, sizeof(BD_NAME));
	deviceRespondedIdx = nextDevice;
	SetEvent(hEvent);
}

void WIDCOMMStack::OnInquiryComplete(BOOL success, short num_responses) {
	if (stack == NULL) {
		return;
	}
	deviceInquirySuccess = success;
	deviceInquiryComplete = TRUE;
	SetEvent(hEvent);
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_runDeviceInquiryImpl
(JNIEnv * env, jobject peer, jobject startedNotify, jint accessCode, jobject listener) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	debug("StartDeviceInquiry");
	stack->deviceInquiryComplete = FALSE;
	stack->deviceInquiryTerminated = FALSE;

    memset(stack->deviceResponded, 0, sizeof(stack->deviceResponded));
 	stack->deviceRespondedIdx = -1;

	jclass peerClass = env->GetObjectClass(peer);
	if (peerClass == NULL) {
		throwRuntimeException(env, "Fail to get Object Class");
		return INQUIRY_ERROR;
	}

	jmethodID deviceDiscoveredCallbackMethod = env->GetMethodID(peerClass, "deviceDiscoveredCallback", "(Ljavax/bluetooth/DiscoveryListener;JILjava/lang/String;)V");
	if (deviceDiscoveredCallbackMethod == NULL) {
		throwRuntimeException(env, "Fail to get MethodID deviceDiscoveredCallback");
		return INQUIRY_ERROR;
	}

	jclass notifyClass = env->GetObjectClass(startedNotify);
	if (notifyClass == NULL) {
		throwRuntimeException(env, "Fail to get Object Class");
		return INQUIRY_ERROR;
	}
	jmethodID notifyMethod = env->GetMethodID(notifyClass, "deviceInquiryStartedCallback", "()V");
	if (notifyMethod == NULL) {
		throwRuntimeException(env, "Fail to get MethodID deviceInquiryStartedCallback");
		return INQUIRY_ERROR;
	}

	if (!stack->StartInquiry()) {
		debug("deviceInquiryStart error");
		stack->throwExtendedBluetoothStateException(env);
		return INQUIRY_ERROR;
	}
	debug("deviceInquiryStarted");

	env->CallVoidMethod(startedNotify, notifyMethod);
	if (ExceptionCheckCompatible(env)) {
		stack->StopInquiry();
		return INQUIRY_ERROR;
	}

	int reportedIdx = -1;

	while ((stack != NULL) && ((!stack->deviceInquiryComplete) || (reportedIdx != stack->deviceRespondedIdx))) {
		DWORD  rc = WaitForSingleObject(stack->hEvent, 200);
		if (rc == WAIT_FAILED) {
			throwRuntimeException(env, "WaitForSingleObject");
			return INQUIRY_ERROR;
		}
		if ((stack != NULL) && (reportedIdx != stack->deviceRespondedIdx)) {
			reportedIdx ++;
			if (reportedIdx >= DEVICE_FOUND_MAX) {
				reportedIdx = 0;
			}
			DeviceFound dev = stack->deviceResponded[reportedIdx];
			env->CallVoidMethod(peer, deviceDiscoveredCallbackMethod, listener, dev.deviceAddr, dev.deviceClass, env->NewStringUTF((char*)(dev.bdName)));
			if (ExceptionCheckCompatible(env)) {
				stack->StopInquiry();
				return INQUIRY_ERROR;
			}
		}
	}

	if (stack != NULL) {
		stack->StopInquiry();
	}

	if (stack == NULL) {
		return INQUIRY_TERMINATED;
	} else if (stack->deviceInquiryTerminated) {
		return INQUIRY_TERMINATED;
	} else if (stack->deviceInquirySuccess) {
		return INQUIRY_COMPLETED;
	} else {
		return INQUIRY_ERROR;
	}
}

JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_deviceInquiryCancelImpl
(JNIEnv *env, jobject peer) {
	stack->deviceInquiryTerminated = TRUE;
	stack->StopInquiry();
	SetEvent(stack->hEvent);
	return TRUE;
}

// --- Service search

JNIEXPORT jlongArray JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_runSearchServicesImpl
(JNIEnv *env, jobject peer, jobject startedNotify, jbyteArray uuidValue, jlong address) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	debug("StartSearchServices");

	BD_ADDR bda;
	LongToBcAddr(address, bda);

#ifdef EXT_DEBUG
	wchar_t addressString[14];
	BcAddrToString(addressString, bda);
	debugs("StartSearchServices on %S", addressString);
#endif

	GUID *p_service_guid = NULL;
	GUID service_guid;
	//If uuidValue parameter is NULL, all public browseable services for the device will be reported
	if (uuidValue != NULL) {
		jbyte *bytes = env->GetByteArrayElements(uuidValue, 0);
		// build UUID
		convertUUIDBytesToGUID(bytes, &service_guid);
		env->ReleaseByteArrayElements(uuidValue, bytes, 0);
		p_service_guid = &service_guid;
	}
	if (p_service_guid == NULL) {
		debug("p_service_guid is NULL");
	}

	stack->searchServicesComplete = FALSE;
	stack->searchServicesTerminated = FALSE;

	if (!stack->StartDiscovery(bda, p_service_guid)) {
		debug("StartSearchServices error");
		stack->throwExtendedBluetoothStateException(env);
		return NULL;
	}

	jclass notifyClass = env->GetObjectClass(startedNotify);
	if (notifyClass == NULL) {
		throwRuntimeException(env, "Fail to get Object Class");
		return NULL;
	}
	jmethodID notifyMethod = env->GetMethodID(notifyClass, "searchServicesStartedCallback", "()V");
	if (notifyMethod == NULL) {
		throwRuntimeException(env, "Fail to get MethodID searchServicesStartedCallback");
		return NULL;
	}
	env->CallVoidMethod(startedNotify, notifyMethod);
	if (ExceptionCheckCompatible(env)) {
		return NULL;
	}

	while ((stack != NULL) && (!stack->searchServicesComplete) && (!stack->searchServicesTerminated)) {
		DWORD  rc = WaitForSingleObject(stack->hEvent, 500);
		if (rc == WAIT_FAILED) {
			throwRuntimeException(env, "WaitForSingleObject");
			return NULL;
		}
	}
	if (stack == NULL) {
		return NULL;
	}

	if (stack->searchServicesTerminated) {
		throwException(env, "com/intel/bluetooth/SearchServicesTerminatedException", "");
		return NULL;
	}

	UINT16 obtainedServicesRecords;
	CBtIf::DISCOVERY_RESULT searchServicesResultCode = stack->GetLastDiscoveryResult(bda, &obtainedServicesRecords);

	//todo SERVICE_SEARCH_TERMINATED

	if (searchServicesResultCode != CBtIf::DISCOVERY_RESULT_SUCCESS) {
		debugs("searchServicesResultCode %i", searchServicesResultCode);
		return NULL;
	}
	if (obtainedServicesRecords <= 0) {
		return env->NewLongArray(0);
	}

	if (obtainedServicesRecords > SDP_DISCOVERY_RECORDS_DEVICE_MAX) {
		debugs("WARN too many ServicesRecords %i", obtainedServicesRecords);
		//obtainedServicesRecords = SDP_DISCOVERY_RECORDS_USED_MAX;
	}
	debugs("obtainedServicesRecords %i", obtainedServicesRecords);
	// Retrive all Records and filter in Java
	int retriveRecords = SDP_DISCOVERY_RECORDS_DEVICE_MAX;

	// Select RecHolder
	DiscoveryRecHolder* discoveryRecHolder = stack->discoveryRecHolderCurrent;
	if (discoveryRecHolder == NULL) {
		discoveryRecHolder = new DiscoveryRecHolder();
		discoveryRecHolder->oddHolder = TRUE;
		stack->discoveryRecHolderCurrent = discoveryRecHolder;
		Edebug("DiscoveryRecHolder created");
	}

	int useIdx = discoveryRecHolder->sdpDiscoveryRecordsUsed;
	if (useIdx + retriveRecords > SDP_DISCOVERY_RECORDS_USED_MAX) {
		useIdx = 0;
		Edebug("DiscoveryRecHolder switch");
		// Select next RecHolder
		if (stack->discoveryRecHolderHold != NULL) {
			Edebugs("DiscoveryRecHolder delete %p", discoveryRecHolder);
			delete stack->discoveryRecHolderHold;
		}
		BOOL newOdd = !(discoveryRecHolder->oddHolder);
		stack->discoveryRecHolderHold = discoveryRecHolder;
		discoveryRecHolder = new DiscoveryRecHolder();
		discoveryRecHolder->oddHolder = newOdd;
		stack->discoveryRecHolderCurrent = discoveryRecHolder;
		if (newOdd) {
			Edebug("DiscoveryRecHolder created ODD");
		} else {
			Edebug("DiscoveryRecHolder created EVEN");
		}
	} else {
		Edebugs("DiscoveryRecHolder useIdx %i", useIdx);
	}

	Edebugs("discoveryRecHolderHold %p", stack->discoveryRecHolderHold);
	Edebugs("discoveryRecHolderCurrent %p", stack->discoveryRecHolderCurrent);

#ifdef EXT_DEBUG
	wchar_t addressString2[14];
	BcAddrToString(addressString2, bda);
	debugs("ReadDiscoveryRecords on %S", addressString2);
#endif

	CSdpDiscoveryRec *sdpDiscoveryRecordsList = discoveryRecHolder->sdpDiscoveryRecords + useIdx;

	//guid_filter does not work as Expected with SE Phones!
	int recSize = stack->ReadDiscoveryRecords(bda, retriveRecords, sdpDiscoveryRecordsList, NULL);
	if (recSize == 0) {
		debugs("ReadDiscoveryRecords returns empty, While expected min %i", obtainedServicesRecords);
		return NULL;
	}
	Edebugs("DiscoveryRecHolder curr %p", discoveryRecHolder);
	debugs("DiscoveryRecHolder +=recSize %i", recSize);
	discoveryRecHolder->sdpDiscoveryRecordsUsed += recSize;

	int offset = SDP_DISCOVERY_RECORDS_HANDLE_OFFSET + useIdx;
	if (discoveryRecHolder->oddHolder) {
		offset += SDP_DISCOVERY_RECORDS_HOLDER_MARK;
	}

	Edebugs("DiscoveryRecHolder first.h %i", offset);

	jlongArray result = env->NewLongArray(recSize);
	jlong *longs = env->GetLongArrayElements(result, 0);
	for (int r = 0; r < recSize; r ++) {
		longs[r] = offset + r;
	}
	env->ReleaseLongArrayElements(result, longs, 0);

	return result;
}

void WIDCOMMStack::OnDiscoveryComplete() {
	searchServicesComplete = TRUE;
	SetEvent(hEvent);
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_cancelServiceSearchImpl
(JNIEnv *env, jobject) {
	if (stack != NULL)  {
		stack->searchServicesTerminated = TRUE;
		SetEvent(stack->hEvent);
	}
}

CSdpDiscoveryRec* validDiscoveryRec(JNIEnv *env, jlong handle) {
	if (stack == NULL) {
		throwIOException(env, "Invalid DiscoveryRec handle");
		return NULL;
	}
	int offset = (int)handle;
	Edebugs("DiscoveryRecHolder handle %i", offset);
	Edebugs("stack->discoveryRecHolderHold %p", stack->discoveryRecHolderHold);
	Edebugs("stack->discoveryRecHolderCurrent %p", stack->discoveryRecHolderCurrent);

	DiscoveryRecHolder* discoveryRecHolder;
	BOOL heedOdd = (handle > SDP_DISCOVERY_RECORDS_HOLDER_MARK);
	if (heedOdd) {
		offset -= SDP_DISCOVERY_RECORDS_HOLDER_MARK;
	}
	if (stack->discoveryRecHolderCurrent->oddHolder == heedOdd) {
		discoveryRecHolder = stack->discoveryRecHolderCurrent;
		Edebug("DiscoveryRecHolder use ODD");
	} else {
		discoveryRecHolder = stack->discoveryRecHolderHold;
		Edebug("DiscoveryRecHolder use EVEN");
	}
	if (discoveryRecHolder == NULL) {
		throwIOException(env, "Invalid DiscoveryRec holder");
		return NULL;
	}
	if ((offset < SDP_DISCOVERY_RECORDS_HANDLE_OFFSET)
		|| (offset > SDP_DISCOVERY_RECORDS_USED_MAX + SDP_DISCOVERY_RECORDS_HANDLE_OFFSET)) {
		throwIOExceptionExt(env, "Invalid DiscoveryRec handle [%i]", offset);
		return NULL;
	}
	Edebugs("DiscoveryRecHolder used %p", discoveryRecHolder);
	return discoveryRecHolder->sdpDiscoveryRecords + offset - SDP_DISCOVERY_RECORDS_HANDLE_OFFSET;
}

JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_isServiceRecordDiscoverable
(JNIEnv *env, jobject, jlong address, jlong handle) {
	CSdpDiscoveryRec* record = validDiscoveryRec(env, handle);
	if (record == NULL) {
		return JNI_FALSE;
	}
	stack->searchServicesComplete = FALSE;
	stack->searchServicesTerminated = FALSE;

	BD_ADDR bda;
	LongToBcAddr(address, bda);
	if (!stack->StartDiscovery(bda, &(record->m_service_guid))) {
		debugs("StartDiscovery WIDCOMM error[%s]", stack->getExtendedError());
		return JNI_FALSE;
	}

	while ((stack != NULL) && (!stack->searchServicesComplete) && (!stack->searchServicesTerminated)) {
		DWORD  rc = WaitForSingleObject(stack->hEvent, 500);
		if (rc == WAIT_FAILED) {
			return JNI_FALSE;
		}
	}
	if ((stack == NULL) || (stack->searchServicesTerminated)) {
		return JNI_FALSE;
	}

	UINT16 obtainedServicesRecords;
	CBtIf::DISCOVERY_RESULT searchServicesResultCode = stack->GetLastDiscoveryResult(bda, &obtainedServicesRecords);
	if (searchServicesResultCode != CBtIf::DISCOVERY_RESULT_SUCCESS) {
		debugs("isServiceRecordDiscoverable resultCode %i", searchServicesResultCode);
		return JNI_FALSE;
	}
	debugs("isServiceRecordDiscoverable found sr %i", obtainedServicesRecords);
	if (obtainedServicesRecords < 1) {
		return JNI_FALSE;
	}

	return JNI_TRUE;
}

JNIEXPORT jbyteArray JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getServiceAttribute
(JNIEnv *env, jobject peer, jint attrID, jlong handle) {
	CSdpDiscoveryRec* record = validDiscoveryRec(env, handle);
	if (record == NULL) {
		return NULL;
	}

	SDP_DISC_ATTTR_VAL* pval = new SDP_DISC_ATTTR_VAL;

	if (!record->FindAttribute((UINT16)attrID, pval)) {
		// attr not found
		delete pval;
		return NULL;
	}

	jbyteArray result = env->NewByteArray(sizeof(SDP_DISC_ATTTR_VAL));
	jbyte *bytes = env->GetByteArrayElements(result, 0);
	memcpy(bytes, pval, sizeof(SDP_DISC_ATTTR_VAL));
	env->ReleaseByteArrayElements(result, bytes, 0);
	delete pval;
	return result;
}

//	 --- Client RFCOMM connections

// Guarded by CriticalSection csCRfCommIf
WIDCOMMStackRfCommPort* WIDCOMMStack::createCommPort(BOOL server) {
	WIDCOMMStackRfCommPort* port;
    if (server) {
	    port = new WIDCOMMStackRfCommPortServer();
    } else {
        port = new WIDCOMMStackRfCommPort();
    }

	if (!commPool->addObject(port)) {
		delete port;
		return NULL;
	}
	return port;
}

void WIDCOMMStack::deleteCommPort(WIDCOMMStackRfCommPort* commPort) {
	if (commPort == NULL) {
		return;
	}
	commPort->readyToFree = TRUE;
}

WIDCOMMStackRfCommPort::WIDCOMMStackRfCommPort() {

	readyForReuse();

    hConnectionEvent = CreateEvent(
            NULL,     // no security attributes
            FALSE,     // auto-reset event
            FALSE,    // initial state is NOT signaled
            NULL);    // object not named
    hDataReceivedEvent = CreateEvent(
            NULL,     // no security attributes
            FALSE,     // auto-reset event
            FALSE,     // initial state is NOT signaled
            NULL);    // object not named

	openConnections ++;
}

void WIDCOMMStackRfCommPort::readyForReuse() {
	resetReceiveBuffer();
	isConnected = FALSE;
	isConnectionError = FALSE;
	other_event_code = 0;
	isClosing = FALSE;
	readyToFree = FALSE;
	service_name[0] = '\0';
}

void WIDCOMMStackRfCommPort::resetReceiveBuffer() {
	receiveBuffer.reset();
}

WIDCOMMStackRfCommPort::~WIDCOMMStackRfCommPort() {
	if (isConnected) {
		isClosing = TRUE;
		SetEvent(hConnectionEvent);
		Close();
	}
	isConnected = FALSE;

	CloseHandle(hDataReceivedEvent);
	CloseHandle(hConnectionEvent);
	openConnections --;
}

WIDCOMMStackRfCommPort* validRfCommHandle(JNIEnv *env, jlong handle) {
	if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return NULL;
	}
	return (WIDCOMMStackRfCommPort*)stack->commPool->getObject(env, handle);
}

void WIDCOMMStackRfCommPort::OnEventReceived (UINT32 event_code) {
	if ((magic1 != MAGIC_1) || (magic2 != MAGIC_2) || isClosing) {
		return;
	}
	if (PORT_EV_CONNECTED & event_code) {
        isConnected = TRUE;
		SetEvent(hConnectionEvent);
	} else if (PORT_EV_CONNECT_ERR & event_code) {
		isConnectionError = TRUE;
		isConnected = FALSE;
		SetEvent(hConnectionEvent);
	} else if (PORT_EV_OVERRUN & event_code) {
		isConnectionError = TRUE;
		receiveBuffer.setOverflown();
		SetEvent(hConnectionEvent);
	} else {
		other_event_code = event_code;
		SetEvent(hConnectionEvent);
	}
}

void WIDCOMMStackRfCommPort::OnDataReceived(void *p_data, UINT16 len) {
	if ((magic1 != MAGIC_1) || (magic2 != MAGIC_2) || isClosing) {
		return;
	}
	if (isConnected && !isClosing) {
		receiveBuffer.write(p_data, len);
		SetEvent(hDataReceivedEvent);
	}
}

#define open_client_return  open_client_finally(rf); return

void open_client_finally(WIDCOMMStackRfCommPort* rf) {
	if ((rf != NULL) && (stack != NULL)) {
		// Just in case Close
		rf->Close();
		stack->deleteCommPort(rf);
	}
	if (stack != NULL) {
		LeaveCriticalSection(&stack->csCRfCommIf);
	}
}

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfOpenClientConnection
(JNIEnv *env, jobject peer, jlong address, jint channel, jboolean authenticate, jboolean encrypt) {
	BD_ADDR bda;
	LongToBcAddr(address, bda);

	WIDCOMMStackRfCommPort* rf = NULL;
	EnterCriticalSection(&stack->csCRfCommIf);
	//vc6 __try {
		rf = stack->createCommPort(FALSE);
		if (rf == NULL) {
			throwIOException(env, "No free connections Objects in Pool");
			open_client_return 0;
		}
        debugs("RfCommPort handle %i", rf->internalHandle);
		if ((rf->hConnectionEvent == NULL) || (rf->hDataReceivedEvent == NULL)) {
			throwRuntimeException(env, "fails to CreateEvent");
			open_client_return 0;
		}
		//debug("AssignScnValue");
		// What GUID do we need in call to CRfCommIf.AssignScnValue() if we don't have any?
		//memcpy(&(rf->service_guid), &test_client_service_guid, sizeof(GUID));
		if (!stack->rfCommIf.AssignScnValue(&(rf->service_guid), (UINT8)channel)) {
			throwIOException(env, "failed to assign SCN");
			open_client_return 0;
		}
		//debug("SetSecurityLevel");
		UINT8 sec_level = BTM_SEC_NONE;
		BT_CHAR *p_service_name;

		#ifdef _WIN32_WCE
			p_service_name = (BT_CHAR*)L"bluecovesrv";
		#else // _WIN32_WCE
			p_service_name = "bluecovesrv";
		#endif // #else // _WIN32_WCE

		if (!stack->rfCommIf.SetSecurityLevel(p_service_name, sec_level, FALSE)) {
			throwIOException(env, "Error setting security level");
			open_client_return 0;
        }
		//debug("OpenClient");
		CRfCommPort::PORT_RETURN_CODE rc = rf->OpenClient((UINT8)channel, bda);
		if (rc != CRfCommPort::SUCCESS) {
			throwIOException(env, "Failed to OpenClient");
			open_client_return 0;
		}

		//debug("waitFor Connection signal");
		DWORD waitStart = GetTickCount();
		while ((stack != NULL) && !rf->isClosing && !rf->isConnected && !rf->isConnectionError) {
			DWORD  rc = WaitForSingleObject(rf->hConnectionEvent, 500);
			if (rc == WAIT_FAILED) {
				throwRuntimeException(env, "WaitForSingleObject");
				open_client_return 0;
			}
			if ((GetTickCount() - waitStart)  > COMMPORTS_CONNECT_TIMEOUT) {
				throwIOException(env, "Connection timeout");
				open_client_return 0;
			}
		}
		if ((stack == NULL) || rf->isClosing || rf->isConnectionError) {
			throwIOException(env, "Failed to connect");
			open_client_return 0;
		}
		debug("connected");
		//rf->SetFlowEnabled(TRUE);

		jlong handle = rf->internalHandle;
		rf = NULL;
		open_client_return handle;
	/* vc6 } __finally {
		if ((rf != NULL) && (stack != NULL)) {
			// Just in case Close
			rf->Close();
			stack->deleteCommPort(rf);
		}
		LeaveCriticalSection(&stack->csCRfCommIf);
	}*/
}

void WIDCOMMStackRfCommPort::closeRfCommPort(JNIEnv *env) {
	isClosing = TRUE;
	SetEvent(hConnectionEvent);
	debug2("closing RfCommPort [%i], Connected[%s]", internalHandle, bool2str(isConnected));
	Purge(PORT_PURGE_TXCLEAR);
	Purge(PORT_PURGE_RXCLEAR);
	CRfCommPort::PORT_RETURN_CODE rc = Close();
	if (rc != CRfCommPort::SUCCESS && rc != CRfCommPort::NOT_OPENED) {
		throwIOException(env, "Failed to Close");
	} else {
		debug1("closed RfCommPort [%i]", internalHandle);
	}
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_closeRfCommPort
(JNIEnv *env, jobject peer, jlong handle) {
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL) {
		return;
	}
	//debug("CloseClientConnection");
	rf->closeRfCommPort(env);
	// Some worker thread is still trying to access this object, delete later
	if (stack != NULL) {
		stack->deleteCommPort(rf);
	}
	//debugs("connection handles %i", openConnections);
}

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_getConnectionRfRemoteAddress
(JNIEnv *env, jobject peer, jlong handle) {
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL) {
		return 0;
	}
	if (!rf->isConnected || rf->isClosing) {
		throwIOException(env, "Connection is closed");
		return 0;
	}
	BD_ADDR connected_bd_addr;
    if (rf->IsConnected(&connected_bd_addr)) {
		return BcAddrToLong(connected_bd_addr);
	} else {
		throwIOException(env, "Connection down");
		return 0;
	}
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfRead__J
(JNIEnv *env, jobject peer, jlong handle) {
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL) {
		return -1;
	}
	Edebug("->read()");
	if (rf->isClosing) {
		return -1;
	}
	if (rf->receiveBuffer.isOverflown()) {
		throwIOException(env, "Receive buffer overflown");
		return 0;
	}
	HANDLE hEvents[2];
	hEvents[0] = rf->hConnectionEvent;
	hEvents[1] = rf->hDataReceivedEvent;
	while (rf->isConnected && (!rf->isClosing) && (rf->receiveBuffer.available() == 0)) {
		debug("read() waits for data");
		DWORD  rc = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
		if (rc == WAIT_FAILED) {
			throwRuntimeException(env, "WaitForMultipleObjects");
			return 0;
		}
		/*
		if (rf->other_event_code != 0) {
			debug1("connectionEvent %i", rf->other_event_code);
			rf->other_event_code = 0;
		}
		debug2("isClosing [%s], isConnected[%s]", bool2str(rf->isClosing), bool2str(rf->isConnected));
		*/
	}
	if ((rf->isClosing) || (rf->receiveBuffer.available() == 0)) {
		// See InputStream.read();
		return -1;
	}
	return rf->receiveBuffer.readByte();
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfRead__J_3BII
(JNIEnv *env, jobject peer, jlong handle, jbyteArray b, jint off, jint len) {
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL) {
		return -1;
	}
	debug("->read(byte[])");
	if (rf->isClosing) {
		return -1;
	}
	if (rf->receiveBuffer.isOverflown()) {
		throwIOException(env, "Receive buffer overflown");
		return 0;
	}

	jbyte *bytes = env->GetByteArrayElements(b, 0);

	HANDLE hEvents[2];
	hEvents[0] = rf->hConnectionEvent;
	hEvents[1] = rf->hDataReceivedEvent;

	int done = 0;

	while (rf->isConnected && (!rf->isClosing) && (done < len)) {
		while (rf->isConnected  && (!rf->isClosing) && (rf->receiveBuffer.available() == 0)) {
			debug("read[] waits for data");
			DWORD  rc = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
			if (rc == WAIT_FAILED) {
				env->ReleaseByteArrayElements(b, bytes, 0);
				throwRuntimeException(env, "WaitForMultipleObjects");
				return 0;
			}
			debug1("read waits returns %s", waitResultsString(rc));
		}
		int count = rf->receiveBuffer.available();
		if (count > 0) {
			if (count > len - done) {
				count = len - done;
			}
			done += rf->receiveBuffer.read(bytes + off + done, count);
		}
		debug1("read[] received %i", count);
	}
	// Read from not Connected
	int count = rf->receiveBuffer.available();
	if (count > 0) {
		if (count > len - done) {
			count = len - done;
		}
		done += rf->receiveBuffer.read(bytes + off + done, count);
	}

	if ((rf->isClosing) || (!rf->isConnected && done == 0)) {
		// See InputStream.read();
		done = -1;
	}
	env->ReleaseByteArrayElements(b, bytes, 0);
	return done;
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfReadAvailable
(JNIEnv *env, jobject peer, jlong handle) {
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL || rf->isClosing) {
		return 0;
	}
	if (rf->receiveBuffer.isOverflown()) {
		throwIOException(env, "Receive buffer overflown");
	}
	return rf->receiveBuffer.available();
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfWrite__JI
(JNIEnv *env, jobject peer, jlong handle, jint b) {
	Edebug("->write(int)");
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL) {
		return;
	}
	if (!rf->isConnected || rf->isClosing) {
		throwIOException(env, "Failed to write to closed connection");
		return;
	}

	while ((rf->isConnected) && (!rf->isClosing)) {
		UINT8 signal; 
		rf->GetModemStatus(&signal);
		if (signal & PORT_CTSRTS_ON) {
			break;
		}
		Sleep(200);
	}

	char c = (char)b;
	UINT16 written = 0;
	while ((written == 0) && rf->isConnected) {
		CRfCommPort::PORT_RETURN_CODE rc = rf->Write((void*)(&c), 1, &written);
		if (rc != CRfCommPort::SUCCESS) {
			throwIOException(env, "Failed to write");
			return;
		}
		if (written == 0) {
			debug("write(int) write again");
		}
	}
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfWrite__J_3BII
(JNIEnv *env, jobject peer, jlong handle, jbyteArray b, jint off, jint len) {
	debug("->write(byte[])");
	WIDCOMMStackRfCommPort* rf = validRfCommHandle(env, handle);
	if (rf == NULL) {
		return;
	}
	if (!rf->isConnected || rf->isClosing) {
		throwIOException(env, "Failed to write to closed connection");
		return;
	}

	jbyte *bytes = env->GetByteArrayElements(b, 0);

	UINT16 done = 0;

	while ((rf->isConnected) && (!rf->isClosing)) {
		UINT8 signal; 
		rf->GetModemStatus(&signal);
		if (signal & PORT_CTSRTS_ON) {
			break;
		}
		Sleep(200);
	}

	while ((done < len) && rf->isConnected && (!rf->isClosing)) {
		UINT16 written = 0;
		CRfCommPort::PORT_RETURN_CODE rc = rf->Write((void*)(bytes + off + done), (UINT16)(len - done), &written);
		if (rc != CRfCommPort::SUCCESS) {
			env->ReleaseByteArrayElements(b, bytes, 0);
			throwIOException(env, "Failed to write");
			return;
		}
		done += written;
	}

	env->ReleaseByteArrayElements(b, bytes, 0);
}

WIDCOMMStackRfCommPortServer::WIDCOMMStackRfCommPortServer() {
	sdpService = new CSdpService();
	isClientOpen = FALSE;
}

WIDCOMMStackRfCommPortServer::~WIDCOMMStackRfCommPortServer() {
	if (sdpService != NULL) {
		delete sdpService;
		sdpService = NULL;
	}
}

void WIDCOMMStackRfCommPortServer::closeRfCommPort(JNIEnv *env) {
	WIDCOMMStackRfCommPort::closeRfCommPort(env);
	if (sdpService != NULL) {
		delete sdpService;
		sdpService = NULL;
	}
}

#define open_server_return  open_server_finally(env, rf); return

void open_server_finally(JNIEnv *env, WIDCOMMStackRfCommPortServer* rf) {
	if ((rf != NULL) && (stack != NULL)) {
		rf->closeRfCommPort(env);
		stack->deleteCommPort(rf);
	}

	if (stack != NULL) {
		LeaveCriticalSection(&stack->csCRfCommIf);
	}
}

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_rfServerOpenImpl
(JNIEnv *env, jobject peer, jbyteArray uuidValue, jbyteArray uuidValue2, jstring name, jboolean authenticate, jboolean encrypt) {
    if (stack == NULL) {
		throwIOException(env, "Stack closed");
		return 0;
	}
	EnterCriticalSection(&stack->csCRfCommIf);
	WIDCOMMStackRfCommPortServer* rf = NULL;
//VC6	__try {

		rf = (WIDCOMMStackRfCommPortServer*)stack->createCommPort(TRUE);
		if (rf == NULL) {
			throwIOException(env, "No free connections Objects in Pool");
			open_server_return 0;
		}
		const char *cname = env->GetStringUTFChars(name, 0);
		#ifdef _WIN32_WCE
			swprintf_s((wchar_t*)rf->service_name, BT_MAX_SERVICE_NAME_LEN, L"%s", cname);
		#else // _WIN32_WCE
			sprintf_s(rf->service_name, BT_MAX_SERVICE_NAME_LEN, "%s", cname);
		#endif // #else // _WIN32_WCE
		env->ReleaseStringUTFChars(name, cname);

		jbyte *bytes = env->GetByteArrayElements(uuidValue, 0);
		// build UUID
		convertUUIDBytesToGUID(bytes, &(rf->service_guid));
		env->ReleaseByteArrayElements(uuidValue, bytes, 0);

		BOOL assignScnRC;
		#ifdef _WIN32_WCE
			assignScnRC = stack->rfCommIf.AssignScnValue(&(rf->service_guid), (UINT8)0);
		#else // _WIN32_WCE
			assignScnRC = stack->rfCommIf.AssignScnValue(&(rf->service_guid), 0, rf->service_name);
		#endif // #else // _WIN32_WCE

		if (!assignScnRC) {
			throwIOException(env, "failed to assign SCN");
			open_server_return 0;
		}
		rf->scn = stack->rfCommIf.GetScn();


		GUID service_guids[2];
		int service_guids_len = 1;
		memcpy(&(service_guids[0]), &(rf->service_guid), sizeof(GUID));

		if (uuidValue2 != NULL) {
			jbyte *bytes2 = env->GetByteArrayElements(uuidValue2, 0);
			convertUUIDBytesToGUID(bytes2, &(service_guids[1]));
			env->ReleaseByteArrayElements(uuidValue2, bytes2, 0);
			service_guids_len = 2;
		}

		if (rf->sdpService->AddServiceClassIdList(service_guids_len, service_guids) != SDP_OK) {
		//if (rf->sdpService->AddServiceClassIdList(1, &(rf->service_guid)) != SDP_OK) {
			throwIOException(env, "Error AddServiceClassIdList");
			open_server_return 0;
		}

		if (rf->sdpService->AddServiceName(rf->service_name) != SDP_OK) {
			throwIOException(env, "Error AddServiceName");
			open_server_return 0;
		}
		if (rf->sdpService->AddRFCommProtocolDescriptor(rf->scn) != SDP_OK) {
			throwIOException(env, "Error AddRFCommProtocolDescriptor");
			open_server_return 0;
		}
		UINT32 service_name_len;
		#ifdef _WIN32_WCE
			service_name_len = wcslen((wchar_t*)rf->service_name);
		#else // _WIN32_WCE
			service_name_len = strlen(rf->service_name);
		#endif // #else // _WIN32_WCE

		if (rf->sdpService->AddAttribute(0x0100, TEXT_STR_DESC_TYPE, service_name_len, (UINT8*)rf->service_name) != SDP_OK) {
			throwIOException(env, "Error AddAttribute ServiceName");
		}
		debug1("service_name assigned [%s]", rf->service_name);

		if (rf->sdpService->MakePublicBrowseable() != SDP_OK) {
			throwIOException(env, "Error MakePublicBrowseable");
			open_server_return 0;
		}
		rf->sdpService->SetAvailability(255);

		UINT8 sec_level = BTM_SEC_NONE;
		if (!stack->rfCommIf.SetSecurityLevel(rf->service_name, sec_level, TRUE)) {
			throwIOException(env, "Error setting security level");
			open_server_return 0;
        }

		jlong handle = rf->internalHandle;
		rf = NULL;
		open_server_return handle;
/*VC6
	} __finally {
		if ((rf != NULL) && (stack != NULL)) {
			rf->closeRfCommPort(env);
			stack->deleteCommPort(rf);
		}
		LeaveCriticalSection(&stack->csCRfCommIf);
	}
*/
}

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_rfServerAcceptAndOpenRfServerConnection
(JNIEnv *env, jobject, jlong handle) {
	WIDCOMMStackRfCommPortServer* rf = (WIDCOMMStackRfCommPortServer*)validRfCommHandle(env, handle);
	if (rf == NULL) {
		return 0;
	}
	if (rf->sdpService == NULL) {
		throwIOException(env, "Connection closed");
		return 0;
	}

	if (rf->isClientOpen) {
		debug("server waits for client prev connection to close");
		while ((stack != NULL) && (rf->isClientOpen) && (rf->sdpService != NULL)) {
			DWORD  rc = WaitForSingleObject(rf->hConnectionEvent, 500);
			if (rc == WAIT_FAILED) {
				throwRuntimeException(env, "WaitForSingleObject");
				return 0;
			}
		}
		if ((stack == NULL) || (rf->sdpService == NULL)) {
			throwIOException(env, "Connection closed");
			return 0;
		}
		//Sleep(200);
	}

	rf->isConnected = FALSE;
	rf->isConnectionError = FALSE;
	rf->isClosing = FALSE;
	rf->resetReceiveBuffer();

	CRfCommPort::PORT_RETURN_CODE rc = rf->OpenServer(rf->scn);
	if (rc != CRfCommPort::SUCCESS) {
		throwIOException(env, "Failed to OpenServer");
		return 0;
	}
	debug("server waits for connection");
	while ((stack != NULL) && (!rf->isClosing)  && (!rf->isConnected) && (!rf->isConnectionError) && (rf->sdpService != NULL)) {
		DWORD  rc = WaitForSingleObject(rf->hConnectionEvent, 500);
		if (rc == WAIT_FAILED) {
			throwRuntimeException(env, "WaitForSingleObject");
			return 0;
		}
	}

	if ((stack == NULL) || rf->isClosing || rf->isConnectionError || (rf->sdpService == NULL)) {
		if ((stack == NULL) || rf->isClosing || (rf->sdpService == NULL)) {
			throwIOException(env, "Connection closed");
		} else if (rf->isConnectionError) {
			throwIOException(env, "Connection error");
		} else {
			throwIOException(env, "Failed to connect");
		}
		return 0;
	}
	debug("server connection made");
	rf->isClientOpen = TRUE;
	return rf->internalHandle;
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_rfServerSCN
(JNIEnv *env, jobject, jlong handle) {
	WIDCOMMStackRfCommPortServer* rf = (WIDCOMMStackRfCommPortServer*)validRfCommHandle(env, handle);
	if (rf == NULL) {
		return 0;
	}
	return rf->scn;
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_connectionRfCloseServerConnection
(JNIEnv *env, jobject, jlong handle) {
	WIDCOMMStackRfCommPortServer* rf = (WIDCOMMStackRfCommPortServer*)validRfCommHandle(env, handle);
	if ((rf == NULL) || (!rf->isClientOpen)) {
		return;
	}
	rf->isClosing = TRUE;
	SetEvent(rf->hConnectionEvent);
	debugs("closing ServerConnection, Connected[%s]", bool2str(rf->isConnected));
	CRfCommPort::PORT_RETURN_CODE rc = rf->Close();
	if (rc != CRfCommPort::SUCCESS && rc != CRfCommPort::NOT_OPENED) {
		throwIOException(env, "Failed to Close");
	}
	rf->isConnected = FALSE;
	rf->isConnectionError = FALSE;
	rf->isClientOpen = FALSE;
	debug("notify ServerConnection readers");
	SetEvent(rf->hConnectionEvent);
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackWIDCOMM_rfServerAddAttribute
(JNIEnv *env, jobject, jlong handle, jint attrID, jshort attrType, jcharArray value) {
	WIDCOMMStackRfCommPortServer* rf = (WIDCOMMStackRfCommPortServer*)validRfCommHandle(env, handle);
	if (rf == NULL) {
		return;
	}

	jchar *chars = env->GetCharArrayElements(value, 0);
	UINT8 arrLen = (UINT8)env->GetArrayLength(value);
	UINT8 *p_val = new UINT8[arrLen + 1];
	for (UINT8 i = 0; i < arrLen; i++ ) {
		p_val[i] = (UINT8)chars[i];
	}
	p_val[arrLen] = '\0';

	UINT8 attr_len = arrLen;

	env->ReleaseCharArrayElements(value, chars, 0);

	if (rf->sdpService->AddAttribute((UINT16)attrID, (UINT8)attrType, attr_len, p_val) != SDP_OK) {
		throwExceptionExt(env, "javax/bluetooth/ServiceRegistrationException", "Failed to AddAttribute %i", attrID);
	} else {
		debug4("attr set %i type=%i len=%i [%s]", attrID, attrType, attr_len, p_val);
	}

	delete p_val;

}

#endif //  _BTWLIB