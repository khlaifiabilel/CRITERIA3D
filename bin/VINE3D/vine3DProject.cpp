#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDir>
#include <QDateTime>

#include <iostream>
#include <math.h>

#include "utilities.h"
#include "commonConstants.h"
#include "meteo.h"
#include "interpolation.h"
#include "solarRadiation.h"
#include "waterBalance.h"
#include "plant.h"
#include "dataHandler.h"
#include "modelCore.h"
#include "atmosphere.h"
#include "disease.h"
#include "vine3DShell.h"
#include "vine3DProject.h"
#include "soilDbTools.h"
#include "soilFluxes3D.h"


Vine3DProject::Vine3DProject() : Project3D()
{
    initializeVine3DProject();
}


void Vine3DProject::initializeVine3DProject()
{
    dbVine3DFileName = "";

    isObsDataLoaded = false;
    setCurrentFrequency(hourly);

    computeDiseases = false;

    dailyOutputPath = "daily_output/";
    hourlyOutputPath = "hourly_ouptut/";
    fieldMapName = "";

    lastDateTransmissivity.setDate(1900,1,1);

    cultivar.clear();
    trainingSystems.clear();
    modelCases.clear();
    fieldBook.clear();

    statePlant.stateGrowth.initialize();
    statePlant.statePheno.initialize();
}


bool Vine3DProject::loadVine3DSettings()
{
    //eventually put Vine3D generic settings
    return true;
}


void Vine3DProject::clearVine3DProject()
{
    if (isProjectLoaded)
    {
        logInfo("Close Project");
        dbVine3D.close();

        dbVine3DFileName = "";

        modelCaseIndexMap.clear();

        clearProject3D();

        delete vine3DMapsH;
        delete vine3DMapsD;
    }
}

bool Vine3DProject::loadVine3DProjectSettings(QString projectFile)
{
    if (! QFile(projectFile).exists())
    {
        logError("Project file not found: " + projectFile);
        return false;
    }

    projectSettings->beginGroup("project");
        fieldMapName = projectSettings->value("modelCaseMap").toString();
        dbVine3DFileName = projectSettings->value("db_vine3d").toString();
        soilDbFileName = projectSettings->value("soil_db").toString();
        if (soilDbFileName == "")
            soilDbFileName = projectSettings->value("db_soil").toString();
    projectSettings->endGroup();

    projectSettings->beginGroup("settings");
        computationSoilDepth = projectSettings->value("soil_depth").toDouble();
        computeDiseases = projectSettings->value("compute_diseases").toBool();
    projectSettings->endGroup();

    return true;
}

bool Vine3DProject::openVine3DDatabase(QString fileName)
{
    if (fileName == "")
    {
        logError("Missing VINE3D DB filename");
        return false;
    }

    dbVine3D = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
    dbVine3D.setDatabaseName(getCompleteFileName(fileName, ""));

    if (!dbVine3D.open())
    {
       errorString = "Connection with database fail";
       return false;
    }

    logInfo("VINE3D database = " + fileName);
    return true;
}

bool Vine3DProject::loadVine3DProject(QString myFileName)
{
    clearVine3DProject();

    initializeProject();
    initializeProject3D();
    initializeVine3DProject();

    if (myFileName == "") return(false);

    if (! loadProjectSettings(myFileName))
        return false;

    if (! loadVine3DProjectSettings(myFileName))
        return false;

    if (dbVine3DFileName != "") openVine3DDatabase(dbVine3DFileName);

    if (! loadProject())
        return false;

    logInfo("Initialize DEM and project maps...");

    vine3DMapsH = new Vine3DHourlyMaps(DEM);
    vine3DMapsD = new Crit3DDailyMeteoMaps(DEM);

    statePlantMaps = new Crit3DStatePlantMaps(DEM);

    // soil data
    if (soilDbFileName != "") loadSoilDatabase(soilDbFileName);
    computationSoilDepth = findSoilMaxDepth();

    // VINE3D parameters
    if (!loadGrapevineParameters() || !loadTrainingSystems() || !loadFieldsProperties() || !loadFieldBook())
    {
        logError();
        dbVine3D.close();
        return false;
    }

    if (!loadFieldShape())
    {
        myFileName = getCompleteFileName(fieldMapName, PATH_GEO);
        if (!loadFieldMap(myFileName)) return false;
    }

    if (! setVine3DSoilIndexMap())
        return false;

    if (! initializeWaterBalance3D())
    {
        logError();
        return false;
    }
    outputWaterBalanceMaps = new Crit3DWaterBalanceMaps(DEM);

    if (! initializeGrapevine(this))
    {
        logError();
        return false;
    }

    logInfo("Project loaded");
    isProjectLoaded = true;

    return true;
}


bool Vine3DProject::loadGrapevineParameters()
{
    logInfo ("Read grapevine parameters...");

    QSqlQuery myQuery(dbVine3D);

    myQuery.prepare(
            " SELECT id_cultivar, name,"
            " phenovitis_force_physiological_maturity, miglietta_radiation_use_efficiency,"
            " miglietta_d, miglietta_f, miglietta_fruit_biomass_offset,"
            " miglietta_fruit_biomass_slope,"
            " hydrall_psileaf, hydrall_stress_threshold,"
            " hydrall_vpd , hydrall_alpha_leuning,"
            " phenovitis_ecodormancy, phenovitis_critical_chilling,"
            " phenovitis_force_flowering, phenovitis_force_veraison,"
            " phenovitis_force_fruitset, degree_days_veraison, hydrall_carbox_rate"
            " FROM cultivar"
            " ORDER BY id_cultivar");

    if (! myQuery.exec())
    {
        errorString = "Error reading grapevine parameters:\n" + myQuery.lastError().text();
        return false;
    }

    //read values
    TVineCultivar myCultivar;

    while (myQuery.next())
    {
        myCultivar.id = myQuery.value(0).toInt();
        myCultivar.parameterPhenoVitis.criticalForceStatePhysiologicalMaturity = myQuery.value(2).toDouble();
        myCultivar.parameterBindiMiglietta.radiationUseEfficiency = myQuery.value(3).toDouble();
        myCultivar.parameterBindiMiglietta.d = myQuery.value(4).toDouble();
        myCultivar.parameterBindiMiglietta.f = myQuery.value(5).toDouble();
        myCultivar.parameterBindiMiglietta.fruitBiomassOffset = myQuery.value(6).toDouble();
        myCultivar.parameterBindiMiglietta.fruitBiomassSlope = myQuery.value(7).toDouble();
        myCultivar.parameterWangLeuning.psiLeaf = myQuery.value(8).toDouble();
        myCultivar.parameterWangLeuning.waterStressThreshold = myQuery.value(9).toDouble();
        myCultivar.parameterWangLeuning.sensitivityToVapourPressureDeficit = myQuery.value(10).toDouble();
        myCultivar.parameterWangLeuning.alpha = myQuery.value(11).toDouble() * 1E5;
        myCultivar.parameterPhenoVitis.co1 = myQuery.value(12).toDouble();
        myCultivar.parameterPhenoVitis.criticalChilling = myQuery.value(13).toDouble();
        myCultivar.parameterPhenoVitis.criticalForceStateFlowering = myQuery.value(14).toDouble();
        myCultivar.parameterPhenoVitis.criticalForceStateVeraison = myQuery.value(15).toDouble();
        myCultivar.parameterPhenoVitis.criticalForceStateFruitSet = myQuery.value(16).toDouble();
        myCultivar.parameterPhenoVitis.degreeDaysAtVeraison = myQuery.value(17).toDouble();
        myCultivar.parameterWangLeuning.maxCarboxRate = myQuery.value(18).toDouble();

        cultivar.push_back(myCultivar);
    }

    return true;
}


