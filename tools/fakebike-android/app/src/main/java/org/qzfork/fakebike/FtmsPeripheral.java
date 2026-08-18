package org.qzfork.fakebike;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattServer;
import android.bluetooth.BluetoothGattServerCallback;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.AdvertiseCallback;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.UUID;

/**
 * A Fitness Machine peripheral: advertises, serves the FTMS GATT profile, and streams a
 * {@link RideScenario} as Indoor Bike Data.
 *
 * <p>This is the layer docs/fork/VIRTUAL-BIKE.md defers under "a real peripheral" - the one
 * that needs a radio and therefore cannot run in CI. Everything the Layer C tests cover stops
 * at QZ's own output; this covers the half nothing else reaches: discovery over BLE, the
 * platform Bluetooth backend, connection, subscription, and the bytes QZ writes back.
 *
 * <h2>The name matters more than it looks</h2>
 *
 * QZ picks the {@code ftmsbike} driver from the advertised name, and for this bike the test
 * is exact: {@code bluetooth.cpp} wants a name starting with "YPBM" that is <b>ten
 * characters long</b>. "YPBM123456" is ten. Change it to nine or eleven and QZ will see the
 * device, decline to claim it, and say nothing useful about why.
 *
 * <p>On Android the advertised local name is the adapter's own name, not something an app can
 * set per-advertisement, so {@link #start} renames the adapter and {@link #stop} puts it back.
 * That is a change to the phone, not to this app, and it is why stopping cleanly matters.
 *
 * <h2>What goes on the wire</h2>
 *
 * Indoor Bike Data with flags 0x0264 - instantaneous speed, cadence, resistance level,
 * instantaneous power and heart rate - which is the layout QZ's own virtual bike emits and
 * the one its parser is best exercised on.
 *
 * <p>The real YPBM trainer does something different and more awkward: it splits its data
 * across two notifications per second, one with flags 0x01f5 and one with 0x2a00. Matching
 * that byte for byte is worth doing eventually, because it is a parse path nothing else
 * exercises, but it belongs with Layer B and recorded fixtures rather than here. This
 * peripheral exists to test the radio, and a frame QZ certainly understands is the right
 * thing to test it with.
 */
public class FtmsPeripheral {

    private static final String TAG = "FakeBike";

    /** Ten characters, and QZ counts them. See the class comment. */
    public static final String ADVERTISED_NAME = "YPBM123456";

    private static UUID uuid16(int id) {
        return UUID.fromString(String.format("%08x-0000-1000-8000-00805f9b34fb", id));
    }

    private static final UUID FITNESS_MACHINE_SERVICE = uuid16(0x1826);
    private static final UUID FITNESS_MACHINE_FEATURE = uuid16(0x2ACC);
    private static final UUID INDOOR_BIKE_DATA = uuid16(0x2AD2);
    private static final UUID TRAINING_STATUS = uuid16(0x2AD3);
    private static final UUID SUPPORTED_RESISTANCE_RANGE = uuid16(0x2AD6);
    private static final UUID CONTROL_POINT = uuid16(0x2AD9);
    private static final UUID CCCD = uuid16(0x2902);

    // FTMS control point opcodes, from the enum in src/devices/ftmsbike/ftmsbike.h.
    private static final byte OP_REQUEST_CONTROL = 0x00;
    private static final byte OP_RESET = 0x01;
    private static final byte OP_SET_TARGET_RESISTANCE = 0x04;
    private static final byte OP_SET_TARGET_POWER = 0x05;
    private static final byte OP_START_RESUME = 0x07;
    private static final byte OP_STOP_PAUSE = 0x08;
    private static final byte OP_SET_SIM_PARAMS = 0x11;
    private static final byte OP_RESPONSE_CODE = (byte) 0x80;
    private static final byte RESULT_SUCCESS = 0x01;
    private static final byte RESULT_NOT_SUPPORTED = 0x02;

    /** How often a frame goes out. The real bike notifies once a second. */
    private static final long TICK_MS = 1000;

    public interface Listener {
        void onState(String message);
        void onFrame(double rideSeconds, int watts, double cadence, double speedKmh,
                     int resistance, int heart, boolean silent);
        /** The last thing a client asked for over the control point. */
        void onCommand(String summary);
    }

