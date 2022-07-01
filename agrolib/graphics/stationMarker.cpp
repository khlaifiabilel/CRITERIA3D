#include "commonConstants.h"
#include "basicMath.h"
#include "stationMarker.h"
#include "qdebug.h"

#include <QMenu>

StationMarker::StationMarker(qreal radius,bool sizeIsZoomInvariant, QColor fillColor, MapGraphicsView* view, MapGraphicsObject *parent) :
    CircleObject(radius, sizeIsZoomInvariant, fillColor, parent)
{
    this->setFlag(MapGraphicsObject::ObjectIsSelectable, false);
    this->setFlag(MapGraphicsObject::ObjectIsMovable, false);
    this->setFlag(MapGraphicsObject::ObjectIsFocusable, false);
    _view = view;
    _id = "";
    _name = "";
    _dataset = "";
    _altitude = NODATA;
    _lapseRateCode = primary;
    _municipality = "";
    _active = true;
}

void StationMarker::setId(std::string id)
{
    _id = id;
}

std::string StationMarker::id() const
{
    return _id;
}

void StationMarker::setName(const std::string &name)
{
    _name = name;
}

void StationMarker::setDataset(const std::string &dataset)
{
    _dataset = dataset;
}

void StationMarker::setAltitude(double altitude)
{
    _altitude = altitude;
}

void StationMarker::setLapseRateCode(lapseRateCodeType code)
{
    _lapseRateCode = code;
}

void StationMarker::setMunicipality(const std::string &municipality)
{
    _municipality = municipality;
}

void StationMarker::setQuality(const quality::qualityType &quality)
{
    _quality = quality;
}

bool StationMarker::active() const
{
    return _active;
}

void StationMarker::setActive(bool active)
{
    _active = active;
}

void StationMarker::setToolTip()
{
    QString idpoint = QString::fromStdString(_id);
    QString name = QString::fromStdString(_name);
    QString dataset = QString::fromStdString(_dataset);
    QString altitude = QString::number(_altitude);
    QString municipality = QString::fromStdString(_municipality);
    QString lapseRateName = QString::fromStdString(getLapseRateCodeName(_lapseRateCode));

    QString toolTipText = QString("Point: <b> %1 </b> <br/> ID: %2 <br/> dataset: %3 <br/> altitude: %4 m <br/> municipality: %5 <br/> lapse rate code: %6")
                            .arg(name, idpoint, dataset, altitude, municipality, lapseRateName);

    double value = currentValue();
    if (! isEqual(value, NODATA))
    {
        QString valueStr = QString::number(value, 'f', 1);

        QString myQuality = "";
        if (_quality == quality::wrong_syntactic)
            myQuality = "WRONG DATA (syntax control)";
        if (_quality == quality::wrong_spatial)
            myQuality = "WRONG DATA (spatial control)";

        toolTipText = QString("value: <b> %1 <br/> %2 <br/> </b>").arg(valueStr, myQuality) + toolTipText;
    }

    CircleObject::setToolTip(toolTipText);
}


void StationMarker::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{  
    if (event->button() == Qt::RightButton)
    {
        bool isGrid = false;
        QMenu menu;
        QAction *openMeteoWidget = menu.addAction("Open new meteo widget");
        QAction *appendMeteoWidget = menu.addAction("Append to last meteo widget");
        QAction *openPointStatisticsWidget = menu.addAction("Open point statistics widget");
        QAction *openHomogeneityWidget = menu.addAction("Open homogeneity test widget");
        QMenu *orogCodeSubMenu;
        orogCodeSubMenu = menu.addMenu("Orog code");
        QAction *actionOrogCode_primary = orogCodeSubMenu->addAction( "Set as primary station" );
        QAction *actionOrogCode_secondary = orogCodeSubMenu->addAction( "Set as secondary station" );
        QAction *actionOrogCode_supplemental = orogCodeSubMenu->addAction( "Set as supplemental station" );

        QAction *selection =  menu.exec(QCursor::pos());

        if (selection != nullptr)
        {
            if (selection == openMeteoWidget)
            {
                emit newStationClicked(_id, _name, isGrid);
            }
            else if (selection == appendMeteoWidget)
            {
                emit appendStationClicked(_id, _name, isGrid);
            }
            else if (selection == openPointStatisticsWidget)
            {
                emit newPointStatisticsClicked(_id, isGrid);
            }
            else if (selection == openHomogeneityWidget)
            {
                emit newHomogeneityTestClicked(_id);
            }
            else if (selection == actionOrogCode_primary)
            {
                emit changeOrogCodeClicked(_id, 0);
            }
            else if (selection == actionOrogCode_secondary)
            {
                emit changeOrogCodeClicked(_id, 1);
            }
            else if (selection == actionOrogCode_supplemental)
            {
                emit changeOrogCodeClicked(_id, 2);
            }
        }
    }
}

void StationMarker::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
}

