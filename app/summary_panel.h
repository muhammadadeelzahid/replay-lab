#pragma once

/**
 * @file
 * @brief The summary panel that describes the open run.
 */

#include <QTextBrowser>

namespace abrviz {

class Session;

/**
 * @brief Run information, the ladder, warnings and load errors of the open run.
 *
 * Rebuilt from the session whenever `Session::runChanged` is emitted.
 */
class SummaryPanel final : public QTextBrowser {
    Q_OBJECT

public:
    /**
     * @brief Creates the panel and fills it from the current run.
     *
     * @param session Session to read from; not owned, must outlive the panel.
     * @param parent Parent widget, or null.
     */
    explicit SummaryPanel(Session* session, QWidget* parent = nullptr);

private:
    /**
     * @brief Rebuilds the HTML from the session's run and load errors.
     *
     * With no run open, shows "No run open." followed by the load errors.
     */
    void refresh();

    Session* session_;  ///< Not owned; outlives the panel.
};

} // namespace abrviz
