package org.qzfork.fakebike;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;

/**
 * A ride scenario, ported from src/devices/simulatedbike/ridescenario.cpp.
 *
 * This is the third player of the same format - the simulated bike inside QZ plays it, the
 * Layer C tests play it, and this peripheral plays it out over a real radio. The same file
 * through all three has to produce the same numbers, so the two rules that are easy to get
 * wrong are ported exactly rather than approximated:
 *
 * <p><b>Each field is its own timeline.</b> A value interpolates between the two nearest
 * samples that both state it, is absent before the first that states it, and holds after the
 * last. So {@code hr} can appear in one sample in ten without dragging its neighbours to
 * zero, and {@code cadence=0} is an explicit value to be reproduced exactly rather than read
 * as "said nothing".
 *
 * <p><b>Interpolation is always linear.</b> No step mode, no easing, no per-field override.
 *
 * <p>{@code silence=<seconds>} is the exception that is not about values: the bike reports
 * nothing at all for that long. On this peripheral that is a genuine gap in the notification
 * stream, which is the one thing the DIRCON loop in the test suite cannot produce.
 *
 * <p>Kept deliberately dependency-free and mechanical. If the C++ changes, this has to change
 * with it, and a reader comparing the two should be able to do it line by line.
 */
public class RideScenario {

    public static final int MODE_POWER = 0;
    public static final int MODE_RESISTANCE = 1;
    public static final int MODE_SPEED = 2;

    /** One field of one sample. {@code present == false} means the line did not state it. */
    public static class Value {
        public final boolean present;
        public final double value;

        Value(boolean present, double value) {
            this.present = present;
            this.value = value;
        }
    }

    private static final Value ABSENT = new Value(false, 0);

    /** One line of the file. */
    private static class Sample {
        double t;
        Value watts = ABSENT;
        Value cadence = ABSENT;
        Value hr = ABSENT;
        Value speed = ABSENT;
        Value resistance = ABSENT;
        double silence;
    }

    /** Every field at one instant, as a player should report it. */
    public static class Point {
        public double t;
        public Value watts = ABSENT;
        public Value cadence = ABSENT;
        public Value hr = ABSENT;
        public Value speed = ABSENT;
        public Value resistance = ABSENT;
        /** True while a silence window is open: the bike reports nothing at all. */
        public boolean silent;
    }

    private boolean valid;
    private String name = "";
    private int mode = MODE_POWER;
    private String bike = "";
    private double startResistance = -1;
    private double ergLag = 2.0;
    private double noise = 0.0;
    private final List<Sample> samples = new ArrayList<>();

    public boolean isValid() { return valid; }
    public String getName() { return name; }
    public int getMode() { return mode; }
    public String getBike() { return bike; }
    public double getStartResistance() { return startResistance; }
    public double getErgLag() { return ergLag; }
    public double getNoise() { return noise; }

    public double duration() {
        double last = 0;
        for (Sample s : samples) {
            if (s.t > last) last = s.t;
            if (s.t + s.silence > last) last = s.t + s.silence;
        }
        return last;
    }

    public static RideScenario load(InputStream in, String name) throws IOException {
        StringBuilder text = new StringBuilder();
        BufferedReader reader =
                new BufferedReader(new InputStreamReader(in, Charset.forName("UTF-8")));
        String line;
        while ((line = reader.readLine()) != null) {
            text.append(line).append('\n');
        }
        reader.close();
        RideScenario s = parse(text.toString());
        s.name = name;
        return s;
    }

