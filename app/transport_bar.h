#pragma once

#include "abrviz/media/replay_source.h"

#include <QWidget>

class QAction;
class QLabel;
class QToolButton;

namespace abrviz {

class ReplayPlayer;
class SegmentTimeline;
class Session;

/// The strip under every view while a run is open, styled after QuickTime Player: elapsed
/// replay time, borderless playback buttons, the state and remaining time, and the segment
/// timeline, which doubles as the scrubber.
///
/// The buttons show the window's actions (so the menu, the shortcuts and the buttons are the
/// same commands). The labels have fixed widths: they change many times a second while the
/// replay runs, and a label that grew would lay out (and repaint) the whole window.
class TransportBar final : public QWidget {
    Q_OBJECT

public:
    struct Actions {
        QAction* previousSegment = nullptr;
        QAction* stepBack = nullptr;
        QAction* play = nullptr;
        QAction* stepForward = nullptr;
        QAction* nextSegment = nullptr;
    };

    TransportBar(Session* session, ReplayPlayer* player, SegmentTimeline* timeline,
                 const Actions& actions, QWidget* parent = nullptr);

    [[nodiscard]] QString timeText() const;
    [[nodiscard]] QString stateText() const;

private:
    void update();

    Session* session_;      // not owned
    ReplayPlayer* player_;  // not owned
    QLabel* time_;       // elapsed replay time
    QLabel* remaining_;  // remaining replay time
    QLabel* state_;      // state and simulation time
};

} // namespace abrviz
