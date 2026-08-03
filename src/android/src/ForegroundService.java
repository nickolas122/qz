package org.cagnulen.qdomyoszwift;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import androidx.core.app.NotificationCompat;
import android.content.pm.ServiceInfo;
import org.cagnulen.qdomyoszwift.QLog;

public class ForegroundService extends Service {
	 public static final String CHANNEL_ID = "ForegroundServiceChannel";
         private static final String EXTRA_FOREGROUND_SERVICE_TYPE = "FOREGROUND_SERVICE_TYPE";
         private static final String EXTRA_DAEMON_MODE = "DAEMON_MODE";
         private static final int FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE = 0x10;

         // Daemon mode keeps the CPU alive with the screen off, so the Qt process can carry on
         // scanning and, once connected, re-broadcasting while the tablet is idle. A partial lock
         // does not touch the screen: KeepAwakeHelper stays in charge of that.
         private PowerManager.WakeLock daemonWakeLock;

	 @Override
	 public void onCreate() {
		  super.onCreate();
		}
	 @Override
	 public int onStartCommand(Intent intent, int flags, int startId) {
		  String input = (intent != null) ? intent.getStringExtra("inputExtra") : null;
		  if (input == null) {
		      input = "QZ is Running";
		  }
		  createNotificationChannel();
                  Intent notificationIntent = new Intent();
		  PendingIntent pendingIntent = PendingIntent.getActivity(this,
		          0, notificationIntent, PendingIntent.FLAG_IMMUTABLE);
					Notification notification = new NotificationCompat.Builder(this, CHANNEL_ID)
					 .setContentTitle("QZ is Running")
					 .setContentText(input)
					 .setSmallIcon(R.drawable.icon)
					 .setContentIntent(pendingIntent)
					 .setOngoing(true)
					 .build();

                                        try {
                                             int serviceType = (intent != null)
                                                 ? intent.getIntExtra(EXTRA_FOREGROUND_SERVICE_TYPE, FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
                                                 : FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE;
                                             if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                                                startForeground(1, notification, serviceType);
                                             } else {
                                                startForeground(1, notification);
                                             }
                                         } catch (Exception e) {
                                             QLog.e("ForegroundService", "Failed to start foreground service", e);
                                             return START_NOT_STICKY;
                                         }

                  // Re-issuing the intent with a new inputExtra is how the C++ side refreshes the
                  // notification text (searching -> connected to <bike>), so this runs on every call.
                  if (intent != null && intent.getBooleanExtra(EXTRA_DAEMON_MODE, false)) {
                      acquireDaemonWakeLock();
                  } else {
                      releaseDaemonWakeLock();
                  }

		  //do heavy work on a background thread
		  //stopSelf();
		  return START_NOT_STICKY;
		}
	 @Override
	 public void onDestroy() {
		  releaseDaemonWakeLock();
		  super.onDestroy();
		}

	 @Override
	 public IBinder onBind(Intent intent) {
		  return null;
		}

         private void acquireDaemonWakeLock() {
             if (daemonWakeLock != null && daemonWakeLock.isHeld()) {
                 return;
             }
             try {
                 PowerManager powerManager = (PowerManager) getSystemService(Context.POWER_SERVICE);
                 if (powerManager == null) {
                     QLog.e("ForegroundService", "no PowerManager, daemon mode will sleep with the screen");
                     return;
                 }
                 daemonWakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "QZ:DaemonMode");
                 daemonWakeLock.setReferenceCounted(false);
                 daemonWakeLock.acquire();
                 QLog.d("ForegroundService", "daemon mode: partial wake lock acquired");
             } catch (Exception e) {
                 QLog.e("ForegroundService", "Failed to acquire the daemon wake lock", e);
             }
         }

         private void releaseDaemonWakeLock() {
             if (daemonWakeLock == null) {
                 return;
             }
             try {
                 if (daemonWakeLock.isHeld()) {
                     daemonWakeLock.release();
                     QLog.d("ForegroundService", "daemon mode: partial wake lock released");
                 }
             } catch (Exception e) {
                 QLog.e("ForegroundService", "Failed to release the daemon wake lock", e);
             }
             daemonWakeLock = null;
         }

	 private void createNotificationChannel() {
		  if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			   NotificationChannel serviceChannel = new NotificationChannel(
				        CHANNEL_ID,
						  "Foreground Service Channel",
						  NotificationManager.IMPORTANCE_DEFAULT
						);
				NotificationManager manager = getSystemService(NotificationManager.class);
				manager.createNotificationChannel(serviceChannel);
				}
	 }
}
