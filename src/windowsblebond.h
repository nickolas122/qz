#ifndef WINDOWSBLEBOND_H
#define WINDOWSBLEBOND_H

#include <QtGlobal>

// Windows stores a pairing record per BLE device. Plenty of trainer consoles
// forget their half of the bond over a power cycle, and Windows does not notice:
// it keeps serving reads from its GATT cache while refusing every write with
// "access denied". The connection looks healthy - services discovered, battery
// level updating, device shown as connected - and simply carries no data.
//
// Only removing the record and pairing again clears that state.
namespace windowsblebond {

// Drops the Windows pairing record for `address`. Returns true when Windows
// confirms the removal. Always false off Windows.
bool removeBond(quint64 address);

// Opens the Bluetooth pane of Windows Settings so the trainer can be paired
// again. This step cannot be automated: the Win32 GATT path finds services
// through SetupDiGetClassDevs(DIGCF_PRESENT), which only enumerates devices
// Windows has already paired, and Win32 offers no usable LE pairing call.
void openPairingSettings();

} // namespace windowsblebond

#endif // WINDOWSBLEBOND_H