    private final Context context;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Random random = new Random();

    private BluetoothManager manager;
    private BluetoothGattServer server;
    private BluetoothLeAdvertiser advertiser;
    private BluetoothGattCharacteristic indoorBikeData;
    private BluetoothGattCharacteristic controlPoint;
    private final List<BluetoothDevice> subscribers = new ArrayList<>();
    private final List<BluetoothDevice> controlSubscribers = new ArrayList<>();

    private String previousAdapterName;
    private boolean running;

    // --- ride state, mirroring simulatedbike::update() -----------------------------------
    private RideScenario scenario;
    private double rideSeconds;
    private long lastTickMs;
    private double simulatedWatts;
    private double resistance;
    private double lastSpeed;
    /** -1 when no client has asked for a target power. */
    private double requestedPower = -1;
    private double inclination;
    /** Set once a client writes a resistance, after which the scenario stops setting it. */
    private boolean clientOwnsResistance;
    /** The last control point command, for the status line. */
    private String lastCommand = "";

    public FtmsPeripheral(Context context, Listener listener) {
        this.context = context;
        this.listener = listener;
    }

    public boolean isRunning() { return running; }

    @SuppressLint("MissingPermission")
    public boolean start(RideScenario scenario) {
        if (running) return true;
        this.scenario = scenario;

        manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        if (manager == null || manager.getAdapter() == null) {
            say("no Bluetooth adapter");
            return false;
        }
        if (!manager.getAdapter().isEnabled()) {
            say("Bluetooth is off - turn it on and try again");
            return false;
        }
        advertiser = manager.getAdapter().getBluetoothLeAdvertiser();
        if (advertiser == null) {
            say("this device cannot advertise as a peripheral");
            return false;
        }

        // The advertised local name is the adapter's, so it has to be borrowed. Saved here
        // and restored in stop(); if the app is killed instead, the phone keeps the name and
        // the user has to put it back in Settings.
        previousAdapterName = manager.getAdapter().getName();
        if (!ADVERTISED_NAME.equals(previousAdapterName)) {
            manager.getAdapter().setName(ADVERTISED_NAME);
        }

        // setName() is asynchronous - it goes through binder to the Bluetooth process and is
        // persisted there - and the advertising payload is built from whatever the name is at
        // the moment startAdvertising() runs. Starting immediately is a race, and losing it
        // means advertising under the phone's real name or under none at all. Wait for the
        // stack to agree before going on the air.
        if (!waitForAdapterName(ADVERTISED_NAME, 3000)) {
            say("the adapter would not take the name " + ADVERTISED_NAME
                    + " (it is \"" + manager.getAdapter().getName() + "\") - QZ will not "
                    + "recognise this device");
        }

        if (!openGattServer()) return false;
        startAdvertising();

        rideSeconds = 0;
        simulatedWatts = 0;
        lastSpeed = 0;
        requestedPower = -1;
        inclination = 0;
        clientOwnsResistance = false;
        lastCommand = "";
        resistance = scenario.getStartResistance() >= 0 ? scenario.getStartResistance() : 0;
        lastTickMs = System.currentTimeMillis();
        running = true;
        handler.post(tick);
        return true;
    }

    @SuppressLint("MissingPermission")
    public void stop() {
        if (!running) return;
        running = false;
        handler.removeCallbacks(tick);

        if (advertiser != null) {
            try {
                advertiser.stopAdvertising(advertiseCallback);
            } catch (Exception e) {
                Log.w(TAG, "stopAdvertising: " + e);
            }
        }
        if (server != null) {
            server.close();
            server = null;
        }
        subscribers.clear();
        controlSubscribers.clear();

        // Give the phone its name back. Leaving it as YPBM123456 would be a lasting change
        // made by a test tool, which is not a thing a test tool should do.
        if (manager != null && manager.getAdapter() != null && previousAdapterName != null
                && !ADVERTISED_NAME.equals(previousAdapterName)) {
            manager.getAdapter().setName(previousAdapterName);
        }
        say("stopped");
    }

    // --- GATT ---------------------------------------------------------------------------