bool Vine3DProject::loadTrainingSystems()
{
    logInfo ("Read training system...");

    QSqlQuery myQuery(dbVine3D);

    myQuery.prepare(
            " SELECT id_training_system, nr_shoots_plant, row_width, row_height,"
            " row_distance, plant_distance"
            " FROM training_system"
            " ORDER BY id_training_system");

    if (! myQuery.exec())
    {
        errorString = "Error reading training systems. " + myQuery.lastError().text();
        return false;
    }

    TtrainingSystem mySystem;

    while (myQuery.next())
    {
        mySystem.id = myQuery.value(0).toInt();
        mySystem.shootsPerPlant = myQuery.value(1).toFloat();
        mySystem.rowWidth = myQuery.value(2).toFloat();
        mySystem.rowHeight = myQuery.value(3).toFloat();
        mySystem.rowDistance = myQuery.value(4).toFloat();
        mySystem.plantDistance = myQuery.value(5).toFloat();

        trainingSystems.push_back(mySystem);
    }

    return(true);
}


bool Vine3DProject::loadFieldBook()
{
    logInfo ("Read field book table...");

    QSqlQuery myQuery(dbVine3D);

    myQuery.prepare(
            " SELECT date_, id_field, irrigated, grass, pinchout, leaf_removal,"
            " harvesting_performed, cluster_thinning, tartaric_acid, irrigation_hours, thinning_percentage"
            " FROM field_book"
            " ORDER BY date_, id_field");

    if (! myQuery.exec())
    {
        errorString = "missing field_book\n" + myQuery.lastError().text();
        return false;
    }

    TVine3DOperation myOperation;

    int idBook = 0;
    while (myQuery.next())
    {
        for (int i = 2; i <= 8; i++)
        {
            if (myQuery.value(i).toFloat() > 0)
            {
                myOperation.operationDate = myQuery.value(0).toDate();
                myOperation.idField = myQuery.value(1).toInt();

                //irrigation
                if (i == 2)
                {
                    myOperation.operation = irrigationOperation;
                    myOperation.quantity = myQuery.value(9).toFloat();
                    fieldBook.push_back(myOperation);
                }
                //grass sowing/removal
                if (i == 3)
                {
                    if (myQuery.value(3).toInt() == 1)
                        myOperation.operation = grassSowing;
                    else if (myQuery.value(3).toInt() > 1)
                        myOperation.operation = grassRemoving;

                    myOperation.quantity = 0.0;
                    fieldBook.push_back(myOperation);
                }
                //pinchout == trimming
                if (i == 4)
                {
                    this->fieldBook[idBook].operation = trimming;
                    this->fieldBook[idBook].quantity = 2.5;
                    fieldBook.push_back(myOperation);
                }
                //leaf removal
                if (i == 5)
                {
                    this->fieldBook[idBook].operation = leafRemoval;
                    this->fieldBook[idBook].quantity = 3.0;
                    fieldBook.push_back(myOperation);
                }
                //harvesting
                if (i == 6)
                {
                    this->fieldBook[idBook].operation = harvesting;
                    this->fieldBook[idBook].quantity = 0.0;
                    fieldBook.push_back(myOperation);
                }
                //cluster thinning
                if (i == 7)
                {
                    this->fieldBook[idBook].operation = clusterThinning;
                    this->fieldBook[idBook].quantity = myQuery.value(10).toFloat();
                    fieldBook.push_back(myOperation);
                }
                //tartaric acid analysis
                if (i == 8)
                {
                    this->fieldBook[idBook].operation = tartaricAnalysis;
                    this->fieldBook[idBook].quantity = myQuery.value(i).toFloat();
                    fieldBook.push_back(myOperation);
                }
            }
        }
    }

    return true;
}


int Vine3DProject::queryFieldPoint(double x, double y)
{
    QString UTMx = QString::number(x, 'f', 1);
    QString UTMy = QString::number(y, 'f', 1);
    QString UTMZone = QString::number(this->gisSettings.utmZone);

    QString myQueryString = "SELECT id_field FROM fields_shp";
    myQueryString += " WHERE ST_Contains(geom, ST_GeomFromText";
    myQueryString += "('POINT(" + UTMx + " " + UTMy + ")', 326" + UTMZone + ")) = true";
    QSqlQuery myQuery;

    myQuery = this->dbVine3D.exec(myQueryString);

    if (myQuery.size() == -1)
    {
        this->errorString = myQuery.lastError().text();
        this->logError();
        return(NODATA);
    }

    if (myQuery.size() > 0)
    {
        myQuery.next();
        return myQuery.value(0).toInt();
    }
    else
        return NODATA;
}


bool Vine3DProject::loadFieldShape()
{
    return false;
    /* to be revised
    this->logInfo ("Read Fields...");
    int dim = 1;
    int i, j, id;
    double x0, y0;
    std::vector <float> valuesList;

    QString myQueryString = "SELECT id_field FROM fields_shp";

    QSqlQuery myQuery;
    myQuery = this->dbConnection.exec(myQueryString);
    if (myQuery.size() == -1)
    {
        this->errorString = myQuery.lastError().text();
        return(false);
    }
    myQuery.clear();

    this->modelCaseIndexMap.initializeGrid(this->DEM);

    double step = this->modelCaseIndexMap.header->cellSize / (2*dim+1);

    for (long row = 0; row < this->modelCaseIndexMap.header->nrRows ; row++)
        for (long col = 0; col < this->modelCaseIndexMap.header->nrCols; col++)
            if (this->DEM.value[row][col] != this->DEM.header->flag)
            {
                //center
                gis::getUtmXYFromRowCol(this->modelCaseIndexMap, row, col, &x0, &y0);
                id = queryFieldPoint(x0, y0);
                if (id != NODATA)
                    this->modelCaseIndexMap.value[row][col] = id;
                else
                {
                    valuesList.resize(0);
                    for (i = -dim; i <= dim; i++)
                        for (j = -dim; j <= dim; j++)
                            if ((i != 0)|| (j != 0))
                            {
                                id = queryFieldPoint(x0+(i*step), y0+(j*step));
                                if (id != NODATA)
                                    valuesList.push_back(id);
                            }
                    if (valuesList.size() == 0)
                        this->modelCaseIndexMap.value[row][col] = this->modelCaseIndexMap.header->flag;
                    else
                        this->modelCaseIndexMap.value[row][col] = gis::prevailingValue(valuesList);
                }
            }

    gis::updateMinMaxRasterGrid(&(this->modelCaseIndexMap));
    this->nrModelCases = int(this->modelCaseIndexMap.maximum);
    return true;
    */
}

