#ifndef BLUETOOTHDEVICETYPE_H
#define BLUETOOTHDEVICETYPE_H

// BIKE keeps the value it had when TREADMILL sat in front of it. The number is not
// only internal: templateinfosenderbuilder hands it to template JavaScript as
// BIKE_TYPE, and a template comparing against a literal 2 would otherwise break.
enum BLUETOOTH_TYPE { UNKNOWN = 0, BIKE = 2 };

#endif // BLUETOOTHDEVICETYPE_H