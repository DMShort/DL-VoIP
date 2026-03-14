#include "vu_meter.h"

#include <QPainter>
#include <algorithm>

VuMeter::VuMeter(QWidget* parent)
    : QWidget(parent)
{
    m_updateTimer.setInterval(30); // ~33 fps
    connect(&m_updateTimer, &QTimer::timeout, this, [this]() {
        if (m_levelSource) {
            m_level = std::clamp(m_levelSource(), 0.0f, 1.0f);

            // Peak hold with decay
            if (m_level > m_peak) {
                m_peak = m_level;
            } else {
                m_peak *= PeakDecay;
            }
        }
        update();
    });
}

void VuMeter::setLevelSource(std::function<float()> source) {
    m_levelSource = std::move(source);
}

void VuMeter::start() {
    m_updateTimer.start();
}

void VuMeter::stop() {
    m_updateTimer.stop();
    m_level = 0.0f;
    m_peak = 0.0f;
    update();
}

void VuMeter::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(0, 0, w, h, QColor(30, 30, 30));

    // Level bar with color gradient
    int levelWidth = static_cast<int>(m_level * w);
    if (levelWidth > 0) {
        // Green for low, yellow for medium, red for high
        QColor barColor;
        if (m_level < 0.5f) {
            barColor = QColor(0, 180, 0);
        } else if (m_level < 0.8f) {
            barColor = QColor(200, 200, 0);
        } else {
            barColor = QColor(200, 0, 0);
        }
        painter.fillRect(0, 0, levelWidth, h, barColor);
    }

    // Peak indicator
    int peakX = static_cast<int>(m_peak * w);
    if (peakX > 1 && peakX < w) {
        painter.setPen(QColor(255, 255, 255, 180));
        painter.drawLine(peakX, 0, peakX, h);
    }

    // Border
    painter.setPen(QColor(60, 60, 60));
    painter.drawRect(0, 0, w - 1, h - 1);
}