int getCaseIndexFromId(int caseId, std::vector <Crit3DModelCase> modelCases)
{
    if (modelCases.size() == 0)
        return NODATA;

    int i;
    for (i=0; i < modelCases.size(); i++)
        if (caseId == modelCases[i].id)
            return i;

    //default value
    if (i == modelCases.size() - 1)
        return 0;

    return NODATA;
}

void modelCaseIndexMapIndexFromId(gis::Crit3DRasterGrid* myGrid, std::vector <Crit3DModelCase> modelCases)
{
    int fieldId, fieldIndex;

    // transform from id to index
    for (int myRow = 0; myRow < myGrid->header->nrRows; myRow++)
        for (int myCol = 0; myCol < myGrid->header->nrCols; myCol++)
        {
            fieldId = int(myGrid->value[myRow][myCol]);
            if (fieldId != int(myGrid->header->flag))
            {
                fieldIndex = getCaseIndexFromId(fieldId, modelCases);
                if (fieldIndex != NODATA)
                    myGrid->value[myRow][myCol] = fieldIndex;
            }
        }
}

bool Vine3DProject::loadFieldMap(QString myFileName)
{
    this->logInfo ("Read field map...");

    std::string fn = myFileName.left(myFileName.length()-4).toStdString();
    gis::Crit3DRasterGrid myGrid;

    std::string errorStr;
    if (! gis::readEsriGrid(fn, &(myGrid), errorStr))
    {
        this->errorString = "Load fields map failed:\n" + myFileName + "\n" + QString::fromStdString(errorStr);
        logError();
        return (false);
    }

    // compute prevailing map
    modelCaseIndexMap.initializeGrid(DEM);
    gis::prevailingMap(myGrid, &(modelCaseIndexMap));
    gis::updateMinMaxRasterGrid(&(modelCaseIndexMap));

    modelCaseIndexMapIndexFromId(&modelCaseIndexMap, modelCases);

    this->logInfo ("Field map = " + myFileName);
    return (true);
}


bool Vine3DProject::readFieldQuery(QSqlQuery myQuery, int* idField, Crit3DLanduse* landuse, int* vineIndex, int* trainingIndex,
                                   int* soilIndex, float* maxLaiGrass, float* maxIrrigationRate)
{
    int i, idCultivar, idTraining, idSoil;

    *idField = myQuery.value("id_field").toInt();

    //LANDUSE
    std::string landuse_name = myQuery.value("landuse").toString().toStdString();
    if (landuseNames.find(landuse_name) == landuseNames.end())
    {
        errorString = "Unknown landuse for field " + QString::number(*idField);
        return false;
    }
    else
        *landuse = landuseNames.at(landuse_name);

    //CULTIVAR
    idCultivar = myQuery.value("id_cultivar").toInt();
    i=0;
    while (i < cultivar.size() && idCultivar != cultivar[i].id) i++;
    if (i == cultivar.size())
    {
        errorString = "cultivar " + QString::number(idCultivar) + " not found" + myQuery.lastError().text();
        return false;
    }
    *vineIndex = i;

    //TRAINING SYSTEM
    idTraining = myQuery.value("id_training_system").toInt();
    i=0;
    while (i < trainingSystems.size() && idTraining != this->trainingSystems[i].id) i++;
    if (i == trainingSystems.size())
    {
        errorString = "training system nr." + QString::number(idTraining) + " not found" + myQuery.lastError().text();
        return false;
    }
    *trainingIndex = i;

    //SOIL
    idSoil = myQuery.value("id_soil").toInt();

    unsigned int index=0;
    while (index < this->nrSoils && idSoil != soilList[index].id)
        index++;

    if (index == this->nrSoils)
    {
        errorString = "soil " + QString::number(idSoil) + " not found" + myQuery.lastError().text();
        return false;
    }
    *soilIndex = signed(index);

    *maxLaiGrass = myQuery.value("max_lai_grass").toFloat();
    *maxIrrigationRate = myQuery.value("irrigation_max_rate").toFloat();

    return true;
}


bool Vine3DProject::loadFieldsProperties()
{
    logInfo ("Read fields properties...");

    int fieldIndex, idField, vineIndex, trainingIndex, soilIndex;
    float maxLaiGrass, maxIrrigationRate;
    Crit3DLanduse landuse;

    QSqlQuery myQuery(dbVine3D);

    // CHECK DEFAULT
    myQuery.prepare("SELECT id_field, landuse, id_cultivar, id_training_system, id_soil, max_lai_grass, irrigation_max_rate FROM fields WHERE id_field=0");
    if (! myQuery.exec())
    {
        errorString = "Error reading fields table" + myQuery.lastError().text();
        return false;
    }
    else if (!myQuery.next())
    {
        this->errorString = "Missing default field (index = 0) in fields table";
        return(false);
    }

    // READ PROPERTIES
    myQuery.prepare("SELECT id_field, landuse, id_cultivar, id_training_system, id_soil, max_lai_grass, irrigation_max_rate FROM fields ORDER BY id_field");
    if (! myQuery.exec())
    {
        errorString = "Error reading fields table" + myQuery.lastError().text();
        return false;
    }

    fieldIndex = 0;
    Crit3DModelCase myCase;

    while (myQuery.next())
    {
        if (readFieldQuery(myQuery, &idField, &landuse, &vineIndex, &trainingIndex, &soilIndex, &maxLaiGrass, &maxIrrigationRate))
        {
            myCase.id = idField;
            myCase.landuse = landuse;
            myCase.soilIndex = soilIndex;
            myCase.cultivar = &(this->cultivar[vineIndex]);
            myCase.maxLAIGrass = maxLaiGrass;
            myCase.maxIrrigationRate = maxIrrigationRate;

            float density = 1 / (trainingSystems[trainingIndex].rowDistance * trainingSystems[trainingIndex].plantDistance);

            myCase.trainingSystem = trainingIndex;
            myCase.plantDensity = density;
            myCase.shootsPerPlant = this->trainingSystems[trainingIndex].shootsPerPlant;

            modelCases.push_back(myCase);
        }
        else
        {
            errorString = "Error reading fields";
            return false;
        }
    }

    return(true);
}


float Vine3DProject::findSoilMaxDepth()
{
    double maxSoilDepth = 0;
    for (unsigned int i = 0; i < nrSoils; i++)
    {
        maxSoilDepth = MAXVALUE(maxSoilDepth, soilList[i].totalDepth);
    }
    return MINVALUE(computationSoilDepth, maxSoilDepth);
}

int Vine3DProject::getAggregatedVarCode(int rawVarCode)
{
    for (int i=0; i<nrAggrVar; i++)
        if (this->varCodes[i] == rawVarCode)
            return this->aggrVarCodes[i];

    return NODATA;
}

