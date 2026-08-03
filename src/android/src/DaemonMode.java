package org.cagnulen.qdomyoszwift;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import org.cagnulen.qdomyoszwift.QLog;

/**
 * Helpers for the "always on" tablet setup. A foreground service plus a partial wake lock is not
 * enough on its own: Doze still freezes the app once the tablet has been idle for a while, which
 * stops the BLE scan from ever noticing the bike waking up. The exemption below is what keeps the
 * process schedulable.
 */
public class DaemonMode {

    public static boolean isIgnoringBatteryOptimizations(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            // No Doze before Marshmallow, nothing to opt out of.
            return true;
        }
        try {
            PowerManager powerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
            if (powerManager == null) {
                return false;
            }
            return powerManager.isIgnoringBatteryOptimizations(context.getPackageName());
        } catch (Exception e) {
            QLog.e("DaemonMode", "Failed to read the battery optimization state", e);
            return false;
        }
    }

    /**
     * Shows the system prompt, once, if QZ is still battery-optimized. The user has to accept it;
     * there is no way to grant this silently.
     */
    public static void requestIgnoreBatteryOptimizations(Activity activity) {
        if (activity == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }
        if (isIgnoringBatteryOptimizations(activity)) {
            QLog.d("DaemonMode", "already exempt from battery optimizations");
            return;
        }
        try {
            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + activity.getPackageName()));
            activity.startActivity(intent);
        } catch (Exception e) {
            QLog.e("DaemonMode", "Direct exemption request refused, falling back to the settings list", e);
            // Some OEM ROMs strip the per-package screen; the generic list is better than nothing.
            try {
                activity.startActivity(new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
            } catch (Exception ignored) {
                QLog.e("DaemonMode", "No battery optimization settings screen on this device", ignored);
            }
        }
    }
}
