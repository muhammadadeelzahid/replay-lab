#pragma once

/**
 * @file
 * @brief Reading the simulated network from the set-up lines of `sim.log`.
 */

#include "abrviz/core/model/run_data.h"

#include <QString>

namespace abrviz {

/**
 * @brief Reads the network the run was simulated on from the top of `sim.log`.
 *
 * Finds the satellite gNB and its position, the IAB relays, the carrier, and which cell the UE
 * `nodeId` attached to and how far away it was. Only the first lines are read. Missing lines
 * leave the fields empty.
 *
 * @param simLogPath Path of `sim.log`.
 * @param nodeId The ns-3 node of the UE.
 * @return The topology; empty if the file cannot be opened.
 */
[[nodiscard]] NetworkTopology readNetworkTopology(const QString& simLogPath, int nodeId);

/**
 * @brief Reads the network from the text of the set-up lines (for tests).
 *
 * @param text The set-up lines of `sim.log`.
 * @param nodeId The ns-3 node of the UE.
 * @return The topology; fields whose lines are missing stay empty.
 * @see readNetworkTopology()
 */
[[nodiscard]] NetworkTopology parseNetworkTopology(const QString& text, int nodeId);

} // namespace abrviz
