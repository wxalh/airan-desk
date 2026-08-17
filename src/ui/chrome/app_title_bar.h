#ifndef APP_TITLE_BAR_H
#define APP_TITLE_BAR_H

#include <QPoint>
#include <QSize>
#include <QWidget>

class QIcon;
class QEvent;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QMouseEvent;
class QTimer;

class AppTitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit AppTitleBar(QWidget *targetWindow, bool showMinimize = true, bool showMaximize = true, QWidget *parent = nullptr);
    ~AppTitleBar() override;

    void setTitle(const QString &title);
    void setUiScale(double scale);
    void setResizeAspectRatio(const QSize &baseSize);

signals:
    void uiScaleChanged();

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void updateMaximizeButton();

private:
    QPushButton *createButton(const QIcon &icon, const QString &tooltip);
    int scaled(int value) const;
    double dpiScale() const;
    double effectiveScale() const;
    void applyScale();
    void applyStyle();
    void enableAncestorMouseTracking();
    void syncButtonHoverFromCursor();
    void toggleMaximize();
    Qt::Edges hitTest(const QPoint &globalPos) const;
    void updateCursor(const QPoint &globalPos);
    QRect applyResizeAspectRatio(const QRect &geometry, const QPoint &globalPos) const;

    QWidget *m_targetWindow{nullptr};
    QHBoxLayout *m_layout{nullptr};
    QLabel *m_iconLabel{nullptr};
    QLabel *m_titleLabel{nullptr};
    QPushButton *m_minimizeButton{nullptr};
    QPushButton *m_maximizeButton{nullptr};
    QPushButton *m_closeButton{nullptr};
    bool m_dragging{false};
    bool m_resizing{false};
    double m_scale{1.0};
    bool m_customScale{false};
    QTimer *m_hoverTimer{nullptr};
    Qt::Edges m_resizeEdges{Qt::Edges()};
    QPoint m_dragOffset;
    QSize m_resizeAspectBase;
};

#endif /* APP_TITLE_BAR_H */