bool Vine3DProject::getMeteoVarIndexRaw(meteoVariable myVar, int* nrIndices, int** varIndices)
{
    int myAggrVarIndex = getMeteoVarIndex(myVar);

    if (myAggrVarIndex == NODATA) return false;

    *nrIndices = 0;
    int i;
    for (i=0; i<nrAggrVar; i++)
        if (myAggrVarIndex == this->aggrVarCodes[i])
            (*nrIndices)++;

    if (*nrIndices == 0) return false;

    *varIndices = (int *) calloc(*nrIndices, sizeof(int));

    int j=0;
    for (i=0; i<nrAggrVar; i++)
        if (myAggrVarIndex == this->aggrVarCodes[i])
        {
            (*varIndices)[j] = varCodes[i];
            j++;
        }

    return true;
}


bool Vine3DProject::loadDBPoints()
{
    closeMeteoPointsDB();

    logInfo ("Read points locations...");

    QString queryString = "SELECT id_point, name, utm_x, utm_y, altitude, is_utc, is_forecast FROM points_properties";
    queryString += " ORDER BY id_point";

    QSqlQuery query = dbVine3D.exec(queryString);
    if (query.size() == -1)
    {
        this->errorString = "Query failed in Table 'points_properties'\n" + query.lastError().text();
        return(false);
    }

    nrMeteoPoints = query.size();
    meteoPoints = new Crit3DMeteoPoint[nrMeteoPoints];

    //read values
    int i = 0;
    int id;
    while (query.next())
    {
        meteoPoints[i].active = true;

        id = query.value("id_point").toInt();
        meteoPoints[i].id = std::to_string(id);
        meteoPoints[i].name = query.value("name").toString().toStdString();
        meteoPoints[i].point.utm.x = query.value("utm_x").toFloat();
        meteoPoints[i].point.utm.y = query.value("utm_y").toFloat();
        meteoPoints[i].point.z = query.value("altitude").toFloat();
        meteoPoints[i].isUTC = query.value("is_utc").toBool();
        meteoPoints[i].isForecast = query.value("is_forecast").toBool();

        //temporary
        meteoPoints[i].lapseRateCode = primary;

        gis::getLatLonFromUtm(gisSettings, meteoPoints[i].point.utm.x, meteoPoints[i].point.utm.y,
                                    &(meteoPoints[i].latitude), &(meteoPoints[i].longitude));

        i++;
    }

    findVine3DLastMeteoDate();

    if (dbVine3D.isOpen())
    {
        for (int i = 0; i < this->nrMeteoPoints; i++)
        {
            if (! readPointProxyValues(&(this->meteoPoints[i]), &(this->dbVine3D)))
            {
                logError("Error reading proxy values");
                return false;
            }
        }
    }

    //position with respect to DEM
    if (DEM.isLoaded)
        checkMeteoPointsDEM();

    return(true);
}

void Vine3DProject::findVine3DLastMeteoDate()
{
    QSqlQuery qry(dbVine3D);
    QStringList tables;
    QDateTime lastDate(QDate(1800, 1, 1), QTime(0, 0, 0));

    tables << "obs_values_boundary";
    tables << "obs_values_h";

    QDateTime date;
    QString dateStr, statement;
    foreach (QString table, tables)
    {
        statement = QString( "SELECT MAX(date_) FROM \"%1\" AS dateTime").arg(table);
        if(qry.exec(statement))
        {
            if (qry.next())
            {
                dateStr = qry.value(0).toString();
                if (!dateStr.isEmpty())
                {
                    date = QDateTime::fromString(dateStr,"yyyy-MM-dd");
                    if (date > lastDate) lastDate = date;
                }
            }
        }
    }

    setCurrentDate(lastDate.date());
    setCurrentHour(12);
}

float Vine3DProject::meteoDataConsistency(meteoVariable myVar, const Crit3DTime& myTimeIni, const Crit3DTime& myTimeFin)
{
    float dataConsistency = 0.0;
    for (int i = 0; i < nrMeteoPoints; i++)
        dataConsistency = MAXVALUE(dataConsistency, meteoPoints[i].obsDataConsistencyH(myVar, myTimeIni, myTimeFin));

    return dataConsistency;
}

bool Vine3DProject::meteoDataLoaded(const Crit3DTime& myTimeIni, const Crit3DTime& myTimeFin)
{
    for (int i = 0; i < nrMeteoPoints; i++)
        if (meteoPoints[i].isDateIntervalLoadedH(myTimeIni, myTimeFin))
            return true;

    return false;
}

//observed data: 5 minutes
bool Vine3DProject::loadObsDataSubHourly(int indexPoint, meteoVariable myVar, QDateTime d1, QDateTime d2, QString tableName)
{
    QTime myTime;
    Crit3DDate myDate;
    int myHour, myMinutes;
    QString queryString;
    float myValue;

    if (nrMeteoPoints <= indexPoint)
    {
        logError("Function loadObsData: wrong point index");
        return(false);
    }
    Crit3DMeteoPoint* myPoint = &(meteoPoints[indexPoint]);

    int nrDays = d1.daysTo(d2) + 1;

    //initialize data
    myPoint->initializeObsDataH(meteoSettings->getHourlyIntervals(), nrDays, getCrit3DDate(d1.date()));

    queryString = "SELECT date_time, id_variable, obs_value FROM " + tableName;
    queryString += " WHERE id_location = " + QString::fromStdString(myPoint->id);
    queryString += " AND id_variable = " + QString::number(getMeteoVarIndex(myVar));
    queryString += " AND date_time >= '" + d1.toString("yyyy-MM-dd hh:mm:ss") + "'";
    queryString += " AND date_time <= '" + d2.toString("yyyy-MM-dd hh:mm:ss") + "'";
    queryString += " ORDER BY date_time";

    QSqlQuery myQuery = dbVine3D.exec(queryString);
    if (myQuery.size() == -1)
    {
        logError("Query failed in Table 'obs_values': " + myQuery.lastError().text());
        return(false);
    }
    else if (myQuery.size() == 0) return(false);

    //read values
    while (myQuery.next())
    {
        if (getValue(myQuery.value(2), &myValue))
        {
            myDate = getCrit3DDate(myQuery.value(0).toDate());
            myTime = myQuery.value(0).toDateTime().time();
            myHour = myTime.hour();
            myMinutes = myTime.minute();

            myPoint->setMeteoPointValueH(myDate, myHour, myMinutes, myVar, myValue);
        }
    }

    myQuery.clear();
    return(true);
}


