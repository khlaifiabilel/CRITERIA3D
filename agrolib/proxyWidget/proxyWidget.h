﻿#ifndef PROXYWIDGET_H
#define PROXYWIDGET_H

    #include <QtWidgets>
    #include <QtCharts>
    #include "chartView.h"
    #include "meteoPoint.h"
    #include "interpolationSettings.h"

    class Crit3DProxyWidget : public QWidget
    {
        Q_OBJECT

        public:
            Crit3DProxyWidget(Crit3DInterpolationSettings* interpolationSettings, QList<Crit3DMeteoPoint> &primaryList, QList<Crit3DMeteoPoint> &secondaryList, frequencyType currentFrequency, QDateTime currentDateTime);
            ~Crit3DProxyWidget();
            void closeEvent(QCloseEvent *event);
            void updateDateTime(QDateTime newDateTime);
            void updateFrequency(frequencyType newFrequency);
            void changeProxyPos(const QString proxyName);
            void changeVar(const QString varName);
            void plot();

    private:
            Crit3DInterpolationSettings* interpolationSettings;
            QList<Crit3DMeteoPoint> primaryList;
            QList<Crit3DMeteoPoint> secondaryList;
            frequencyType currentFrequency;
            QDateTime currentDateTime;
            QComboBox variable;
            QComboBox axisX;
            QCheckBox detrended;
            QCheckBox climatologyLR;
            QCheckBox modelLP;
            QCheckBox zeroIntercept;
            QTextEdit r2;
            QTextEdit lapseRate;
            QTextEdit r2ThermalLevels;
            ChartView *chartView;
            meteoVariable myVar;
            int proxyPos;

    signals:
        void closeProxyWidget();
    };


#endif // PROXYWIDGET_H
