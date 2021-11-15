﻿#ifndef PROXYWIDGET_H
#define PROXYWIDGET_H

    #include <QtWidgets>
    #include <QtCharts>
    #include "chartView.h"
    #include "meteoPoint.h"
    #include "interpolationSettings.h"
    #include "interpolationPoint.h"

    class Crit3DProxyWidget : public QWidget
    {
        Q_OBJECT

        public:
            Crit3DProxyWidget(Crit3DInterpolationSettings* interpolationSettings, QList<Crit3DInterpolationDataPoint> &primaryList, QList<Crit3DInterpolationDataPoint> &supplementalList, QList<Crit3DInterpolationDataPoint> &secondaryList, frequencyType currentFrequency, QDate currentDate, int currentHour);
            ~Crit3DProxyWidget();
            void closeEvent(QCloseEvent *event);
            void updateDateTime(QDate newDate, int newHour);
            void updateFrequency(frequencyType newFrequency);
            void changeProxyPos(const QString proxyName);
            void changeVar(const QString varName);
            void plot();
            void climatologicalLRClicked(int toggled);
            void computeHighestStationIndex();
            void updatePointList(const QList<Crit3DInterpolationDataPoint> &primaryValue, const QList<Crit3DInterpolationDataPoint> &secondaryValue, const QList<Crit3DInterpolationDataPoint> &supplementalValue );

    private:
            Crit3DInterpolationSettings* interpolationSettings;
            QList<Crit3DInterpolationDataPoint> primaryList;
            QList<Crit3DInterpolationDataPoint> secondaryList;
            QList<Crit3DInterpolationDataPoint> supplementalList;
            frequencyType currentFrequency;
            QDate currentDate;
            int currentHour;
            QComboBox variable;
            QComboBox axisX;
            QCheckBox detrended;
            QCheckBox climatologicalLR;
            QCheckBox modelLP;
            QCheckBox zeroIntercept;
            QTextEdit r2;
            QTextEdit lapseRate;
            QTextEdit r2ThermalLevels;
            ChartView *chartView;
            meteoVariable myVar;
            int proxyPos;

            //int highestStationIndex;
            double zMax;
            double zMin;
            //int highestStationBelongToList;

    signals:
        void closeProxyWidget();
    };


#endif // PROXYWIDGET_H