// observed data: aggregation hourly
bool Vine3DProject::loadObsDataHourly(int indexPoint, QDate d1, QDate d2, QString tableName, bool useAggrCodes)
{
    QString queryString;
    Crit3DDate myDate;
    int myHour;
    meteoVariable myVar;
    float myValue, myFlag;
    bool isValid;
    bool dataAvailable = false;

    if (nrMeteoPoints <= indexPoint)
    {
        logError("Function loadObsDataHourly: wrong point index");
        return(false);
    }
    Crit3DMeteoPoint* myPoint = &(meteoPoints[indexPoint]);

    if (useAggrCodes)
        queryString = "SELECT date_, hour_, id_variable, obs_value FROM " + tableName;
    else
        queryString = "SELECT date_, hour_, id_variable, obs_value, data_valid FROM " + tableName;

    queryString += " WHERE id_location = " + QString::fromStdString(myPoint->id);
    queryString += " AND date_ >= '" + d1.toString("yyyy-MM-dd") + "'";
    queryString += " AND date_ <= '" + d2.toString("yyyy-MM-dd") + "'";
    queryString += " ORDER BY date_, hour_";

    QSqlQuery myQuery = dbVine3D.exec(queryString);

    if (myQuery.size() == -1)
    {
        logError("Query failed in Table " + tableName + "\n" + myQuery.lastError().text());
        return(false);
    }
    else if (myQuery.size() == 0) return(false);

    //read values
    while (myQuery.next())
    {
        myDate = getCrit3DDate(myQuery.value(0).toDate());
        myHour = myQuery.value(1).toInt();
        //transform local time in UTC
        if (!myPoint->isUTC)
        {
            myHour -= this->gisSettings.timeZone;
            if (myHour < 0)
            {
                myDate = myDate.addDays(-1);
                myHour += 24;
            }
        }

        if (useAggrCodes)
            isValid = true;
        else
        {
            isValid = false;
            if (getValue(myQuery.value(4), &myFlag))
                if (myFlag >= 0.5) isValid = true;
        }

        if (isValid)
        {
            if (useAggrCodes)
                myVar  = getMeteoVariable(myQuery.value(2).toInt());
            else
                myVar = getMeteoVariable(this->getAggregatedVarCode(myQuery.value(2).toInt()));

            if (getValue(myQuery.value(3), &myValue))
            {
                dataAvailable = true;
                myPoint->setMeteoPointValueH(myDate, myHour, 0, myVar, myValue);
            }
        }
    }

    myQuery.clear();
    return(dataAvailable);
}


bool Vine3DProject::loadObsDataHourlyVar(int indexPoint, meteoVariable myVar, QDate d1, QDate d2, QString tableName, bool useAggrCodes)
{
    QString queryString;
    Crit3DDate myDate;
    int myHour;
    float myValue, myFlag;
    bool isValid;
    int nrIndices;
    int* varIndices;
    bool dataAvailable=false;

    if (nrMeteoPoints <= indexPoint)
    {
        logError("Function loadObsDataBoundary: wrong point index");
        return(false);
    }
    Crit3DMeteoPoint* myPoint = &(meteoPoints[indexPoint]);

    if (useAggrCodes)
    {
        queryString = "SELECT date_, hour_, obs_value FROM " + tableName;
        queryString += " WHERE id_variable = " + QString::number(getMeteoVarIndex(myVar));
    }
    else
    {
        queryString = "SELECT date_, hour_, obs_value, data_valid FROM " + tableName;
        if (!this->getMeteoVarIndexRaw(myVar, &nrIndices, &varIndices)) return false;
        queryString += " WHERE id_variable IN (";
        for (int i=0; i<nrIndices-1; i++)
            queryString += QString::number(varIndices[i]) + ",";
        queryString += QString::number(varIndices[nrIndices-1]) + ")";
    }

    queryString += " AND id_location = " + QString::fromStdString(myPoint->id);
    queryString += " AND date_ >= '" + d1.toString("yyyy-MM-dd") + "'";
    queryString += " AND date_ <= '" + d2.toString("yyyy-MM-dd") + "'";
    queryString += " ORDER BY date_, hour_";

    QSqlQuery myQuery = dbVine3D.exec(queryString);
    if (myQuery.size() == -1)
    {
        logError("Query failed in table: "+ tableName + "\n" + myQuery.lastError().text());
        return(false);
    }
    else if (myQuery.size() == 0) return(false);

    //read values
    while (myQuery.next())
    {
        myDate = getCrit3DDate(myQuery.value(0).toDate());
        myHour = myQuery.value(1).toInt();
        //transform local time in UTC
        if (!myPoint->isUTC)
        {
            myHour -= this->gisSettings.timeZone;
            if (myHour < 0)
            {
                myDate = myDate.addDays(-1);
                myHour += 24;
            }
        }

        if (useAggrCodes)
            isValid = true;
        else
        {
            isValid = false;
            if (getValue(myQuery.value(3), &myFlag))
                if (myFlag >= 0.5) isValid = true;
        }

        if (isValid)
            if (getValue(myQuery.value(2), &myValue))
            {
                dataAvailable = true;
                myPoint->setMeteoPointValueH(myDate, myHour, 0, myVar, myValue);
            }
    }

    myQuery.clear();
    return(dataAvailable);
}


bool Vine3DProject::loadObsDataAllPoints(QDate d1, QDate d2, bool showInfo)
{
    isObsDataLoaded = false;

    logInfo("Load observed data:" + d1.toString() + " " + d2.toString());

    bool isObsDataBoundaryLoaded = false;
    bool isObsDataWMSLoaded = false;
    bool isForecast = false;
    long nrDays = long(d1.daysTo(d2)) + 1;
    int hourlyFraction = 1;

    int step = 1;
    QString infoStr;

    if (showInfo)
    {
        infoStr = "Loading data from " + d1.toString() + " to " + d2.toString();
        setProgressBar(infoStr, nrMeteoPoints);
    }

    for (int i = 0; i < nrMeteoPoints; i++)
    {
        if (showInfo)
            if ((i % step) == 0)
                updateProgressBar(i);

        meteoPoints[i].initializeObsDataH(hourlyFraction, nrDays, getCrit3DDate(d1));

        if (meteoPoints[i].isForecast)
        {
            if (loadObsDataHourly(i, d1, d2, "forecast", true))
                isForecast = true;
        }
        else
        {
            if (loadObsDataHourly(i, d1, d2, "obs_values_boundary", true))
                isObsDataBoundaryLoaded = true;

            if (loadObsDataHourly(i, d1, d2, "obs_values_h", false))
                isObsDataWMSLoaded = true;
        }

    }

    if (showInfo)
        closeProgressBar();

    isObsDataLoaded = (isObsDataBoundaryLoaded || isObsDataWMSLoaded || isForecast);

    if (! isObsDataLoaded) this->errorString = "Missing observed data.";

    return(isObsDataLoaded);
}


bool Vine3DProject::loadObsDataAllPointsVar(meteoVariable myVar, QDate d1, QDate d2)
{
    isObsDataLoaded = false;

    bool isObsDataBoundaryLoaded = false;
    bool isObsDataWMSLoaded = false;
    bool isForecastLoaded=false;
    int nrDays = d1.daysTo(d2) + 1;
    int hourlyFraction = 1;
    Crit3DDate dateIni = getCrit3DDate(d1);
    Crit3DDate dateFin = getCrit3DDate(d2);

    for (int i = 0; i < nrMeteoPoints; i++)
    {
        if (! meteoPoints[i].isDateIntervalLoadedH(dateIni,dateFin))
            meteoPoints[i].initializeObsDataH(hourlyFraction, nrDays, dateIni);
        else
            meteoPoints[i].emptyVarObsDataH(myVar, dateIni, dateFin);

        if (meteoPoints[i].isForecast)
        {
            if (loadObsDataHourlyVar(i, myVar, d1, d2, "forecast", true))
                isForecastLoaded = true;
        }
        else
        {
            if (loadObsDataHourlyVar(i, myVar, d1, d2, "obs_values_boundary", true))
                isObsDataBoundaryLoaded = true;

            if (loadObsDataHourlyVar(i, myVar, d1, d2, "obs_values_h", false))
                isObsDataWMSLoaded = true;
        }

    }

    isObsDataLoaded = (isObsDataBoundaryLoaded || isObsDataWMSLoaded || isForecastLoaded);

    if (! isObsDataLoaded) this->errorString = "missing data";
    return(isObsDataLoaded);
}