    @SuppressLint("MissingPermission")
    private boolean openGattServer() {
        server = manager.openGattServer(context, serverCallback);
        if (server == null) {
            say("could not open a GATT server");
            return false;
        }

        BluetoothGattService service =
                new BluetoothGattService(FITNESS_MACHINE_SERVICE,
                        BluetoothGattService.SERVICE_TYPE_PRIMARY);

        // Fitness Machine Feature: cadence, resistance, heart rate and power measurement,
        // with resistance, power and simulation parameters accepted as targets. QZ reads this
        // to decide what the machine can do - a client that trusts it and finds power missing
        // shows 0 W for the whole session, which is a bug this fork has already been bitten by.
        BluetoothGattCharacteristic feature = new BluetoothGattCharacteristic(
                FITNESS_MACHINE_FEATURE,
                BluetoothGattCharacteristic.PROPERTY_READ,
                BluetoothGattCharacteristic.PERMISSION_READ);
        feature.setValue(new byte[] {
                (byte) 0x82, (byte) 0x44, 0x00, 0x00, // cadence, resistance, HR, power
                (byte) 0x18, (byte) 0x20, 0x00, 0x00  // resistance, power, sim params
        });
        service.addCharacteristic(feature);

        // Supported Resistance Level Range: 1.0 to 32.0 in steps of 1.0, in 0.1 units.
        BluetoothGattCharacteristic range = new BluetoothGattCharacteristic(
                SUPPORTED_RESISTANCE_RANGE,
                BluetoothGattCharacteristic.PROPERTY_READ,
                BluetoothGattCharacteristic.PERMISSION_READ);
        range.setValue(new byte[] {0x0a, 0x00, (byte) 0x40, 0x01, 0x0a, 0x00});
        service.addCharacteristic(range);

        BluetoothGattCharacteristic status = new BluetoothGattCharacteristic(
                TRAINING_STATUS,
                BluetoothGattCharacteristic.PROPERTY_READ,
                BluetoothGattCharacteristic.PERMISSION_READ);
        status.setValue(new byte[] {0x00, 0x01});
        service.addCharacteristic(status);

        indoorBikeData = new BluetoothGattCharacteristic(
                INDOOR_BIKE_DATA,
                BluetoothGattCharacteristic.PROPERTY_NOTIFY, 0);
        indoorBikeData.addDescriptor(newCccd());
        service.addCharacteristic(indoorBikeData);

        controlPoint = new BluetoothGattCharacteristic(
                CONTROL_POINT,
                BluetoothGattCharacteristic.PROPERTY_WRITE
                        | BluetoothGattCharacteristic.PROPERTY_INDICATE,
                BluetoothGattCharacteristic.PERMISSION_WRITE);
        controlPoint.addDescriptor(newCccd());
        service.addCharacteristic(controlPoint);

        server.addService(service);
        return true;
    }

    /**
     * What a client last wrote to a CCCD, per client and per characteristic.
     *
     * A descriptor read has to give back what that client wrote, not a global last-write:
     * `BluetoothGattDescriptor.setValue()` would be one value shared by every connection.
     */
    private final java.util.Map<String, byte[]> cccdValues = new java.util.HashMap<>();

    private static String cccdKey(BluetoothDevice device, BluetoothGattDescriptor descriptor) {
        return device.getAddress() + "/" + descriptor.getCharacteristic().getUuid();
    }

    private byte[] cccdValue(BluetoothDevice device, BluetoothGattDescriptor descriptor) {
        byte[] value = cccdValues.get(cccdKey(device, descriptor));
        return value != null ? value : new byte[] {0x00, 0x00};
    }

    private void rememberCccd(BluetoothDevice device, BluetoothGattDescriptor descriptor,
                              byte[] value) {
        if (value != null && value.length >= 2) {
            cccdValues.put(cccdKey(device, descriptor), new byte[] {value[0], value[1]});
        }
    }

    private BluetoothGattDescriptor newCccd() {
        return new BluetoothGattDescriptor(CCCD,
                BluetoothGattDescriptor.PERMISSION_READ | BluetoothGattDescriptor.PERMISSION_WRITE);
    }

