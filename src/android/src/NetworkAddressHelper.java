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
            // No Wi-Fi, no hotspot, no Ethernet: nothing on another machine can reach this phone,
            // and mobile data was refused above because advertising a carrier-NAT address is worse
            // than advertising none. But a training app *on this phone* can still reach it, over
            // loopback, and that is a supported way to ride - both apps on the one device, no
            // network at all. So loopback is the last resort rather than giving up: it is right
            // for the only client that could possibly connect in this situation, and no client
            // that could be misled by it exists.
            QLog.d(TAG, "no locally reachable address - falling back to loopback, which serves a " +
                        "training app running on this same device");
            return "127.0.0.1";
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