int Vine3DProject::getIndexPointFromId(QString myId)
{
    for (int i = 0; i < nrMeteoPoints; i++)
        if (QString::fromStdString(meteoPoints[i].id) == myId)
            return(i);
    return(NODATA);
}


float Vine3DProject::getTimeStep()
{
    return (3600 / meteoSettings->getHourlyIntervals());
}


bool Vine3DProject::loadObsDataFilled(QDateTime firstTime, QDateTime lastTime)
{
    QDate d1 = firstTime.date().addDays(-30);
    QDate d2 = lastTime.date().addDays(30);

    if (! this->loadObsDataAllPoints(d1, d2, false)) return(false);

    // Replace missing data
    long nrReplacedData = 0;
    Crit3DTime myTime = getCrit3DTime(firstTime);
    long nrHours = firstTime.secsTo(lastTime) / 3600;
    for (int i = 0; i <=nrHours; i++)
    {
        if (!checkLackOfData(this, airTemperature, myTime, &nrReplacedData)
            || !checkLackOfData(this, precipitation, myTime, &nrReplacedData)
            || !checkLackOfData(this, airRelHumidity, myTime, &nrReplacedData))
        {
            this->logError("Weather data missing: " + getQDateTime(myTime).toString("yyyyMMdd hh:mm"));
            return(false);
        }
        checkLackOfData(this, windScalarIntensity, myTime, &nrReplacedData);
        myTime = myTime.addSeconds(3600);
    }

    if(nrReplacedData > 0)
    {
        this->logInfo("\nWarning! "+ QString::number(nrReplacedData)+ " hourly data are missing.");
        this->logInfo("They was replaced by mean values.\n");
    }

    return true;
}


