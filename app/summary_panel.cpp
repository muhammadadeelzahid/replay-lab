#include "summary_panel.h"

#include "format.h"
#include "session.h"

namespace abrviz {

namespace {

QString row(const QString& name, const QString& value)
{
    return QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
        .arg(name.toHtmlEscaped(), value.toHtmlEscaped());
}

} // namespace

SummaryPanel::SummaryPanel(Session* session, QWidget* parent)
    : QTextBrowser(parent)
    , session_(session)
{
    setObjectName(QStringLiteral("summaryPanel"));
    setOpenExternalLinks(false);
    connect(session_, &Session::runChanged, this, &SummaryPanel::refresh);
    refresh();
}

void SummaryPanel::refresh()
{
    const auto& run = session_->run();
    QString html;
    if (!run) {
        html = tr("<p>No run open.</p>");
        for (const QString& error : session_->loadErrors())
            html += QStringLiteral("<p style='color:#c0392b'>%1</p>").arg(error.toHtmlEscaped());
        setHtml(html);
        return;
    }

    const RunInfo& info = run->info;
    const VideoInfo& video = run->video;
    html += QStringLiteral("<h3>%1</h3><table cellspacing='4'>").arg(info.runId.toHtmlEscaped());
    html += row(tr("Video"), QStringLiteral("%1 (%2 segments)").arg(video.title).arg(video.segmentCount));
    if (!video.source.isEmpty() || !video.license.isEmpty())
        html += row(tr("Source"), QStringLiteral("%1, %2").arg(video.source, video.license));
    html += row(tr("Policy"), QStringLiteral("%1 (%2)").arg(info.policyName, info.policyClient));
    html += row(tr("Feeder link"),
                info.feederLinkRate ? format::bitrate(static_cast<double>(*info.feederLinkRate))
                                    : tr("uncongested"));
    html += row(tr("UEs"), tr("%1 simulated, %2 in this bundle (node %3)")
                               .arg(info.simulatedUes)
                               .arg(info.numUes)
                               .arg(info.nodeId));
    html += row(tr("Seed"), QString::number(info.rngRun));
    html += row(tr("Startup"), format::seconds(run->playbackStart - run->streamStart, 3));
    html += QStringLiteral("</table>");

    html += tr("<h4>Ladder</h4><table cellspacing='4'>");
    for (const Rung& rung : video.ladder) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3 x %4</td></tr>")
                    .arg(rung.index)
                    .arg(format::bitrate(static_cast<double>(rung.bitrate)))
                    .arg(rung.width)
                    .arg(rung.height);
    }
    html += QStringLiteral("</table>");

    if (!run->warnings.empty() || !session_->loadErrors().isEmpty()) {
        html += tr("<h4>Notes</h4><ul>");
        for (const Warning& warning : run->warnings)
            html += QStringLiteral("<li>%1</li>").arg(warning.message.toHtmlEscaped());
        for (const QString& error : session_->loadErrors())
            html += QStringLiteral("<li style='color:#c0392b'>%1</li>").arg(error.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    }
    setHtml(html);
}

} // namespace abrviz