    @SuppressLint("MissingPermission")
    private void startAdvertising() {
        AdvertiseSettings settings = new AdvertiseSettings.Builder()
                .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
                .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
                .setConnectable(true)
                .build();

        // The name goes in the advertisement itself, not only in the scan response.
        //
        // Putting it only in the scan response looks tidier and costs a whole afternoon: a
        // scanner receives a scan response only if it is scanning *actively*, and plenty are
        // not. QZ on Windows saw this device as an unnamed address with a 0x1826 service on
        // it and could not match the name it selects the driver by, so it never claimed it.
        //
        // The 31-byte budget is still the thing to respect - flags 3, the 16-bit service UUID
        // 4, and a ten-character name 12, which is 19 - but that budget is only comfortable
        // because the adapter has been renamed by now. If the rename failed, the phone's real
        // name goes in instead and a long one overflows: that is ADVERTISE_FAILED_DATA_TOO_
        // LARGE, reported by Android as a bare error code 1 and translated below. Failing
        // loudly there is the point. An advertisement with no name is a device QZ will look
        // straight past without a word.
        AdvertiseData data = new AdvertiseData.Builder()
                .setIncludeDeviceName(true)
                .setIncludeTxPowerLevel(false)
                .addServiceUuid(new ParcelUuid(FITNESS_MACHINE_SERVICE))
                .build();
        // ...and in the scan response as well, for anything that does ask.
        AdvertiseData scanResponse = new AdvertiseData.Builder()
                .setIncludeDeviceName(true)
                .build();

        advertiser.startAdvertising(settings, data, scanResponse, advertiseCallback);
    }

    /**
     * Poll until the adapter reports @p wanted, or give up.
     *
     * @return whether the stack agreed within @p ms.
     */
    @SuppressLint("MissingPermission")
    private boolean waitForAdapterName(String wanted, long ms) {
        long deadline = System.currentTimeMillis() + ms;
        while (System.currentTimeMillis() < deadline) {
            if (wanted.equals(manager.getAdapter().getName())) return true;
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return wanted.equals(manager.getAdapter().getName());
    }

    private final AdvertiseCallback advertiseCallback = new AdvertiseCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onStartSuccess(AdvertiseSettings settingsInEffect) {
            // The name actually on the air, not the one that was asked for - if the rename
            // did not take, this is where it shows.
            say("advertising as " + manager.getAdapter().getName());
        }

        @Override
        public void onStartFailure(int errorCode) {
            String why;
            switch (errorCode) {
                case ADVERTISE_FAILED_DATA_TOO_LARGE:
                    why = "advertising data too large - is the Bluetooth name long?";
                    break;
                case ADVERTISE_FAILED_FEATURE_UNSUPPORTED:
                    why = "this device cannot advertise";
                    break;
                case ADVERTISE_FAILED_ALREADY_STARTED:
                    why = "already advertising";
                    break;
                case ADVERTISE_FAILED_TOO_MANY_ADVERTISERS:
                    why = "too many advertisers";
                    break;
                default:
                    why = "internal error";
                    break;
            }
            say("advertising failed (" + errorCode + "): " + why);
        }
    };

    private final BluetoothGattServerCallback serverCallback = new BluetoothGattServerCallback() {
        @Override
        public void onConnectionStateChange(BluetoothDevice device, int status, int newState) {
            if (newState == BluetoothGatt.STATE_CONNECTED) {
                say("connected: " + safeName(device));
            } else {
                subscribers.remove(device);
                controlSubscribers.remove(device);
                say("disconnected: " + safeName(device));
            }
        }

        @SuppressLint("MissingPermission")
        @Override
        public void onCharacteristicReadRequest(BluetoothDevice device, int requestId, int offset,
                                                BluetoothGattCharacteristic characteristic) {
            byte[] value = characteristic.getValue();
            if (value == null) value = new byte[0];
            if (offset > value.length) {
                server.sendResponse(device, requestId,
                        BluetoothGatt.GATT_INVALID_OFFSET, offset, null);
                return;
            }
            byte[] slice = new byte[value.length - offset];
            System.arraycopy(value, offset, slice, 0, slice.length);
            server.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, slice);
        }

