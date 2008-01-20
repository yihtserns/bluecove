/**
 * BlueCove BlueZ module - Java library for Bluetooth on Linux
 *  Copyright (C) 2008 Mina Shokry
 *  Copyright (C) 2007 Vlad Skarzhevskyy
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @version $Id$
 */
#define CPP__FILE "BlueCoveBlueZ_L2CAP.cc"

#include "BlueCoveBlueZ.h"
#include <bluetooth/l2cap.h>

#include <unistd.h>
#include <errno.h>
#include <sys/poll.h>

bool l2Get_options(JNIEnv* env, jlong handle, l2cap_options* opt);

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2OpenClientConnectionImpl
  (JNIEnv* env, jobject, jint deviceDescriptor, jlong address, jint channel, jboolean authenticate, jboolean encrypt, jint receiveMTU, jint transmitMTU, jint timeout) {
    debug("CONNECT connect, psm %d", channel);

    sockaddr_l2 localAddr;
	int error = hci_read_bd_addr(deviceDescriptor, &localAddr.l2_bdaddr, LOCALDEVICE_ACCESS_TIMEOUT);
	if (error != 0) {
        throwBluetoothStateException(env, "Bluetooth Device is not ready. %d", error);
	    return 0;
	}

    // allocate socket
    int handle = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (handle < 0) {
        throwIOException(env, "Failed to create socket. [%d] %s", errno, strerror(errno));
        return 0;
    }

    //bind local address
    localAddr.l2_family = AF_BLUETOOTH;
    localAddr.l2_psm = 0;
    //We used deviceDescriptor to get local address for selected device
    //bacpy(&localAddr.l2_bdaddr, BDADDR_ANY);

    if (bind(handle, (sockaddr *)&localAddr, sizeof(localAddr)) < 0) {
		throwIOException(env, "Failed to bind socket. [%d] %s", errno, strerror(errno));
		close(handle);
		return 0;
	}

	// Set link mtu and security options
    l2cap_options opt;
    socklen_t opt_len = sizeof(opt);
    memset(&opt, 0, opt_len);
    opt.imtu = receiveMTU;
    opt.omtu = (transmitMTU > 0)?transmitMTU:L2CAP_DEFAULT_MTU;
    opt.flush_to = L2CAP_DEFAULT_FLUSH_TO;
    Edebug("L2CAP set imtu %i, omtu %i", opt.imtu, opt.omtu);

    if (setsockopt(handle, SOL_L2CAP, L2CAP_OPTIONS, &opt, opt_len) < 0) {
        throwIOException(env, "Failed to set L2CAP mtu options. [%d] %s", errno, strerror(errno));
        close(handle);
        return 0;
    }

    if (encrypt || authenticate) {
		int socket_opt = 0;
		socklen_t len = sizeof(socket_opt);
        if (getsockopt(handle, SOL_L2CAP, L2CAP_LM, &socket_opt, &len) < 0) {
            throwIOException(env, "Failed to read L2CAP link mode. [%d] %s", errno, strerror(errno));
            close(handle);
            return 0;
        }
		//if (master) {
		//	socket_opt |= L2CAP_LM_MASTER;
		//}
		if (authenticate) {
			socket_opt |= L2CAP_LM_AUTH;
			Edebug("L2CAP set authenticate");
		}
		if (encrypt) {
			socket_opt |= L2CAP_LM_ENCRYPT;
		}

		if ((socket_opt != 0) && setsockopt(handle, SOL_L2CAP, L2CAP_LM, &socket_opt, sizeof(socket_opt)) < 0) {
			throwIOException(env, "Failed to set L2CAP link mode. [%d] %s", errno, strerror(errno));
            close(handle);
            return 0;
		}
    }


	sockaddr_l2 remoteAddr;
    remoteAddr.l2_family = AF_BLUETOOTH;
	longToDeviceAddr(address, &remoteAddr.l2_bdaddr);
	remoteAddr.l2_psm = channel;

    // connect to server
    if (connect(handle, (sockaddr*)&remoteAddr, sizeof(remoteAddr)) != 0) {
        throwIOException(env, "Failed to connect. [%d] %s", errno, strerror(errno));
        close(handle);
        return 0;
    }
    debug("L2CAP connected, handle %li", handle);

    l2cap_options copt;
    if (!l2Get_options(env, handle, &copt)) {
        close(handle);
        return 0;
    }
    debug("L2CAP imtu %i, omtu %i", copt.imtu, copt.omtu);
    return handle;
}


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2CloseClientConnection
  (JNIEnv* env, jobject, jlong handle) {
    debug("L2CAP disconnect, handle %li", handle);
    // Closing channel, further sends and receives will be disallowed.
    if (shutdown(handle, SHUT_RDWR) < 0) {
        debug("shutdown failed. [%d] %s", errno, strerror(errno));
    }
    if (close(handle) < 0) {
        throwIOException(env, "Failed to close socket. [%d] %s", errno, strerror(errno));
    }
}

