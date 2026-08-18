package org.cagnulen.qdomyoszwift;

import android.content.Context;
import android.net.wifi.WifiManager;
import org.cagnulen.qdomyoszwift.QLog;

/**
 * The Wi-Fi multicast lock the mDNS responder cannot work without.
 *
 * Android's Wi-Fi driver drops incoming multicast and broadcast frames that are not
 * addressed to this device, to save power. That filtering is on by default and it is
 * absolute: a socket joined to 224.0.0.251 receives nothing, with no error anywhere, so
 * the failure looks like a responder that is running perfectly and simply never asked.
 *
 * QZ's announcements still go out - sending is not filtered - which is why a training app
 * that happens to be listening at the moment QZ starts can find it, and why the same setup
 * fails when the app is started second. A training app that browses, as Rouvy does, sends a
 * query and waits for an answer to it. The query never arrives, so no answer is ever sent.
 *
 * Holding a MulticastLock disables the filtering for as long as it is held. It costs
 * battery, which is the whole reason for the default, so it is taken when the DIRCON
 * endpoint is built and dropped when it goes away rather than held for the life of the app.
 *
 * Needs CHANGE_WIFI_MULTICAST_STATE, which is a normal permission: granted at install with
 * nothing to prompt for.
 */
public class MulticastLockHelper {
    private static final String TAG = "MulticastLockHelper";
    private static final String LOCK_NAME = "qz-dircon-mdns";

    private static WifiManager.MulticastLock lock = null;

    /**
     * @return true if the lock is held when this returns, whoever took it.
     */
    public static synchronized boolean acquire(Context context) {
        if (lock != null && lock.isHeld()) {
            QLog.d(TAG, "multicast lock already held");
            return true;
        }

        try {
            // The application context, not the activity's: the lock outlives whatever
            // happens to be on screen, and holding an activity here would leak it.
            WifiManager wifi =
                (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi == null) {
                QLog.e(TAG, "no WifiManager - multicast reception will stay filtered");
                return false;
            }

            lock = wifi.createMulticastLock(LOCK_NAME);
            // Without this the lock is dropped the moment the reference count reaches
            // zero, which a single release() from anywhere would do.
            lock.setReferenceCounted(false);
            lock.acquire();

            boolean held = lock.isHeld();
            QLog.d(TAG, held ? "multicast lock acquired - mDNS queries can now be received"
                             : "multicast lock did not take");
            return held;
        } catch (Exception e) {
            // A missing permission arrives as a SecurityException. Log it and carry on:
            // DIRCON over TCP still works for a client that already knows the address.
            QLog.e(TAG, "could not acquire the multicast lock: " + e);
            lock = null;
            return false;
        }
    }

    public static synchronized void release() {
        if (lock == null) {
            return;
        }
        try {
            if (lock.isHeld()) {
                lock.release();
                QLog.d(TAG, "multicast lock released");
            }
        } catch (Exception e) {
            QLog.e(TAG, "could not release the multicast lock: " + e);
        }
        lock = null;
    }

    public static synchronized boolean isHeld() {
        return lock != null && lock.isHeld();
    }
}
