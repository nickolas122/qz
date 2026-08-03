package org.cagnulen.qdomyoszwift;

import android.app.Notification;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.app.PendingIntent;
import android.graphics.Color;
import android.graphics.BitmapFactory;
import android.app.NotificationChannel;

public class NotificationClient
{
	 private static NotificationManager m_notificationManager;
	 private static Notification.Builder m_builder;
         private static Context _context;
         private static Intent serviceIntent = null;

         private static final String EXTRA_FOREGROUND_SERVICE_TYPE = "FOREGROUND_SERVICE_TYPE";
         private static final String EXTRA_DAEMON_MODE = "DAEMON_MODE";
         private static final int FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE = 0x10;

	 public NotificationClient() {}

	 public static void notify(Context context, String message) {
                  start(context, message, false);
	 }

         /**
          * Same foreground service, but it also holds a partial wake lock. Used by daemon mode, which
          * brings the service up at startup instead of waiting for a device to connect, so discovery
          * keeps running while the tablet sits idle.
          */
         public static void notifyDaemon(Context context, String message) {
                  start(context, message, true);
         }

         private static void start(Context context, String message, boolean daemonMode) {
                  // The service outlives any single Activity instance, so don't pin its context to one.
                  _context = context.getApplicationContext();
                  serviceIntent = new Intent(_context, ForegroundService.class);
                  serviceIntent.putExtra("inputExtra", message);
                  serviceIntent.putExtra(EXTRA_FOREGROUND_SERVICE_TYPE,
                                         FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
                  serviceIntent.putExtra(EXTRA_DAEMON_MODE, daemonMode);

		  if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
			   _context.startForegroundService(serviceIntent);
		  } else {
		      _context.startService(serviceIntent);
		  }
	 }

         public static void hide() {
             if(serviceIntent != null)
                _context.stopService(serviceIntent);
         }
}