    public static RideScenario parse(String text) {
        RideScenario s = new RideScenario();
        for (String raw : text.split("\n")) {
            String line = stripComment(raw).trim();
            if (line.isEmpty()) continue;

            if (line.startsWith("t=") || line.startsWith("t =")) {
                Sample sample = parseSample(line);
                if (sample == null) return s; // invalid: valid stays false
                s.samples.add(sample);
                continue;
            }

            String[] parts = line.split("\\s+", 2);
            if (parts.length < 2) return s;
            String key = parts[0];
            String value = parts[1].trim();
            try {
                if (key.equals("mode")) {
                    if (value.equals("power")) s.mode = MODE_POWER;
                    else if (value.equals("resistance")) s.mode = MODE_RESISTANCE;
                    else if (value.equals("speed")) s.mode = MODE_SPEED;
                    else return s;
                } else if (key.equals("bike")) {
                    s.bike = value;
                } else if (key.equals("resistance")) {
                    s.startResistance = Double.parseDouble(value);
                } else if (key.equals("erg_lag")) {
                    s.ergLag = Double.parseDouble(value);
                } else if (key.equals("noise")) {
                    s.noise = Double.parseDouble(value);
                } else {
                    return s; // an unknown directive is a typo, not something to ignore
                }
            } catch (NumberFormatException e) {
                return s;
            }
        }
        s.valid = !s.samples.isEmpty();
        return s;
    }

    private static String stripComment(String line) {
        int hash = line.indexOf('#');
        return hash >= 0 ? line.substring(0, hash) : line;
    }

    private static Sample parseSample(String line) {
        Sample sample = new Sample();
        boolean haveT = false;
        for (String token : line.split("\\s+")) {
            int eq = token.indexOf('=');
            if (eq <= 0) return null;
            String key = token.substring(0, eq).trim();
            String rest = token.substring(eq + 1).trim();
            double v;
            try {
                v = Double.parseDouble(rest);
            } catch (NumberFormatException e) {
                return null;
            }
            if (key.equals("t")) {
                sample.t = v;
                haveT = true;
            } else if (key.equals("watts")) {
                sample.watts = new Value(true, v);
            } else if (key.equals("cadence")) {
                sample.cadence = new Value(true, v);
            } else if (key.equals("hr")) {
                sample.hr = new Value(true, v);
            } else if (key.equals("speed")) {
                sample.speed = new Value(true, v);
            } else if (key.equals("resistance")) {
                sample.resistance = new Value(true, v);
            } else if (key.equals("silence")) {
                sample.silence = v;
            } else {
                return null;
            }
        }
        return haveT ? sample : null;
    }

    /** Which field of a sample a timeline is being built from. */
    private interface Field {
        Value of(Sample s);
    }

    private static final Field WATTS = new Field() { public Value of(Sample s) { return s.watts; } };
    private static final Field CADENCE = new Field() { public Value of(Sample s) { return s.cadence; } };
    private static final Field HR = new Field() { public Value of(Sample s) { return s.hr; } };
    private static final Field SPEED = new Field() { public Value of(Sample s) { return s.speed; } };
    private static final Field RESISTANCE =
            new Field() { public Value of(Sample s) { return s.resistance; } };

    private Value valueAt(Field field, double t) {
        Sample before = null;
        Sample after = null;
        for (Sample s : samples) {
            if (!field.of(s).present) continue;
            if (s.t <= t) before = s;
            else if (after == null) after = s;
        }
        if (before == null) {
            // Before the first statement of this field the bike has said nothing about it.
            // Holding the first value backwards would invent a reading the ride never had.
            return ABSENT;
        }
        if (after == null || after.t == before.t) return field.of(before);

        double span = after.t - before.t;
        double frac = (t - before.t) / span;
        double a = field.of(before).value;
        double b = field.of(after).value;
        return new Value(true, a + (b - a) * frac);
    }

    public Point at(double t) {
        Point p = new Point();
        p.t = t;
        if (!valid) return p;

        // A silence window swallows everything inside it, including a sample that would fall
        // in the middle of one. Reporting a value from within a dropout is the bug the
        // dropout scenario exists to catch, so this check comes first and returns.
        for (Sample s : samples) {
            if (s.silence > 0 && t >= s.t && t < s.t + s.silence) {
                p.silent = true;
                return p;
            }
        }

        p.watts = valueAt(WATTS, t);
        p.cadence = valueAt(CADENCE, t);
        p.hr = valueAt(HR, t);
        p.speed = valueAt(SPEED, t);
        p.resistance = valueAt(RESISTANCE, t);
        return p;
    }
}
