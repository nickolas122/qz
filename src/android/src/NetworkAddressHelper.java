package org.cagnulen.qdomyoszwift;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.os.Build;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;
import java.util.List;

import org.cagnulen.qdomyoszwift.QLog;

/**
 * This device's own IPv4 address, for the mDNS A record.
 *
 * Qt cannot find it on a modern Android. QNetworkInterface goes through netlink, and on Android 16
 * it logs "QNetworkInterface/AF_NETLINK: found unknown interface with index N" for every interface
 * on the device and returns nothing usable - so QZ published an mDNS service whose A record carried
 * no address at all. A training app browsing for it sees the service, asks where it is, and is told
 * nothing; from the rider's side that is indistinguishable from QZ not being there.
 *
 * WifiManager.getConnectionInfo().getIpAddress(), which the C++ tried next, has returned 0.0.0.0
 * since Android 10 for anything that is not the foreground Wi-Fi settings app.
 *
 * Both problems are Qt's and the old API's rather than the platform's: java.net.NetworkInterface
 * enumerates perfectly well from Java. So the address comes from here, preferring what
 * ConnectivityManager says the active network is actually using.
 */
public class NetworkAddressHelper {
    private static final String TAG = "NetworkAddressHelper";

    /**
     * @return dotted-quad IPv4 address, or an empty string if this device has none.
     */
    public static String getLocalIpv4(Context context) {
        String address = fromActiveNetwork(context);
        if (address == null) {
            address = fromInterfaces();
        }

        if (address == null) {
            QLog.e(TAG, "no usable IPv4 address on this device - mDNS cannot advertise one");
            return "";
        }

        QLog.d(TAG, "local IPv4 address: " + address);
        return address;
    }

    /**
     * The address of the network Android is actually routing through. Preferred because a device
     * with Wi-Fi and a VPN, or Wi-Fi and a hotspot, has several and only one of them is the one a
     * training app on the same network can reach.
     */
    private static String fromActiveNetwork(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return null; // getActiveNetwork() is API 23; older devices use the enumeration below
        }
        try {
            ConnectivityManager cm =
                (ConnectivityManager) context.getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) {
                return null;
            }
            Network network = cm.getActiveNetwork();
            if (network == null) {
                return null;
            }
            LinkProperties link = cm.getLinkProperties(network);
            if (link == null) {
                return null;
            }
            for (LinkAddress linkAddress : link.getLinkAddresses()) {
                String usable = usableIpv4(linkAddress.getAddress());
                if (usable != null) {
                    return usable;
                }
            }
        } catch (Exception e) {
            // A missing ACCESS_NETWORK_STATE arrives as a SecurityException. Not fatal: the
            // enumeration below needs no permission at all.
            QLog.w(TAG, "active network lookup failed: " + e);
        }
        return null;
    }

    /**
     * Every interface, Wi-Fi first. Needs no permission and no Qt.
     */
    private static String fromInterfaces() {
        String fallback = null;
        try {
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface networkInterface : interfaces) {
                if (!networkInterface.isUp() || networkInterface.isLoopback()) {
                    continue;
                }
                for (InetAddress address : Collections.list(networkInterface.getInetAddresses())) {
                    String usable = usableIpv4(address);
                    if (usable == null) {
                        continue;
                    }
                    // wlan0 is what a training app on the same network can reach. Anything else -
                    // rmnet (mobile data), tun (VPN), ap (hotspot) - is kept only as a last resort.
                    if (networkInterface.getName().startsWith("wlan")) {
                        return usable;
                    }
                    if (fallback == null) {
                        fallback = usable;
                    }
                }
            }
        } catch (Exception e) {
            QLog.e(TAG, "interface enumeration failed: " + e);
        }
        return fallback;
    }

    /**
     * @return the address as a string if it is one a peer could actually connect to, else null.
     */
    private static String usableIpv4(InetAddress address) {
        if (!(address instanceof Inet4Address)) {
            return null;
        }
        if (address.isLoopbackAddress() || address.isLinkLocalAddress() || address.isAnyLocalAddress()) {
            return null;
        }
        return address.getHostAddress();
    }
}
