package org.cagnulen.qdomyoszwift;

import android.content.Context;
import android.content.Intent;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.SparseArray;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.DisplayCutout;
import android.graphics.Insets;
import org.qtproject.qt5.android.bindings.QtActivity;

public class CustomQtActivity extends QtActivity {
    private static final String TAG = "CustomQtActivity";
    private static final int DOCUMENT_PICKER_PROFILE_REQUEST_CODE = 4101;
    private static final int DOCUMENT_PICKER_TRAINING_REQUEST_CODE = 4102;
    private static final int DOCUMENT_PICKER_GPX_REQUEST_CODE = 4103;
    private static final int DOCUMENT_PICKER_SETTINGS_REQUEST_CODE = 4104;
    private final SparseArray<String> pendingImportDirectories = new SparseArray<>();

    // Declare the native method that will be implemented in C++
    private static native void onInsetsChanged(int top, int bottom, int left, int right,
                                               int waterfallTop, int waterfallBottom,
                                               int waterfallLeft, int waterfallRight);
    private static native void nativeOnDocumentPicked(int requestCode, int resultCode, String localPath);

    // The gamepad, handed to gamepadcontroller through gamepadandroid. On Windows QZ polls the pad
    // itself, so shifting works while the training app owns the screen; Android has no such route -
    // input goes to the focused app - so the events are taken here instead, and shifting from the
    // pad works while QZ is the app on screen.
    private static native void nativeGamepadButton(int keyCode, boolean down);
    private static native void nativeGamepadAxes(float hatX, float hatY, float leftTrigger, float rightTrigger);
    private static native void nativeGamepadPresence(boolean present, String name);

    private InputManager.InputDeviceListener gamepadListener;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.d(TAG, "onCreate: CustomQtActivity initialized");
        AgeSignalsHelper.requestAgeSignals(this);
        HealthConnectHelper.initialize(this);
        startWatchingGamepads();

        // This tells the OS that we want to handle the display cutout area ourselves
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // This is the core of the new solution. We set a listener on the main view.
        // The OS will call this listener whenever the insets change (e.g., on rotation).
        final View decorView = getWindow().getDecorView();
        decorView.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                final float density = getResources().getDisplayMetrics().density;
                int top = 0;
                int bottom = 0;
                int left = 0;
                int right = 0;
                int waterfallTop = 0;
                int waterfallBottom = 0;
                int waterfallLeft = 0;
                int waterfallRight = 0;

