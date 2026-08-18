package org.cagnulen.qdomyoszwift;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
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
            // Worth being loud about: on mobile data with no Wi-Fi and no hotspot there is no
            // address any training app could reach, so DIRCON cannot be discovered at all. That
            // is the network's shape rather than a fault, and the log should say which it is.
            QLog.e(TAG, "no locally reachable IPv4 address - is this device on mobile data only? " +
                        "DIRCON needs the training app on the same Wi-Fi, or this phone's hotspot");
            return "";
        }

        QLog.d(TAG, "local IPv4 address: " + address);
        return address;
    }

    /**
     * The address of the network Android is actually routing through - but only if that network
     * is one a peer on the same premises could reach.
     *
     * The default network on a phone with no Wi-Fi is mobile data, and its address is behind the
     * carrier's NAT: nothing on the rider's home network can open a connection to it, and an mDNS
     * A record carrying it is worse than none at all. So cellular is refused here and the
     * enumeration below gets its turn, where a hotspot or a USB tether can still be found.
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

            NetworkCapabilities caps = cm.getNetworkCapabilities(network);
            if (caps == null) {
                return null;
            }
            // A VPN is not refused on its transport alone - it reports the transport it runs over -
            // so it is excluded explicitly. Its address belongs to the tunnel, not to this link.
            boolean local = (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                             caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) &&
                            !caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN);
            if (!local) {
                QLog.d(TAG, "the active network is not Wi-Fi or Ethernet - looking for a local one instead");
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
     * Every interface, best first. Needs no permission and no Qt.
     *
     * Ranked rather than first-wins because a phone routinely has several at once, and only some
     * of them are on a link the training app shares. A hotspot counts: if the rider connected the
     * PC to the phone, that is the link they are both on.
     */
    private static String fromInterfaces() {
        String best = null;
        int bestScore = 0;

        try {
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface networkInterface : interfaces) {
                if (!networkInterface.isUp() || networkInterface.isLoopback()) {
                    continue;
                }

                int score = scoreOf(networkInterface.getName());
                if (score == 0 || score <= bestScore) {
                    continue;
                }

                for (InetAddress address : Collections.list(networkInterface.getInetAddresses())) {
                    String usable = usableIpv4(address);
                    if (usable != null) {
                        best = usable;
                        bestScore = score;
                        break;
                    }
                }
            }
        } catch (Exception e) {
            QLog.e(TAG, "interface enumeration failed: " + e);
        }
        return best;
    }

    /**
     * @return how reachable an interface's address is from a training app, 0 meaning never.
     */
    private static int scoreOf(String name) {
        if (name.startsWith("wlan")) {
            return 4; // the ordinary case: same Wi-Fi as the training app
        }
        if (name.startsWith("ap") || name.startsWith("swlan") || name.startsWith("softap")) {
            return 3; // the phone is the hotspot and the training app joined it
        }
        if (name.startsWith("eth") || name.startsWith("usb") || name.startsWith("rndis")) {
            return 2; // Ethernet, or USB tethering
        }
        if (name.startsWith("rmnet") || name.startsWith("ccmni") || name.startsWith("pdp") ||
            name.startsWith("tun") || name.startsWith("ppp")) {
            // Mobile data sits behind the carrier's NAT and a VPN address belongs to the tunnel.
            // Neither can be connected to from the rider's network, so advertising one sends a
            // training app off to knock on a stranger's door.
            return 0;
        }
        return 1;
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