bool l2Get_options(JNIEnv* env, jlong handle, l2cap_options* opt) {
    socklen_t opt_len = sizeof(*opt);
    if (getsockopt(handle, SOL_L2CAP, L2CAP_OPTIONS, opt, &opt_len) < 0) {
        throwIOException(env, "Failed to get L2CAP link mtu. [%d] %s", errno, strerror(errno));
        return false;
    }
    return true;
}


JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2Ready
  (JNIEnv* env, jobject, jlong handle) {
    pollfd fds;
    int timeout = 10; // milliseconds
    fds.fd = handle;
	fds.events = POLLIN | POLLHUP | POLLERR | POLLRDHUP;
	fds.revents = 0;
	if (poll(&fds, 1, timeout) > 0) {
	    if (fds.revents & POLLIN) {
	        return JNI_TRUE;
	    } else if (fds.revents & (POLLHUP | POLLERR | POLLRDHUP)) {
	        throwIOException(env, "Peer closed connection");
	    }
	}
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2Receive
  (JNIEnv* env, jobject, jlong handle, jbyteArray inBuf) {
    l2cap_options opt;
    if (!l2Get_options(env, handle, &opt)) {
        return 0;
    }

    jbyte *bytes = env->GetByteArrayElements(inBuf, 0);
	size_t inBufLen = (size_t)env->GetArrayLength(inBuf);
	int readLen = inBufLen;
	if (readLen > opt.imtu) {
		readLen = opt.imtu;
	}

	int count = recv(handle, (char *)bytes, readLen, 0);
    if (count < 0) {
	    throwIOException(env, "Failed to read. [%d] %s", errno, strerror(errno));
		count = 0;
	}

	env->ReleaseByteArrayElements(inBuf, bytes, 0);
	debug("receive[] returns %i", count);
	return count;
}

JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2Send
  (JNIEnv* env, jobject, jlong handle, jbyteArray data) {
    jbyte *bytes = env->GetByteArrayElements(data, 0);
	int len = (int)env->GetArrayLength(data);
	l2cap_options opt;
    if (!l2Get_options(env, handle, &opt)) {
        return;
    }
    if (len > opt.omtu) {
		len = opt.omtu;
	}
	int count = send(handle, (char *)bytes, len, 0);
	if (count < 0) {
		throwIOException(env, "Failed to write. [%d] %s", errno, strerror(errno));
	}
	env->ReleaseByteArrayElements(data, bytes, 0);
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2GetReceiveMTU
  (JNIEnv* env, jobject, jlong handle) {
    l2cap_options opt;
    if (l2Get_options(env, handle, &opt)) {
        return opt.imtu;
    } else {
        return 0;
    }
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2GetTransmitMTU
  (JNIEnv* env, jobject, jlong handle) {
    l2cap_options opt;
    if (l2Get_options(env, handle, &opt)) {
        return opt.omtu;
    } else {
        return 0;
    }
}

JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2RemoteAddress
(JNIEnv* env, jobject, jlong handle) {
    sockaddr_l2 remoteAddr;
    socklen_t len = sizeof(remoteAddr);
    if (getpeername(handle, (sockaddr*)&remoteAddr, &len) < 0) {
        throwIOException(env, "Failed to get peer name. [%d] %s", errno, strerror(errno));
		return -1;
	}
    return deviceAddrToLong(&remoteAddr.l2_bdaddr);
}

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothStackBlueZ_l2GetSecurityOpt
  (JNIEnv* env, jobject, jlong handle, jint) {
    int socket_opt = 0;
    socklen_t len = sizeof(socket_opt);
    if (getsockopt(handle, SOL_L2CAP, L2CAP_LM, &socket_opt, &len) < 0) {
        throwIOException(env, "Failed to get L2CAP link mode. [%d] %s", errno, strerror(errno));
        return 0;
    }
    bool encrypted = socket_opt &  (L2CAP_LM_ENCRYPT | L2CAP_LM_SECURE);
    bool authenticated = socket_opt & L2CAP_LM_AUTH;
    if (authenticated) {
		return NOAUTHENTICATE_NOENCRYPT;
	}
	if (encrypted) {
		return AUTHENTICATE_ENCRYPT;
	} else {
		return AUTHENTICATE_NOENCRYPT;
	}
}