                if (density > 0) {
                    // Use system window insets as primary source
                    top = Math.round(insets.getSystemWindowInsetTop() / density);
                    bottom = Math.round(insets.getSystemWindowInsetBottom() / density);
                    left = Math.round(insets.getSystemWindowInsetLeft() / density);
                    right = Math.round(insets.getSystemWindowInsetRight() / density);

                    // For API 28+, also check display cutout for additional padding
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                        DisplayCutout cutout = insets.getDisplayCutout();
                        if (cutout != null) {
                            // Use the maximum between system window inset and cutout safe inset
                            left = Math.max(left, Math.round(cutout.getSafeInsetLeft() / density));
                            right = Math.max(right, Math.round(cutout.getSafeInsetRight() / density));
                            top = Math.max(top, Math.round(cutout.getSafeInsetTop() / density));
                            bottom = Math.max(bottom, Math.round(cutout.getSafeInsetBottom() / density));

                            // Android 11+ exposes curved waterfall display areas separately from cutouts.
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                                Insets waterfallInsets = cutout.getWaterfallInsets();
                                waterfallLeft = Math.round(waterfallInsets.left / density);
                                waterfallRight = Math.round(waterfallInsets.right / density);
                                waterfallTop = Math.round(waterfallInsets.top / density);
                                waterfallBottom = Math.round(waterfallInsets.bottom / density);
                            }
                        }
                    }
                }

                Log.d(TAG, "onApplyWindowInsets - Top:" + top + " Bottom:" + bottom + " Left:" + left + " Right:" + right);
                Log.d(TAG, "Raw insets - SystemTop:" + insets.getSystemWindowInsetTop() + 
                          " SystemBottom:" + insets.getSystemWindowInsetBottom() + 
                          " SystemLeft:" + insets.getSystemWindowInsetLeft() + 
                          " SystemRight:" + insets.getSystemWindowInsetRight());
                Log.d(TAG, "Stable insets - StableTop:" + insets.getStableInsetTop() + 
                          " StableBottom:" + insets.getStableInsetBottom() + 
                          " StableLeft:" + insets.getStableInsetLeft() + 
                          " StableRight:" + insets.getStableInsetRight());
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    DisplayCutout cutout = insets.getDisplayCutout();
                    if (cutout != null) {
                        Log.d(TAG, "Cutout insets - Top:" + cutout.getSafeInsetTop() + 
                              " Bottom:" + cutout.getSafeInsetBottom() + 
                              " Left:" + cutout.getSafeInsetLeft() + 
                              " Right:" + cutout.getSafeInsetRight());
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                            Insets waterfallInsets = cutout.getWaterfallInsets();
                            Log.d(TAG, "Waterfall insets - Top:" + waterfallInsets.top +
                                  " Bottom:" + waterfallInsets.bottom +
                                  " Left:" + waterfallInsets.left +
                                  " Right:" + waterfallInsets.right);
                        }
                    }
                }

                // Push the new, correct inset values to the C++ layer.
                // Guard against the race where Qt's native library hasn't finished
                // loading yet when Android fires onApplyWindowInsets early (targetSdk>=36
                // forces edge-to-edge, triggering this before QtActivity finishes
                // loading libqdomyos-zwift in its background thread).
                try {
                    onInsetsChanged(top, bottom, left, right, waterfallTop, waterfallBottom, waterfallLeft, waterfallRight);
                } catch (UnsatisfiedLinkError ignored) {
                    // Qt not ready yet; insets will be re-applied once Qt initializes.
                }

                return v.onApplyWindowInsets(insets);
            }
        });
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // onCreate runs before Qt has finished loading the native library, so the presence
        // reported there can be lost. By the time the activity resumes the library is up.
        reportGamepadPresence();
    }

    @Override
    protected void onDestroy() {
        if (gamepadListener != null) {
            InputManager inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
            if (inputManager != null) {
                inputManager.unregisterInputDeviceListener(gamepadListener);
            }
            gamepadListener = null;
        }
        super.onDestroy();
    }

    // -- gamepad ------------------------------------------------------------

    private void startWatchingGamepads() {
        InputManager inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) {
            gamepadListener = new InputManager.InputDeviceListener() {
                @Override
                public void onInputDeviceAdded(int deviceId) { reportGamepadPresence(); }
                @Override
                public void onInputDeviceRemoved(int deviceId) { reportGamepadPresence(); }
                @Override
                public void onInputDeviceChanged(int deviceId) { reportGamepadPresence(); }
            };
            inputManager.registerInputDeviceListener(gamepadListener, null);
        }
        reportGamepadPresence();
    }

    /** Tell the C++ side whether Android currently lists a pad, and what it is called. */
    private void reportGamepadPresence() {
        String name = null;
        for (int deviceId : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(deviceId);
            if (isGamepadDevice(device)) {
                name = device.getName();
                break;
            }
        }
        Log.d(TAG, "gamepad presence: " + (name == null ? "none" : name));
        try {
            nativeGamepadPresence(name != null, name == null ? "" : name);
        } catch (UnsatisfiedLinkError ignored) {
            // Qt not ready yet; onResume reports again once it is.
        }
    }

    private static boolean isGamepadDevice(InputDevice device) {
        if (device == null || device.isVirtual()) {
            return false;
        }
        return isGamepadSource(device.getSources());
    }

    // A pad in a generic mode often calls itself a joystick rather than a gamepad, so both count.
    private static boolean isGamepadSource(int sources) {
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
            || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** The keys gamepadandroid has a slot for. Anything else is left to whoever wanted it. */
    private static boolean isMappedGamepadKey(int keyCode) {
        switch (keyCode) {
        case KeyEvent.KEYCODE_BUTTON_A:
        case KeyEvent.KEYCODE_BUTTON_B:
        case KeyEvent.KEYCODE_BUTTON_X:
        case KeyEvent.KEYCODE_BUTTON_Y:
        case KeyEvent.KEYCODE_BUTTON_L1:
        case KeyEvent.KEYCODE_BUTTON_R1:
        case KeyEvent.KEYCODE_BUTTON_L2:
        case KeyEvent.KEYCODE_BUTTON_R2:
        case KeyEvent.KEYCODE_BUTTON_THUMBL:
        case KeyEvent.KEYCODE_BUTTON_THUMBR:
        case KeyEvent.KEYCODE_BUTTON_START:
        case KeyEvent.KEYCODE_BUTTON_SELECT:
        case KeyEvent.KEYCODE_DPAD_UP:
        case KeyEvent.KEYCODE_DPAD_DOWN:
        case KeyEvent.KEYCODE_DPAD_LEFT:
        case KeyEvent.KEYCODE_DPAD_RIGHT:
            return true;
        default:
            return false;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (isGamepadSource(event.getSource()) && isMappedGamepadKey(event.getKeyCode())) {
            // Android's own auto-repeat is dropped: gamepadcontroller has its own hold-to-repeat,
            // with the delay and rate the rider set, and two repeaters would fight.
            if (event.getRepeatCount() == 0) {
                try {
                    nativeGamepadButton(event.getKeyCode(), event.getAction() == KeyEvent.ACTION_DOWN);
                } catch (UnsatisfiedLinkError ignored) {
                }
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (isGamepadSource(event.getSource()) && event.getAction() == MotionEvent.ACTION_MOVE) {
            // Pads disagree about which axis a trigger is: the gamepad profile says LTRIGGER and
            // RTRIGGER, plenty of pads report BRAKE and GAS instead. Whichever is moving wins.
            float leftTrigger = event.getAxisValue(MotionEvent.AXIS_LTRIGGER);
            if (leftTrigger == 0.0f) {
                leftTrigger = event.getAxisValue(MotionEvent.AXIS_BRAKE);
            }
            float rightTrigger = event.getAxisValue(MotionEvent.AXIS_RTRIGGER);
            if (rightTrigger == 0.0f) {
                rightTrigger = event.getAxisValue(MotionEvent.AXIS_GAS);
            }
            try {
                nativeGamepadAxes(event.getAxisValue(MotionEvent.AXIS_HAT_X),
                                  event.getAxisValue(MotionEvent.AXIS_HAT_Y), leftTrigger, rightTrigger);
            } catch (UnsatisfiedLinkError ignored) {
            }
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (isDocumentPickerRequest(requestCode)) {
            handleDocumentPickerResult(requestCode, resultCode, data);
            return;
        }

        String uriString = "";
        if (data != null && data.getData() != null) {
            uriString = data.getData().toString();
        }
        Log.d(TAG, "onActivityResult passthrough requestCode=" + requestCode + " resultCode=" + resultCode + " uri=" + uriString);
        super.onActivityResult(requestCode, resultCode, data);
    }

    public void openDocumentPicker(String mimeType, int requestCode, String destinationDir) {
        pendingImportDirectories.put(requestCode, destinationDir == null ? "" : destinationDir);
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType((mimeType == null || mimeType.isEmpty()) ? "*/*" : mimeType);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(Intent.createChooser(intent, "Select file"), requestCode);
    }

    // This method is still needed for the QML check
    public static int getApiLevel() {
        return Build.VERSION.SDK_INT;
    }

    private boolean isDocumentPickerRequest(int requestCode) {
        return requestCode == DOCUMENT_PICKER_PROFILE_REQUEST_CODE
            || requestCode == DOCUMENT_PICKER_TRAINING_REQUEST_CODE
            || requestCode == DOCUMENT_PICKER_GPX_REQUEST_CODE
            || requestCode == DOCUMENT_PICKER_SETTINGS_REQUEST_CODE;
    }

    private void handleDocumentPickerResult(int requestCode, int resultCode, Intent data) {
        String destinationDir = pendingImportDirectories.get(requestCode, "");
        pendingImportDirectories.remove(requestCode);

        String action = "";
        int flags = 0;
        int clipItemCount = 0;
        String uriString = "";
        String localPath = "";

        if (data != null) {
            action = data.getAction() == null ? "" : data.getAction();
            flags = data.getFlags();
            if (data.getClipData() != null) {
                clipItemCount = data.getClipData().getItemCount();
            }
            if (data.getData() != null) {
                Uri uri = data.getData();
                uriString = uri.toString();
                if (resultCode == RESULT_OK) {
                    localPath = ContentHelper.importContentToAppDir(this, uri, destinationDir);
                }
            }
        }

        Log.d(TAG, "handleDocumentPickerResult requestCode=" + requestCode
            + " resultCode=" + resultCode
            + " action=" + action
            + " flags=0x" + Integer.toHexString(flags)
            + " clipItems=" + clipItemCount
            + " uri=" + uriString
            + " localPath=" + localPath);
        nativeOnDocumentPicked(requestCode, resultCode, localPath);
    }
}
