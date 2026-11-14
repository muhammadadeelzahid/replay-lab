#pragma once

#include <QTextBrowser>

namespace abrviz {

class Session;

/// Run information, the ladder, warnings and load errors of the open run.
class SummaryPanel final : public QTextBrowser {
    Q_OBJECT

public:
    explicit SummaryPanel(Session* session, QWidget* parent = nullptr);

private:
    void refresh();

    Session* session_;  // not owned; outlives the panel
};

} // namespace abrviz
