#pragma once

#include "abrviz/core/model/run_data.h"

#include <QString>

namespace abrviz {

/// Reads the network the run was simulated on from the set-up lines at the top of `sim.log`:
/// the satellite gNB and its position, the IAB relays, the carrier, and which cell the UE
/// `nodeId` attached to and how far away it was. Only the first lines are read. Missing lines
/// leave the fields empty.
[[nodiscard]] NetworkTopology readNetworkTopology(const QString& simLogPath, int nodeId);

/// The same from the text of those lines (for tests).
[[nodiscard]] NetworkTopology parseNetworkTopology(const QString& text, int nodeId);

} // namespace abrviz
