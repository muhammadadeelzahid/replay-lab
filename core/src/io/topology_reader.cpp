#include "abrviz/core/io/topology_reader.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace abrviz {

namespace {

/**
 * @brief Lines read from the top of `sim.log`; the set-up lines all come before the first frame.
 */
constexpr int kHeaderLines = 600;

/**
 * @brief Reads a captured group as a number.
 *
 * @param match The regular expression match.
 * @param group Index of the capture group.
 * @return The number, or nothing if the group is not a number.
 */
std::optional<double> number(const QRegularExpressionMatch& match, int group)
{
    bool ok = false;
    const double value = match.captured(group).toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

} // namespace

NetworkTopology parseNetworkTopology(const QString& text, int nodeId)
{
    static const QRegularExpression satellite(
        QStringLiteral(R"(Install Satellite eNB with CellId (\d+) on Node ID (\d+) at position \(([-\d.e]+), ([-\d.e]+), ([-\d.e]+)\))"));
    static const QRegularExpression relay(
        QStringLiteral(R"(Install IAB device .* on Node ID (\d+) at position \(([-\d.e]+), ([-\d.e]+), ([-\d.e]+)\))"));
    static const QRegularExpression frequency(QStringLiteral(R"(Center Frequency: ([\d.]+) GHz)"));
    static const QRegularExpression bandwidth(QStringLiteral(R"(Total Bandwidth: ([\d.]+) MHz)"));
    static const QRegularExpression attachCall(
        QStringLiteral(R"(AttachToSingleClosestEnb called for UE device: .* node_id: (\d+))"));
    static const QRegularExpression closest(
        QStringLiteral(R"(Closest eNB found at distance: ([\d.e]+) node_id: (\d+))"));
    static const QRegularExpression relayDistance(QStringLiteral(R"(^distance ([\d.e]+)$)"));

    NetworkTopology topology;
    int pendingUe = -1;
    bool afterRelay = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (auto m = satellite.match(line); m.hasMatch()) {
            topology.satelliteNode = m.captured(2).toInt();
            topology.satelliteAltitude = number(m, 5);
            continue;
        }
        if (auto m = relay.match(line); m.hasMatch()) {
            ++topology.relayCount;
            afterRelay = true;
            continue;
        }
        if (auto m = frequency.match(line); m.hasMatch()) {
            topology.carrierFrequencyGHz = number(m, 1);
            continue;
        }
        if (auto m = bandwidth.match(line); m.hasMatch()) {
            topology.bandwidthMHz = number(m, 1);
            continue;
        }
        // The relay's attach to the satellite is logged as "distance D".
        if (auto m = relayDistance.match(line); m.hasMatch() && afterRelay && !topology.backhaulDistance) {
            topology.backhaulDistance = number(m, 1);
            continue;
        }
        if (auto m = attachCall.match(line); m.hasMatch()) {
            pendingUe = m.captured(1).toInt();
            continue;
        }
        if (auto m = closest.match(line); m.hasMatch() && pendingUe == nodeId) {
            topology.accessDistance = number(m, 1);
            topology.servingNode = m.captured(2).toInt();
            pendingUe = -1;
        }
    }
    topology.viaRelay = topology.servingNode && topology.satelliteNode
        && *topology.servingNode != *topology.satelliteNode;
    return topology;
}

NetworkTopology readNetworkTopology(const QString& simLogPath, int nodeId)
{
    QFile file(simLogPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream stream(&file);
    QString text;
    for (int i = 0; i < kHeaderLines && !stream.atEnd(); ++i)
        text += stream.readLine() + QLatin1Char('\n');
    return parseNetworkTopology(text, nodeId);
}

} // namespace abrviz
