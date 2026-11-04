#ifndef AIRAN_MAIN_WINDOW_RESPONSIVE_LAYOUT_H
#define AIRAN_MAIN_WINDOW_RESPONSIVE_LAYOUT_H

#include <QGridLayout>
#include <QWidget>

inline bool applyMainPanelsResponsiveLayout(QGridLayout *layout,
                                            QWidget *localPanel,
                                            QWidget *remotePanel,
                                            int availableWidth,
                                            int wideBreakpoint)
{
    if (!layout || !localPanel || !remotePanel)
        return false;

    const bool wide = availableWidth >= wideBreakpoint;
    int localRow = -1;
    int localColumn = -1;
    int remoteRow = -1;
    int remoteColumn = -1;
    int rowSpan = 0;
    int columnSpan = 0;
    const int localIndex = layout->indexOf(localPanel);
    const int remoteIndex = layout->indexOf(remotePanel);
    if (localIndex >= 0)
        layout->getItemPosition(localIndex, &localRow, &localColumn, &rowSpan, &columnSpan);
    if (remoteIndex >= 0)
        layout->getItemPosition(remoteIndex, &remoteRow, &remoteColumn, &rowSpan, &columnSpan);

    const int expectedRemoteRow = wide ? 0 : 1;
    const int expectedRemoteColumn = wide ? 1 : 0;
    if (localRow != 0 || localColumn != 0 ||
        remoteRow != expectedRemoteRow || remoteColumn != expectedRemoteColumn)
    {
        layout->removeWidget(localPanel);
        layout->removeWidget(remotePanel);
        layout->addWidget(localPanel, 0, 0);
        layout->addWidget(remotePanel, expectedRemoteRow, expectedRemoteColumn);
    }

    layout->setColumnStretch(0, wide ? 38 : 1);
    layout->setColumnStretch(1, wide ? 62 : 0);
    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 0);
    return wide;
}

#endif
