#pragma once

#include <QWidget>
#include <QTimer>
#include <functional>

/// Custom VU meter widget showing audio level as a horizontal bar.
/// Updates at ~30ms intervals via a callback function.
class VuMeter : public QWidget {
    Q_OBJECT

public:
    explicit VuMeter(QWidget* parent = nullptr);

    /// Set the function that returns the current level (0.0 - 1.0).
    void setLevelSource(std::function<float()> source);

    /// Start updating the meter.
    void start();

    /// Stop updating.
    void stop();

    QSize minimumSizeHint() const override { return QSize(100, 12); }
    QSize sizeHint() const override { return QSize(200, 16); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::function<float()> m_levelSource;
    QTimer m_updateTimer;
    float m_level = 0.0f;
    float m_peak = 0.0f;
    static constexpr float PeakDecay = 0.95f;
};
