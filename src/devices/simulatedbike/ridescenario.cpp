#include "ridescenario.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

const RideValue absentValue = {false, 0.0};

std::string trim(const std::string &s) {
    const std::string ws = " \t\r\n";
    const std::string::size_type first = s.find_first_not_of(ws);
    if (first == std::string::npos)
        return std::string();
    const std::string::size_type last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

/** Everything from an unquoted # to the end of the line is a comment. */
std::string stripComment(const std::string &line) {
    const std::string::size_type hash = line.find('#');
    if (hash == std::string::npos)
        return line;
    return line.substr(0, hash);
}

/**
 * Parse a number, rejecting anything with trailing rubbish. strtod alone accepts "90rpm" as
 * 90, which turns a typo into a plausible ride.
 */
bool parseNumber(const std::string &s, double *out) {
    if (s.empty())
        return false;
    const char *begin = s.c_str();
    char *end = nullptr;
    const double v = strtod(begin, &end);
    if (end == begin || *end != '\0')
        return false;
    *out = v;
    return true;
}

/**
 * Bounds exist to catch typos, not to model physiology: a missing decimal point or a
 * transposed pair of digits should stop the file rather than produce a ride nobody notices is
 * wrong. They are deliberately far wider than any real session.
 */
struct FieldBound {
    const char *name;
    double min;
    double max;
};

bool checkBound(const FieldBound &b, double v, std::string *error, int lineNo) {
    if (v >= b.min && v <= b.max)
        return true;
    if (error) {
        std::ostringstream os;
        os << "line " << lineNo << ": " << b.name << " of " << v << " is outside " << b.min
           << ".." << b.max;
        *error = os.str();
    }
    return false;
}

void formatNumber(std::ostringstream &os, double v) {
    // Canonical form drops a trailing ".0" so that a hand-written "watts=200" survives a
    // round trip looking like itself. Anything fractional keeps enough digits to be exact
    // for the resolutions the format actually carries.
    if (v == static_cast<long long>(v)) {
        os << static_cast<long long>(v);
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", v);
    os << buf;
}

void appendField(std::ostringstream &os, const char *name, const RideValue &f) {
    if (!f.present)
        return;
    os << ' ' << name << '=';
    formatNumber(os, f.value);
}

/**
 * One field's timeline, evaluated at t: interpolate between the two nearest samples that both
 * state it, hold outside them, absent before the first.
 *
 * Linear search over the samples rather than an index. A scenario is tens of lines and this is
 * called at the player's tick rate; an index would be a structure to keep in step for no
 * measurable gain.
 */
RideValue valueAt(const std::vector<RideSample> &samples, RideValue RideSample::*field, double t) {
    const RideSample *before = nullptr;
    const RideSample *after = nullptr;

    for (std::vector<RideSample>::const_iterator it = samples.begin(); it != samples.end(); ++it) {
        if (!((*it).*field).present)
            continue;
        if (it->t <= t)
            before = &(*it);
        else if (!after)
            after = &(*it);
    }

    if (!before) {
        // Before the first statement of this field the bike has said nothing about it. Holding
        // the *first* value backwards would invent a reading the ride never had.
        return absentValue;
    }
    if (!after || after->t == before->t)
        return ((*before).*field);

    const double span = after->t - before->t;
    const double frac = (t - before->t) / span;
    const double a = ((*before).*field).value;
    const double b = ((*after).*field).value;
    const RideValue r = {true, a + (b - a) * frac};
    return r;
}

} // namespace

RideScenario::RideScenario()
    : m_valid(false), m_mode(RIDE_MODE_POWER), m_startResistance(-1),
      m_ergLag(RideScenario::defaultErgLag()), m_noise(RideScenario::defaultNoise()) {}

RideScenario RideScenario::parse(const std::string &text, std::string *error) {
    RideScenario s;
    if (error)
        error->clear();

    static const FieldBound boundWatts = {"watts", 0.0, 3000.0};
    static const FieldBound boundCadence = {"cadence", 0.0, 250.0};
    static const FieldBound boundHr = {"hr", 0.0, 250.0};
    static const FieldBound boundSpeed = {"speed", 0.0, 150.0};
    static const FieldBound boundResistance = {"resistance", 0.0, 254.0};

    std::istringstream in(text);
    std::string rawLine;
    int lineNo = 0;
    bool modeSeen = false;

    while (std::getline(in, rawLine)) {
        ++lineNo;
        const std::string line = trim(stripComment(rawLine));
        if (line.empty())
            continue;

        // A sample line is any line whose first token starts with "t=". Everything else is a
        // directive, which keeps the two kinds of line impossible to confuse.
        if (line.compare(0, 2, "t=") == 0) {
            RideSample sample;
            sample.t = 0;
            sample.watts = absentValue;
            sample.cadence = absentValue;
            sample.hr = absentValue;
            sample.speed = absentValue;
            sample.resistance = absentValue;
            sample.silence = 0;
            bool haveT = false;

            std::istringstream ls(line);
            std::string token;
            while (ls >> token) {
                const std::string::size_type eq = token.find('=');
                if (eq == std::string::npos || eq == 0) {
                    if (error) {
                        std::ostringstream os;
                        os << "line " << lineNo << ": '" << token << "' is not key=value";
                        *error = os.str();
                    }
                    return RideScenario();
                }
                const std::string key = token.substr(0, eq);
                double v = 0;
                if (!parseNumber(token.substr(eq + 1), &v)) {
                    if (error) {
                        std::ostringstream os;
                        os << "line " << lineNo << ": '" << token << "' has no usable number";
                        *error = os.str();
                    }
                    return RideScenario();
                }

                const RideValue set = {true, v};
                if (key == "t") {
                    if (v < 0) {
                        if (error) {
                            std::ostringstream os;
                            os << "line " << lineNo << ": t must not be negative";
                            *error = os.str();
                        }
                        return RideScenario();
                    }
                    sample.t = v;
                    haveT = true;
                } else if (key == "watts") {
                    if (!checkBound(boundWatts, v, error, lineNo))
                        return RideScenario();
                    sample.watts = set;
                } else if (key == "cadence") {
                    if (!checkBound(boundCadence, v, error, lineNo))
                        return RideScenario();
                    sample.cadence = set;
                } else if (key == "hr") {
                    if (!checkBound(boundHr, v, error, lineNo))
                        return RideScenario();
                    sample.hr = set;
                } else if (key == "speed") {
                    if (!checkBound(boundSpeed, v, error, lineNo))
                        return RideScenario();
                    sample.speed = set;
                } else if (key == "resistance") {
                    if (!checkBound(boundResistance, v, error, lineNo))
                        return RideScenario();
                    sample.resistance = set;
                } else if (key == "silence") {
                    if (v <= 0) {
                        if (error) {
                            std::ostringstream os;
                            os << "line " << lineNo << ": silence must be positive";
                            *error = os.str();
                        }
                        return RideScenario();
                    }
                    sample.silence = v;
                } else {
                    if (error) {
                        std::ostringstream os;
                        os << "line " << lineNo << ": unknown field '" << key << "'";
                        *error = os.str();
                    }
                    return RideScenario();
                }
            }

            if (!haveT) {
                if (error) {
                    std::ostringstream os;
                    os << "line " << lineNo << ": sample has no t";
                    *error = os.str();
                }
                return RideScenario();
            }
            if (!s.m_samples.empty() && sample.t <= s.m_samples.back().t) {
                if (error) {
                    std::ostringstream os;
                    os << "line " << lineNo << ": t=" << sample.t << " does not advance past "
                       << s.m_samples.back().t;
                    *error = os.str();
                }
                return RideScenario();
            }
            s.m_samples.push_back(sample);
            continue;
        }

        // Directive: name, then one value.
        std::istringstream ds(line);
        std::string key;
        std::string value;
        ds >> key >> value;
        if (value.empty()) {
            if (error) {
                std::ostringstream os;
                os << "line " << lineNo << ": directive '" << key << "' has no value";
                *error = os.str();
            }
            return RideScenario();
        }
        std::string extra;
        if (ds >> extra) {
            if (error) {
                std::ostringstream os;
                os << "line " << lineNo << ": directive '" << key << "' has more than one value";
                *error = os.str();
            }
            return RideScenario();
        }

        if (key == "mode") {
            if (value == "power")
                s.m_mode = RIDE_MODE_POWER;
            else if (value == "resistance")
                s.m_mode = RIDE_MODE_RESISTANCE;
            else if (value == "speed")
                s.m_mode = RIDE_MODE_SPEED;
            else {
                if (error) {
                    std::ostringstream os;
                    os << "line " << lineNo << ": mode must be power, resistance or speed, not '"
                       << value << "'";
                    *error = os.str();
                }
                return RideScenario();
            }
            modeSeen = true;
        } else if (key == "bike") {
            s.m_bike = value;
        } else if (key == "resistance") {
            double v = 0;
            if (!parseNumber(value, &v) || !checkBound(boundResistance, v, error, lineNo)) {
                if (error && error->empty()) {
                    std::ostringstream os;
                    os << "line " << lineNo << ": resistance '" << value << "' is not a number";
                    *error = os.str();
                }
                return RideScenario();
            }
            s.m_startResistance = v;
        } else if (key == "erg_lag") {
            double v = 0;
            if (!parseNumber(value, &v) || v < 0) {
                if (error) {
                    std::ostringstream os;
                    os << "line " << lineNo << ": erg_lag '" << value
                       << "' is not a number of seconds";
                    *error = os.str();
                }
                return RideScenario();
            }
            s.m_ergLag = v;
        } else if (key == "noise") {
            double v = 0;
            if (!parseNumber(value, &v) || v < 0 || v > 1) {
                if (error) {
                    std::ostringstream os;
                    os << "line " << lineNo << ": noise '" << value
                       << "' is not a fraction between 0 and 1";
                    *error = os.str();
                }
                return RideScenario();
            }
            s.m_noise = v;
        } else {
            if (error) {
                std::ostringstream os;
                os << "line " << lineNo << ": unknown directive '" << key << "'";
                *error = os.str();
            }
            return RideScenario();
        }
    }

    if (!modeSeen) {
        if (error)
            *error = "no mode directive: state power, resistance or speed";
        return RideScenario();
    }
    if (s.m_samples.empty()) {
        if (error)
            *error = "no samples";
        return RideScenario();
    }

    s.m_valid = true;
    return s;
}

RideScenario RideScenario::load(const std::string &path, std::string *error) {
    std::ifstream f(path.c_str());
    if (!f) {
        if (error)
            *error = "cannot open " + path;
        return RideScenario();
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    RideScenario s = parse(buf.str(), error);
    if (!s.valid() && error && !error->empty())
        *error = path + ": " + *error;
    return s;
}

double RideScenario::duration() const {
    if (m_samples.empty())
        return 0;
    const RideSample &last = m_samples.back();
    return last.t + last.silence;
}

RidePoint RideScenario::at(double t) const {
    RidePoint p;
    p.t = t;
    p.watts = absentValue;
    p.cadence = absentValue;
    p.hr = absentValue;
    p.speed = absentValue;
    p.resistance = absentValue;
    p.silent = false;

    if (!m_valid)
        return p;

    // A silence window swallows everything inside it, including any sample that would fall in
    // the middle of one. Reporting a value from within a dropout is the bug the dropout
    // scenario exists to catch, so the check comes first and returns.
    for (std::vector<RideSample>::const_iterator it = m_samples.begin(); it != m_samples.end();
         ++it) {
        if (it->silence > 0 && t >= it->t && t < it->t + it->silence) {
            p.silent = true;
            return p;
        }
    }

    p.watts = valueAt(m_samples, &RideSample::watts, t);
    p.cadence = valueAt(m_samples, &RideSample::cadence, t);
    p.hr = valueAt(m_samples, &RideSample::hr, t);
    p.speed = valueAt(m_samples, &RideSample::speed, t);
    p.resistance = valueAt(m_samples, &RideSample::resistance, t);
    return p;
}

std::string RideScenario::toText() const {
    std::ostringstream os;
    if (!m_valid)
        return std::string();

    os << "mode        ";
    switch (m_mode) {
    case RIDE_MODE_POWER:
        os << "power";
        break;
    case RIDE_MODE_RESISTANCE:
        os << "resistance";
        break;
    case RIDE_MODE_SPEED:
        os << "speed";
        break;
    }
    os << '\n';

    if (!m_bike.empty())
        os << "bike        " << m_bike << '\n';
    if (m_startResistance >= 0) {
        os << "resistance  ";
        formatNumber(os, m_startResistance);
        os << '\n';
    }
    if (m_ergLag != defaultErgLag()) {
        os << "erg_lag     ";
        formatNumber(os, m_ergLag);
        os << '\n';
    }
    if (m_noise != defaultNoise()) {
        os << "noise       ";
        formatNumber(os, m_noise);
        os << '\n';
    }
    os << '\n';

    for (std::vector<RideSample>::const_iterator it = m_samples.begin(); it != m_samples.end();
         ++it) {
        os << "t=";
        formatNumber(os, it->t);
        appendField(os, "watts", it->watts);
        appendField(os, "cadence", it->cadence);
        appendField(os, "hr", it->hr);
        appendField(os, "speed", it->speed);
        appendField(os, "resistance", it->resistance);
        if (it->silence > 0) {
            os << " silence=";
            formatNumber(os, it->silence);
        }
        os << '\n';
    }

    return os.str();
}
