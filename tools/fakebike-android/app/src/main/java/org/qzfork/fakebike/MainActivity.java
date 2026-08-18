package org.qzfork.fakebike;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Pick a ride, press play, and the phone becomes a trainer.
 *
 * <p>The whole UI is a spinner, a button and two labels on purpose: this is a test rig, and
 * every control it grows is one more thing to get wrong while chasing a Bluetooth bug.
 */
public class MainActivity extends Activity implements FtmsPeripheral.Listener {

    private static final int REQUEST_PERMISSIONS = 1;

    private Spinner ridePicker;
    private Button playButton;
    private TextView stateLabel;
    private TextView frameLabel;
    private TextView commandLabel;

    private FtmsPeripheral peripheral;
    private final List<String> rideFiles = new ArrayList<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // A trainer that stops streaming when the screen dims is not a trainer. The rig is
        // meant to sit on a desk next to whatever is being tested.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        ridePicker = findViewById(R.id.ridePicker);
        playButton = findViewById(R.id.playButton);
        stateLabel = findViewById(R.id.stateLabel);
        frameLabel = findViewById(R.id.frameLabel);
        commandLabel = findViewById(R.id.commandLabel);

        peripheral = new FtmsPeripheral(this, this);

        loadRideList();
        playButton.setOnClickListener(v -> onPlayOrStop());
        onState("idle - advertises as " + FtmsPeripheral.ADVERTISED_NAME);
    }

    @Override
    protected void onDestroy() {
        // Without this the phone keeps the borrowed Bluetooth name.
        peripheral.stop();
        super.onDestroy();
    }

    private void loadRideList() {
        rideFiles.clear();
        try {
            String[] found = getAssets().list("rides");
            if (found != null) {
                List<String> sorted = new ArrayList<>(Arrays.asList(found));
                java.util.Collections.sort(sorted);
                for (String name : sorted) {
                    if (name.endsWith(".ride")) rideFiles.add(name);
                }
            }
        } catch (IOException e) {
            onState("could not read the bundled rides: " + e);
        }
        if (rideFiles.isEmpty()) {
            onState("no .ride files are bundled in this build");
        }
        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, rideFiles);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        ridePicker.setAdapter(adapter);
    }

    private void onPlayOrStop() {
        if (peripheral.isRunning()) {
            peripheral.stop();
            playButton.setText(R.string.play);
            ridePicker.setEnabled(true);
            return;
        }
        if (!ensurePermissions()) return;

        int index = ridePicker.getSelectedItemPosition();
        if (index < 0 || index >= rideFiles.size()) {
            onState("pick a ride first");
            return;
        }
        String file = rideFiles.get(index);
        RideScenario scenario;
        try {
            scenario = RideScenario.load(getAssets().open("rides/" + file), file);
        } catch (IOException e) {
            onState("could not open " + file + ": " + e);
            return;
        }
        if (!scenario.isValid()) {
            onState(file + " did not parse - it is not a usable scenario");
            return;
        }

        if (peripheral.start(scenario)) {
            playButton.setText(R.string.stop);
            ridePicker.setEnabled(false);
        }
    }

    /**
     * From API 31 advertising and connecting are separate runtime permissions, and without
     * them the adapter calls fail with a SecurityException rather than a useful message.
     */
    private boolean ensurePermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true;
        List<String> missing = new ArrayList<>();
        for (String permission : new String[] {
                Manifest.permission.BLUETOOTH_ADVERTISE,
                Manifest.permission.BLUETOOTH_CONNECT}) {
            if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                missing.add(permission);
            }
        }
        if (missing.isEmpty()) return true;
        requestPermissions(missing.toArray(new String[0]), REQUEST_PERMISSIONS);
        onState("granting Bluetooth permissions - press play again when done");
        return false;
    }

    @Override
    public void onState(String message) {
        runOnUiThread(() -> stateLabel.setText(message));
    }

    @Override
    public void onCommand(String summary) {
        runOnUiThread(() -> commandLabel.setText("last request from the app: " + summary));
    }

    @Override
    public void onFrame(double rideSeconds, int watts, double cadence, double speedKmh,
                        int resistance, int heart, boolean silent) {
        final String text = silent
                ? String.format(Locale.US, "t=%.0fs  -- silent (dropout) --", rideSeconds)
                : String.format(Locale.US,
                        "t=%.0fs   %d W   %.0f rpm   %.1f km/h   res %d   hr %d",
                        rideSeconds, watts, cadence, speedKmh, resistance, heart);
        runOnUiThread(() -> frameLabel.setText(text));
    }
}