bool Vine3DProject::runModels(QDateTime firstTime, QDateTime lastTime, bool saveOutput)
{
    if (! isProjectLoaded)
    {
        logError("Load a project before.");
        return false;
    }

    //if (!loadObsDataFilled(dateTime1, dateTime2))
    logInfoGUI("Loading meteo data...");
    if (! loadMeteoPointsData(firstTime.date().addDays(-1), lastTime.date().addDays(+1), true, false, false))
    {
        this->logError();
        return false;
    }

    QDir myDir;
    QString myOutputPathDaily, myOutputPathHourly;
    bool isInitialState;
    QDate firstDate = firstTime.date();
    QDate lastDate = lastTime.date();
    QDate previousDate = firstDate.addDays(-1);
    int hourTime2 = lastTime.time().hour();
    int finalHour;

    this->logInfoGUI("Run models from: " + firstDate.toString() + " to: " + lastDate.toString());

    for (QDate myDate = firstDate; myDate <= lastDate; myDate = myDate.addDays(1))
    {
        //load state
        isInitialState = false;
        if (myDate == firstDate)
        {
            if (loadStates(previousDate))
                isInitialState = false;
            else
            {
                this->logInfo("State not found.");
                isInitialState = true;
            }
        }

        if (myDate == lastDate)
            finalHour = hourTime2;
        else
            finalHour = 24;

        if (finalHour > 0)
        {
            if (saveOutput)
            {
                //create output directories
                myOutputPathDaily = getProjectPath() + dailyOutputPath + myDate.toString("yyyy/MM/dd/");
                myOutputPathHourly = getProjectPath() + hourlyOutputPath + myDate.toString("yyyy/MM/dd/");

                if ((! myDir.mkpath(myOutputPathDaily)) || (! myDir.mkpath(myOutputPathHourly)))
                {
                    this->logError("Creation output directories failed." );
                    saveOutput = false;
                }
            }

            // load average air temperature map, if exists
            loadDailyMeteoMap(this, dailyAirTemperatureAvg, myDate.addDays(-1));

            if (! modelDailyCycle(isInitialState, getCrit3DDate(myDate), finalHour, this, myOutputPathHourly, saveOutput))
            {
                logError(errorString);
                return false;
            }
        }

        if ((finalHour == 24) || ((myDate == lastDate) && (finalHour == 23)))
        {
            if (saveOutput)
            {
                this->logInfo("Aggregate daily meteo data");
                aggregateAndSaveDailyMap(airTemperature, aggrMin, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(airTemperature, aggrMax, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(airTemperature, aggrAverage, getCrit3DDate(myDate), myOutputPathDaily,myOutputPathHourly);
                aggregateAndSaveDailyMap(precipitation, aggrSum, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(referenceEvapotranspiration, aggrSum, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(airRelHumidity, aggrMin, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(airRelHumidity, aggrMax, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(airRelHumidity, aggrAverage, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(windScalarIntensity, aggrAverage, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(globalIrradiance, aggrIntegral, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);
                aggregateAndSaveDailyMap(leafWetness, aggrSum, getCrit3DDate(myDate), myOutputPathDaily, myOutputPathHourly);

                if (removeDirectory(myOutputPathHourly)) this->logInfo("Delete hourly files");
            }

            //load daily map (for desease)
            if (! loadDailyMeteoMap(this, dailyAirTemperatureAvg, myDate)) return false;
            if (! loadDailyMeteoMap(this, dailyAirRelHumidityAvg, myDate)) return false;
            if (! loadDailyMeteoMap(this, dailyPrecipitation, myDate))  return false;
            if (! loadDailyMeteoMap(this, dailyLeafWetness, myDate)) return false;
            updateThermalSum(this, myDate);

            //powdery mildew
            if (computeDiseases) computePowderyMildew(this);

            //state and output
            if (! saveStateAndOutput(myDate)) return false;
        }
    }

    // Downy mildew (computation from 1 January)
    if (computeDiseases) computeDownyMildew(this, firstDate, lastDate, hourTime2);

    logInfoGUI("end of run");
    return true;
}


bool Vine3DProject::loadStates(QDate myDate)
{
    QString statePath = getProjectPath() + "states/" + myDate.toString("yyyy/MM/dd/");

    //if (!loadPlantState(this, tartaricAcidVar, myDate, myStatePath)) return(false);
    //if (!loadPlantState(this, pHBerryVar, myDate, myStatePath)) return(false);
    //if (!loadPlantState(this, fruitBiomassIndexVar, myDate, myStatePath)) return(false);
    if (!loadPlantState(this, daysAfterBloomVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, cumulatedBiomassVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, fruitBiomassVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, shootLeafNumberVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, meanTemperatureLastMonthVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, chillingUnitsVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, forceStateBudBurstVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, forceStateVegetativeSeasonVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, stageVar, myDate, statePath)) return(false);
    if (!loadPlantState(this, leafAreaIndexVar, myDate, statePath)) return(false);

    if (!loadPlantState(this, isHarvestedVar, myDate, statePath))
    {
        this->statePlantMaps->isHarvestedMap->setConstantValueWithBase(0, DEM);
    }
    if (!loadPlantState(this, fruitBiomassIndexVar, myDate, statePath))
    {
        //defualt= chardonnay
        this->statePlantMaps->fruitBiomassIndexMap->setConstantValueWithBase(this->modelCases[1].cultivar->parameterBindiMiglietta.fruitBiomassSlope, DEM);
    }

    //problema: mancano nei precedenti stati
    loadPlantState(this, cumRadFruitsetVerVar, myDate, statePath);
    loadPlantState(this, degreeDaysFromFirstMarchVar, myDate, statePath);
    loadPlantState(this, degreeDays10FromBudBurstVar, myDate, statePath);
    loadPlantState(this, degreeDaysAtFruitSetVar, myDate, statePath);
    loadPlantState(this, powderyAICVar, myDate, statePath);
    loadPlantState(this, powderyCurrentColoniesVar, myDate, statePath);
    loadPlantState(this, powderySporulatingColoniesVar, myDate, statePath);

    if (!loadWaterBalanceState(this, myDate, statePath, waterMatricPotential)) return false;

    this->logInfo("Load state: " + myDate.toString("yyyy-MM-dd"));
    return(true);
}


bool Vine3DProject::saveStateAndOutput(QDate myDate)
{
    QDir myDir;
    QString statePath = getProjectPath() + "states/" + myDate.toString("yyyy/MM/dd/");
    QString outputPath = getProjectPath() + this->dailyOutputPath + myDate.toString("yyyy/MM/dd/");
    if (! myDir.mkpath(statePath))
    {
        this->logError("Creation directory states failed." );
        return(false);
    }

    this->logInfo("Save state and output");

    if (!savePlantState(this, meanTemperatureLastMonthVar, myDate, statePath)) return(false);
    if (!savePlantState(this, chillingUnitsVar, myDate, statePath)) return(false);
    if (!savePlantState(this, forceStateBudBurstVar, myDate, statePath)) return(false);
    if (!savePlantState(this, forceStateVegetativeSeasonVar, myDate, statePath)) return(false);
    if (!savePlantState(this, cumRadFruitsetVerVar, myDate, statePath)) return(false);

    if (!savePlantState(this, stageVar, myDate, statePath)) return(false);
    if (!savePlantState(this, degreeDaysFromFirstMarchVar, myDate, statePath)) return(false);
    if (!savePlantState(this, degreeDays10FromBudBurstVar, myDate, statePath)) return(false);
    if (!savePlantState(this, degreeDaysAtFruitSetVar, myDate, statePath)) return(false);
    if (!savePlantState(this, daysAfterBloomVar, myDate, statePath)) return(false);
    if (!savePlantState(this, shootLeafNumberVar, myDate, statePath)) return(false);
    if (!savePlantState(this, isHarvestedVar, myDate, statePath)) return(false);

    if (!savePlantState(this, leafAreaIndexVar, myDate, statePath)) return(false);
    if (!savePlantState(this, cumulatedBiomassVar, myDate, statePath)) return(false);
    if (!savePlantState(this, fruitBiomassVar, myDate, statePath)) return(false);
    if (!savePlantState(this, fruitBiomassIndexVar,myDate,statePath)) return(false);

    if (computeDiseases)
    {
        if (!savePlantState(this, powderyAICVar, myDate, statePath)) return(false);
        if (!savePlantState(this, powderyCurrentColoniesVar, myDate, statePath)) return(false);
        if (!savePlantState(this, powderySporulatingColoniesVar, myDate, statePath)) return(false);
    }

    if (!saveWaterBalanceState(this, myDate, statePath, waterMatricPotential)) return (false);

    QString notes = "";
    if (!savePlantOutput(this, daysFromFloweringVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, shootLeafNumberVar, myDate, outputPath, notes, true, true)) return(false);
    if (!savePlantOutput(this, leafAreaIndexVar, myDate, outputPath, notes, true, true)) return(false);
    if (!savePlantOutput(this, stageVar, myDate, outputPath, notes, true, true)) return(false);
    if (!savePlantOutput(this, tartaricAcidVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, brixMaximumVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, brixBerryVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, deltaBrixVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, cumulatedBiomassVar, myDate, outputPath, notes, true, true)) return(false);
    if (!savePlantOutput(this, fruitBiomassVar, myDate, outputPath, notes, true, true)) return(false);
    if (!savePlantOutput(this, transpirationStressVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, transpirationVineyardVar, myDate, outputPath, notes, false, true)) return(false);
    if (!savePlantOutput(this, transpirationGrassVar, myDate, outputPath, notes, false, false)) return(false);
    if (!savePlantOutput(this, wineYieldVar, myDate, outputPath, notes, false, true)) return(false);

    if (computeDiseases)
    {
        if (!savePlantOutput(this, powderyAICVar, myDate, outputPath, notes, true, true)) return(false);
        if (!savePlantOutput(this, powderySporulatingColoniesVar, myDate, outputPath, notes, true, true)) return(false);
        if (!savePlantOutput(this, powderyCOLVar, myDate, outputPath, notes, false, true)) return(false);
        if (!savePlantOutput(this, powderyINFRVar, myDate, outputPath, notes, false, true)) return(false);
        if (!savePlantOutput(this, powderyPrimaryInfectionRiskVar, myDate, outputPath, notes, false, true)) return(false);
    }

    saveWaterBalanceOutput(this, myDate, waterMatricPotential, "matricPotential_m", "10cm", outputPath, 0.1, 0.1);
    saveWaterBalanceOutput(this, myDate, waterMatricPotential, "matricPotential_m", "30cm", outputPath, 0.3, 0.3);
    saveWaterBalanceOutput(this, myDate, waterMatricPotential, "matricPotential_m", "70cm", outputPath, 0.7, 0.7);
    saveWaterBalanceOutput(this, myDate, waterMatricPotential, "matricPotential_m", "130cm", outputPath, 1.3, 1.3);

    if (!saveWaterBalanceOutput(this, myDate, degreeOfSaturation, "degreeOfSaturation", "soilDepth", outputPath, 0.0, double(computationSoilDepth) - 0.01)) return false;
    if (!saveWaterBalanceOutput(this, myDate, availableWaterContent, "waterContent_mm", "rootZone", outputPath, 0.0, double(computationSoilDepth))) return false;
    if (!saveWaterBalanceCumulatedOutput(this, myDate, waterInflow, "waterInflow_l", "", outputPath)) return false;
    if (!saveWaterBalanceCumulatedOutput(this, myDate, bottomDrainage, "bottomDrainage_mm", "", outputPath)) return false;

    return(true);
}

int Vine3DProject::getModelCaseIndex(unsigned row, unsigned col)
{
    if (gis::isOutOfGridRowCol(int(row), int(col), modelCaseIndexMap)) return NODATA;

    int caseIndex = int(modelCaseIndexMap.value[row][col]);
    if (caseIndex == int(modelCaseIndexMap.header->flag))
    {
        //DEFAULT
        caseIndex = 0;
    }

    return caseIndex;
}

bool Vine3DProject::isVineyard(unsigned row, unsigned col)
{
    int caseIndex = getModelCaseIndex(row, col);
    return (modelCases[caseIndex].landuse == landuse_vineyard);
}

int Vine3DProject::getVine3DSoilIndex(long row, long col)
{
    int caseIndex = this->getModelCaseIndex(row, col);

    if (caseIndex != NODATA)
    {
        return this->modelCases[caseIndex].soilIndex;
    }
    else
    {
        return NODATA;
    }
}

bool Vine3DProject::setVine3DSoilIndexMap()
{
    // check
    if (!DEM.isLoaded || !modelCaseIndexMap.isLoaded || nrSoils == 0)
    {
        if (!DEM.isLoaded)
            logError("setVine3DSoilIndexMap: missing Digital Elevation Model.");
        else if (!modelCaseIndexMap.isLoaded)
            logError("setVine3DSoilIndexMap: missing field map.");
        else if (nrSoils == 0)
            logError("setVine3DSoilIndexMap: missing soil properties.");
        return false;
    }

    int soilIndex;
    soilIndexMap.initializeGrid(*(DEM.header));
    for (int row = 0; row < DEM.header->nrRows; row++)
    {
        for (int col = 0; col < DEM.header->nrCols; col++)
        {
            if (int(DEM.value[row][col]) != int(DEM.header->flag))
            {
                soilIndex = getVine3DSoilIndex(row, col);
                if (soilIndex != int(NODATA))
                {
                    soilIndexMap.value[row][col] = soilIndex;
                }
            }
        }
    }

    soilIndexMap.isLoaded = true;
    return true;
}


soil::Crit3DHorizon* Vine3DProject::getSoilHorizon(long row, long col, int layer)
{
    int soilIndex = getSoilIndex(row, col);
    if (soilIndex == NODATA) return nullptr;

    int horizonIndex = soil::getHorizonIndex(soilList[unsigned(soilIndex)], layer);
    if (horizonIndex == NODATA) return nullptr;

    soil::Crit3DHorizon* horizonPtr = &(soilList[unsigned(soilIndex)].horizon[unsigned(horizonIndex)]);
    return horizonPtr;
}


bool Vine3DProject::getFieldBookIndex(int firstIndex, QDate myDate, int fieldIndex, int* outputIndex)
{
    *outputIndex = NODATA;
    for (int i = firstIndex; i < fieldBook.size(); i++)
    {
        // order by date
        if (fieldBook[i].operationDate > myDate) return false;
        if (myDate == fieldBook[i].operationDate)
        {
            if (fieldIndex == fieldBook[i].idField)
            {
                *outputIndex = i;
                return true;
            }
        }
    }
    return false;
}


bool Vine3DProject::computeVine3DWaterSinkSource()
{
    long surfaceIndex, nodeIndex;
    double prec, waterSource;
    double transp, flow;
    int myResult;
    QString myError;

    //initialize
    totalPrecipitation = 0;
    totalEvaporation = 0;
    totalTranspiration = 0;

    for (unsigned long i = 0; i < nrNodes; i++)
        waterSinkSource.at(size_t(i)) = 0.0;

    double area = DEM.header->cellSize * DEM.header->cellSize;

    //precipitation - irrigation
    for (long row = 0; row < indexMap.at(0).header->nrRows; row++)
    {
        for (long col = 0; col < indexMap.at(0).header->nrCols; col++)
        {
            surfaceIndex = long(indexMap.at(0).value[row][col]);
            if (surfaceIndex != long(indexMap.at(0).header->flag))
            {
                waterSource = 0.0;
                prec = double(hourlyMeteoMaps->mapHourlyPrec->value[row][col]);
                if (int(prec) != int(hourlyMeteoMaps->mapHourlyPrec->header->flag)) waterSource += prec;

                double irr = double(vine3DMapsH->mapHourlyIrrigation->value[row][col]);
                if (int(irr) != int(vine3DMapsH->mapHourlyIrrigation->header->flag)) waterSource += irr;

                if (waterSource > 0.0)
                {
                    flow = area * (waterSource / 1000.0);                        // [m3/h]
                    totalPrecipitation += flow;
                    waterSinkSource[unsigned(surfaceIndex)] += flow / 3600.0;   // [m3/s]
                }
            }
        }
    }

    //Evaporation
    for (int row = 0; row < indexMap.at(0).header->nrRows; row++)
    {
        for (int col = 0; col < indexMap.at(0).header->nrCols; col++)
        {
            surfaceIndex = long(indexMap.at(0).value[row][col]);
            if (surfaceIndex != long(indexMap.at(0).header->flag))
            {
                // LAI
                int idField = getModelCaseIndex(row, col);
                float laiGrass = modelCases[idField].maxLAIGrass;
                float laiVine = statePlantMaps->leafAreaIndexMap->value[row][col];
                float laiTot = laiVine + laiGrass;

                double realEvap = computeEvaporation(row, col, double(laiTot));     // [mm]
                flow = area * (realEvap / 1000.0);                                  // [m3/h]
                totalEvaporation += flow;
            }
        }
    }

    //crop transpiration
    for (unsigned int layerIndex=1; layerIndex < nrLayers; layerIndex++)
    {
        for (long row = 0; row < indexMap.at(size_t(layerIndex)).header->nrRows; row++)
        {
            for (long col = 0; col < indexMap.at(size_t(layerIndex)).header->nrCols; col++)
            {
                nodeIndex = long(indexMap.at(size_t(layerIndex)).value[row][col]);
                if (nodeIndex != long(indexMap.at(size_t(layerIndex)).header->flag))
                {
                    transp = double(outputPlantMaps->transpirationLayerMaps[layerIndex]->value[row][col]);
                    if (int(transp) != int(outputPlantMaps->transpirationLayerMaps[layerIndex]->header->flag))
                    {
                        flow = area * (transp / 1000.0);                            //[m^3/h]
                        totalTranspiration += flow;
                        waterSinkSource.at(unsigned(nodeIndex)) -= flow / 3600.0;   //[m^3/s]
                    }
                }
            }
        }
    }

    for (unsigned long i = 0; i < nrNodes; i++)
    {
        myResult = soilFluxes3D::setWaterSinkSource(signed(i), waterSinkSource.at(i));
        if (isCrit3dError(myResult, myError))
        {
            logError("waterBalanceSinkSource:" + myError);
            return false;
        }
    }

    return true;
}


Vine3DHourlyMaps::Vine3DHourlyMaps(const gis::Crit3DRasterGrid& DEM)
{
    mapHourlyIrrigation = new gis::Crit3DRasterGrid;
    mapHourlyIrrigation->initializeGrid(DEM);
}


Vine3DHourlyMaps::~Vine3DHourlyMaps()
{
    mapHourlyIrrigation->clear();
}