        /**
         * Answer descriptor reads. Not optional, and leaving it out cost a whole session.
         *
         * `BluetoothGattServerCallback`'s default implementation does nothing at all - it does
         * not even send an error response - so an unanswered ATT read sits there until the
         * central's 30-second transaction timeout, which then tears the link down. Windows
         * reads the CCCDs while discovering service details, so QZ connected, waited exactly
         * thirty seconds, and reported every service on the device as InvalidService without
         * ever seeing a characteristic. Every request this server can receive has to be
         * answered, including the ones with nothing interesting to say.
         */
        @SuppressLint("MissingPermission")
        @Override
        public void onDescriptorReadRequest(BluetoothDevice device, int requestId, int offset,
                                            BluetoothGattDescriptor descriptor) {
            byte[] value = cccdValue(device, descriptor);
            if (offset > value.length) {
                server.sendResponse(device, requestId,
                        BluetoothGatt.GATT_INVALID_OFFSET, offset, null);
                return;
            }
            byte[] slice = new byte[value.length - offset];
            System.arraycopy(value, offset, slice, 0, slice.length);
            server.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, slice);
        }

        /** Prepared writes need an answer too, for the same reason. */
        @SuppressLint("MissingPermission")
        @Override
        public void onExecuteWrite(BluetoothDevice device, int requestId, boolean execute) {
            server.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, 0, null);
        }

        @SuppressLint("MissingPermission")
        @Override
        public void onDescriptorWriteRequest(BluetoothDevice device, int requestId,
                                             BluetoothGattDescriptor descriptor,
                                             boolean preparedWrite, boolean responseNeeded,
                                             int offset, byte[] value) {
            rememberCccd(device, descriptor, value);
            boolean on = value != null && value.length > 0 && value[0] != 0x00;
            List<BluetoothDevice> list =
                    INDOOR_BIKE_DATA.equals(descriptor.getCharacteristic().getUuid())
                            ? subscribers : controlSubscribers;
            if (on) {
                if (!list.contains(device)) list.add(device);
            } else {
                list.remove(device);
            }
            say((on ? "subscribed to " : "unsubscribed from ")
                    + shortUuid(descriptor.getCharacteristic().getUuid()));
            if (responseNeeded) {
                server.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, value);
            }
        }

        @SuppressLint("MissingPermission")
        @Override
        public void onCharacteristicWriteRequest(BluetoothDevice device, int requestId,
                                                 BluetoothGattCharacteristic characteristic,
                                                 boolean preparedWrite, boolean responseNeeded,
                                                 int offset, byte[] value) {
            if (responseNeeded) {
                server.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, value);
            }
            if (CONTROL_POINT.equals(characteristic.getUuid())) {
                handleControlPoint(device, value);
            }
        }
    };

    /**
     * The control point is where QZ's write path gets tested, and one branch here exists
     * purely because of this bike.
     *
     * <p>{@code ftmsbike.cpp} sends Set Target Resistance as <b>three</b> bytes for a YPBM -
     * opcode plus the level times ten as a 16-bit little-endian value - where the ordinary
     * FTMS spelling is two bytes with the level as a single byte. Accepting both is what makes
     * this a mock of that bike rather than of a generic trainer.
     */
    @SuppressLint("MissingPermission")
    private void handleControlPoint(BluetoothDevice device, byte[] value) {
        if (value == null || value.length == 0) return;
        byte opcode = value[0];
        byte result = RESULT_SUCCESS;

        switch (opcode) {
            case OP_REQUEST_CONTROL:
            case OP_RESET:
            case OP_START_RESUME:
            case OP_STOP_PAUSE:
                break;
            case OP_SET_TARGET_RESISTANCE:
                if (value.length >= 3) {
                    // The YPBM spelling: the level times ten, as a 16-bit little-endian value.
                    resistance = ((value[1] & 0xFF) | ((value[2] & 0xFF) << 8)) / 10.0;
                } else if (value.length >= 2) {
                    // The ordinary FTMS spelling: one byte, the level itself.
                    resistance = value[1] & 0xFF;
                } else {
                    result = RESULT_NOT_SUPPORTED;
                }
                if (result == RESULT_SUCCESS) {
                    // A resistance request ends any ERG hold, the way a trainer does, and takes
                    // the number away from the scenario for the rest of the ride.
                    requestedPower = -1;
                    clientOwnsResistance = true;
                    lastCommand = "resistance -> " + Math.round(resistance);
                }
                break;
            case OP_SET_TARGET_POWER:
                if (value.length >= 3) {
                    requestedPower = (value[1] & 0xFF) | ((value[2] & 0xFF) << 8);
                    lastCommand = "erg -> " + Math.round(requestedPower) + " W";
                } else {
                    result = RESULT_NOT_SUPPORTED;
                }
                break;
            case OP_SET_SIM_PARAMS:
                if (value.length >= 7) {
                    short grade = (short) ((value[3] & 0xFF) | ((value[4] & 0xFF) << 8));
                    inclination = grade / 100.0;
                    lastCommand = String.format(Locale.US, "grade -> %.1f%%", inclination);
                } else {
                    result = RESULT_NOT_SUPPORTED;
                }
                break;
            default:
                result = RESULT_NOT_SUPPORTED;
                break;
        }

        say("control point: " + hex(value) + " -> " + (result == RESULT_SUCCESS ? "ok" : "not supported"));

        if (server != null && controlPoint != null && !controlSubscribers.isEmpty()) {
            controlPoint.setValue(new byte[] {OP_RESPONSE_CODE, opcode, result});
            for (BluetoothDevice subscriber : new ArrayList<>(controlSubscribers)) {
                server.notifyCharacteristicChanged(subscriber, controlPoint, true);
            }
        }
    }

    // --- the ride ------------------------------------------------------------------------

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!running) return;
            long now = System.currentTimeMillis();
            double dt = (now - lastTickMs) / 1000.0;
            lastTickMs = now;
            step(dt);
            handler.postDelayed(this, TICK_MS);
        }
    };

    /** One step of the ride, mirroring simulatedbike::update(). */
    @SuppressLint("MissingPermission")
    private void step(double dt) {
        rideSeconds += dt;
        double duration = scenario.duration();
        if (duration > 0 && rideSeconds > duration) rideSeconds = 0;

        RideScenario.Point p = scenario.at(rideSeconds);

        if (p.silent) {
            // Nothing at all goes out. On a real radio this is a true gap in the stream,
            // which is the one thing the in-process DIRCON tests cannot reproduce - there,
            // QZ's own timer keeps pushing the last values.
            listener.onFrame(rideSeconds, 0, 0, 0, (int) Math.round(resistance), 0, true);
            return;
        }

        // The scenario sets resistance only until a client asks for something. After that the
        // client owns it, the way a trainer under a training app does - otherwise the file and
        // QZ fight over the same number every tick and neither is testable.
        if (p.resistance.present && requestedPower < 0 && !clientOwnsResistance) {
            resistance = p.resistance.value;
        }

        double cadence = p.cadence.present ? p.cadence.value : 0;

        double rideWatts = simulatedWatts;
        if (requestedPower >= 0) {
            rideWatts = ergPower(requestedPower, simulatedWatts, dt);
        } else if (p.watts.present) {
            rideWatts = p.watts.value;
        }
        simulatedWatts = rideWatts;

        // Coasting is not negotiable: no cadence means no power, whatever anyone requested.
        int watts = cadence <= 0 ? 0 : (int) Math.round(applyNoise(rideWatts));

        double speed;
        if (p.speed.present) {
            speed = p.speed.value;
        } else if (cadence <= 0) {
            speed = 0;
        } else {
            speed = speedFromPower(watts, inclination, lastSpeed, dt);
        }
        lastSpeed = speed;

        int heart = p.hr.present ? (int) Math.round(p.hr.value) : 0;

        notifyIndoorBikeData(speed, cadence, resistance, watts, heart);
        listener.onFrame(rideSeconds, watts, cadence, speed, (int) Math.round(resistance),
                heart, false);
        if (!lastCommand.isEmpty()) listener.onCommand(lastCommand);
    }

    /** Power converging on an ERG request, the way a trainer's flywheel does. */
    private double ergPower(double target, double current, double dt) {
        double lag = scenario.getErgLag();
        if (lag <= 0 || dt <= 0) return target;
        double alpha = 1.0 - Math.exp(-dt / lag);
        return current + (target - current) * alpha;
    }

    private double applyNoise(double watts) {
        double n = scenario.getNoise();
        if (n <= 0) return watts;
        return watts * (1.0 + n * (random.nextDouble() * 2.0 - 1.0));
    }

    @SuppressLint("MissingPermission")
    private void notifyIndoorBikeData(double speedKmh, double cadenceRpm, double resistanceLevel,
                                      int watts, int heart) {
        if (server == null || indoorBikeData == null || subscribers.isEmpty()) return;

        int speed = (int) Math.round(speedKmh * 100);      // 0.01 km/h
        int cadence = (int) Math.round(cadenceRpm * 2);    // 0.5 rpm
        int level = (int) Math.round(resistanceLevel);

        byte[] frame = new byte[] {
                0x64, 0x02,                                     // flags 0x0264
                (byte) (speed & 0xFF), (byte) ((speed >> 8) & 0xFF),
                (byte) (cadence & 0xFF), (byte) ((cadence >> 8) & 0xFF),
                (byte) (level & 0xFF), (byte) ((level >> 8) & 0xFF),
                (byte) (watts & 0xFF), (byte) ((watts >> 8) & 0xFF),
                (byte) (heart & 0xFF), 0x00
        };
        indoorBikeData.setValue(frame);
        for (BluetoothDevice device : new ArrayList<>(subscribers)) {
            server.notifyCharacteristicChanged(device, indoorBikeData, false);
        }
    }

    /**
     * Speed from power, ported from metric::calculateSpeedFromPower and its two helpers in
     * src/metric.cpp, with QZ's default rider weight (75 kg), bike weight (0) and rolling
     * resistance (0.005). Ported rather than invented so a rider sees the same speed here as
     * they would from the simulated bike playing the same file.
     */
    private static double speedFromPower(double power, double inclination, double speed,
                                         double dt) {
        if (inclination < -5) inclination = -5;
        final double fullWeight = 75.0;
        double maxSpeed = maxSpeedFromPower(power, inclination);
        double maxPowerFromSpeed = powerFromSpeed(speed, inclination);
        double acceleration = (power - maxPowerFromSpeed) / fullWeight;
        double newSpeed = speed + (acceleration * 3.6 * dt);
        if (newSpeed < 0) newSpeed = 0;
        if (maxSpeed > newSpeed) return newSpeed;
        if (maxSpeed < speed) return newSpeed;
        return maxSpeed;
    }

    private static double powerFromSpeed(double speedKmh, double inclination) {
        final double rollingResistance = 0.005;
        final double aero = 0.22691607640851885;
        final double tran = 0.95;
        double v = speedKmh / 3.6;
        double tv = v;
        double a2Eff = (tv > 0.0) ? aero : -aero;
        double twt = 9.8 * 75.0;
        double tr = twt * ((inclination / 100.0) + rollingResistance);
        return (v * tr + v * tv * tv * a2Eff) / tran;
    }

    private static double maxSpeedFromPower(double power, double inclination) {
        final double rollingResistance = 0.005;
        final double aero = 0.22691607640851885;
        final double tran = 0.95;
        double twt = 9.8 * 75.0;
        double tr = twt * ((inclination / 100.0) + rollingResistance);
        double vel = 20;
        for (int i = 1; i < 10; i++) {
            double tv = vel;
            double aeroEff = (tv > 0.0) ? aero : -aero;
            double f = vel * (aeroEff * tv * tv + tr) - tran * power;
            double fp = aeroEff * (3.0 * vel) * tv + tr;
            double vNew = vel - f / fp;
            if (Math.abs(vNew - vel) < 0.05) {
                return vNew < 0 ? 0 : vNew * 3.6;
            }
            vel = vNew;
        }
        return vel < 0 ? 0 : vel * 3.6;
    }

    // --- odds and ends -------------------------------------------------------------------

    private void say(String message) {
        Log.i(TAG, message);
        listener.onState(message);
    }

    @SuppressLint("MissingPermission")
    private static String safeName(BluetoothDevice device) {
        try {
            String name = device.getName();
            return name != null ? name : device.getAddress();
        } catch (SecurityException e) {
            return device.getAddress();
        }
    }

    private static String shortUuid(UUID uuid) {
        return String.format("0x%04x", (int) ((uuid.getMostSignificantBits() >> 32) & 0xFFFF));
    }

    private static String hex(byte[] data) {
        StringBuilder sb = new StringBuilder();
        for (byte b : data) sb.append(String.format("%02x ", b));
        return sb.toString().trim();
    }
}
