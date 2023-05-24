/*
    INDI Driver for Kepler sCMOS camera.
    Copyright (C) 2022 Jasem Mutlaq (mutlaqja@ikarustech.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.h"
#include "kepler.h"

#include <unistd.h>
#include <memory>
#include <map>
#include <locale>
#include <codecvt>
#include <indielapsedtimer.h>

#define FLI_MAX_SUPPORTED_CAMERAS 4
#define VERBOSE_EXPOSURE          3

template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

#ifdef LEGACY_MODE
/* Pointing parameter */
static INumber pointing_elements[N_TP] =
{
    {"RA2K",     "RA@J2000, hours",               "%11.9m"},
    {"Dec2K",    "Dec@J2000, degs",               "%11.8m"},
    {"RAEOD",    "RA@EOD, hours",                 "%11.9m"},
    {"DecEOD",   "Dec@EOD, degs",                 "%11.8m"},
    {"HA",       "Hour angle, hours +W",          "%11.6m"},
    {"Alt",      "Altitude, degs",                "%11.6m"},
    {"Az",       "Azimuth, degs EofN",            "%11.6m"},
    {"AM",       "Air mass",                      "%5.2f"},
    {"PA",       "Parallactic angle, degs +W",    "%10.6m"},
    {"XVel",     "X axis velocity, asec/sec",     "%10.3m"},
    {"YVel",     "Y axis velocity, asec/sec",     "%10.3m"},
    {"XFE",      "X axis following err, asec",    "%10.3m"},
    {"YFE",      "Y axis following err, asec",    "%10.3m"},
    {"Focus",    "Focus, um",    		  "%10.6m"},
    {"JD",       "JD of these values",            "%13.5m"}
};
INumberVectorProperty pointing = {"Telescope", "Pointing",
                                  "Telescope circumstances",
                                  "", IP_RO, 0, IPS_IDLE, pointing_elements, NARRAY(pointing_elements)
                                 };



/* SetCatalog parameter */
static IText setcatalog_elements[N_SC] =
{
    {"entry",     "Name,edb,tle"},
};
ITextVectorProperty setcatalog = {"Telescope", "SetCatalog",
                                  "Track catalog, edb or TLE target",
                                  "", IP_WO, 60, IPS_IDLE, setcatalog_elements, NARRAY(setcatalog_elements)
                                 };



/* SetAltAz parameter */
static INumber setaltaz_elements[N_SAA] =
{
    {"Alt",      "Altitude, degs",             "%11.6m"},
    {"Az",       "Azimuth, degs EofN",         "%11.6m"},
};
INumberVectorProperty setaltaz = {"Telescope", "SetAltAz",
                                  "Slew to a specific Alt Az",
                                  "", IP_WO, 60, IPS_IDLE, setaltaz_elements, NARRAY(setaltaz_elements)
                                 };



/* SetHADec parameter */
static INumber sethadec_elements[N_SHD] =
{
    {"HA",      "Hour angle, hours",         "%11.6m"},
    {"Dec",     "Declination, degs",         "%11.6m"},
};
INumberVectorProperty sethadec = {"Telescope", "SetHADec",
                                  "Slew to a specific HA Dec",
                                  "", IP_WO, 60, IPS_IDLE, sethadec_elements, NARRAY(sethadec_elements)
                                 };



/* SetRADec2K parameter */
static INumber setradec2k_elements[N_SRD2K] =
{
    {"RA",        "RA @ J2000, hours",            "%11.6m"},
    {"Dec",       "Dec @ J2000, degs",            "%11.6m"},
};
INumberVectorProperty setradec2k = {"Telescope", "SetRADec2K",
                                    "Track an RA Dec @ J2000",
                                    "", IP_WO, 60, IPS_IDLE, setradec2k_elements, NARRAY(setradec2k_elements)
                                   };


/* SetVelocity parameter */
static INumber setvelocity_elements[N_SV] =
{
    {"HA",        "HA velocity, degs/sec",            "%8.5f"},
    {"Dec",       "Dec velocity, degs/sec",           "%8.5f"},
};
INumberVectorProperty setvelocity = {"Telescope", "SetVelocity",
                                     "Set constant slewing speed",
                                     "", IP_WO, 0, IPS_IDLE, setvelocity_elements, NARRAY(setvelocity_elements)
                                    };

/* Now parameter */
static INumber now_elements[N_NOW] =
{
    {"AirTemp",      "Air temp, C",                 "%7.1f"},
    {"DewPoint",     "Dew point, C",                "%7.1f"},
    {"WindChill",    "NWS Wind Chill, C",           "%7.1f"},
    {"AirPressure",  "Air pressure, hPaB",          "%7.1f"},
    {"Humidity",     "Humidity, percent",           "%7.1f"},
    {"WindDir",      "Wind direction, degs E of N", "%7.1f"},
    {"WindSpeed",    "Wind speed, mps",             "%7.1f"},
    {"WindGust",     "Recent wind max, mps",        "%7.1f"},
    {"RainAccum",    "Rain YTD, mm",                "%7.1f"},
    {"RainDetected", "Rain in progress, 0 or 1",    "%7.1f"},
    {"EField",       "E FIeld, V/m",                "%7.1f"},
    {"EFieldJD",     "E FIeld time, JD",            "%13.5f"},
};
INumberVectorProperty envnow = {"Environment", "Now",
                                "Environmental conditions now",
                                "", IP_RO, 0, IPS_IDLE, now_elements, NARRAY(now_elements)
                               };

/* Now parameter */
static INumber ownow_elements[N_OWNOW] =
{
    {"Humidity1", "humidity 1, %",                   "%7.1f"},
    {"DewPoint1", "dew point 1, deg C",              "%7.1f"},
    {"Temp1",     "temperature 1, deg C",            "%7.1f"},
    {"Humidity2", "humidity 2, %",                   "%7.1f"},
    {"DewPoint2", "dew point 2, deg C",              "%7.1f"},
    {"Temp2",     "temperature 2, deg C",            "%7.1f"},
    {"Humidity3", "humidity 3, %",                   "%7.1f"},
    {"DewPoint3", "dew point 3, deg C",              "%7.1f"},
    {"Temp3",     "temperature 3, deg C",            "%7.1f"},
    {"Temp4",     "temperature 4, deg C",            "%7.1f"},
    {"Temp5",     "temperature 5, deg C",            "%7.1f"},
    {"RoofOpen",  "Roof: -1=midway 0=closed 1=open", "%3.0f", -1, 1, 0, -1},
    {"RamOpen",   "Ram:  -1=midway 0=closed 1=open", "%3.0f", -1, 1, 0, -1},
};
INumberVectorProperty ownow = {"1-Wire", "Now",
                               "1-Wire devices now",
                               "", IP_RO, 0, IPS_IDLE, ownow_elements, NARRAY(ownow_elements)
                              };


/* Blind parameter */
static ISwitch blind_elements[N_BLD] =
{
    {"Open",        "Open blind",    ISS_OFF}
};
ISwitchVectorProperty blind = {"1-Wire", "Blind",
                               "Blind control",
                               "", IP_WO, ISR_ATMOST1, 0., IPS_IDLE, blind_elements, NARRAY(blind_elements)
                              };

/* compare pointers to two doubles, qsort-style */
static int med_cmpf(const void *p1, const void *p2)
{
    double diff = *(double *)p1 - *(double *)p2;
    if (diff < 0)
    {
        return (-1);
    }
    if (diff > 0)
    {
        return (1);
    }
    return (0);
}

#endif

/********************************************************************************
*
********************************************************************************/
static class Loader
{
        INDI::Timer hotPlugTimer;
        FPRODEVICEINFO camerasDeviceInfo[FLI_MAX_SUPPORTED_CAMERAS];
        // Serial / Camera Object
        std::map<std::wstring, std::shared_ptr<Kepler>> cameras;
    public:
        Loader()
        {
            load(false);
        }

    public:
        size_t getCountOfConnectedCameras()
        {
            uint32_t detectedCamerasCount = FLI_MAX_SUPPORTED_CAMERAS;
            int32_t result = FPROCam_GetCameraList(camerasDeviceInfo, &detectedCamerasCount);
            return (result >= 0 ? detectedCamerasCount : 0);

        }

    public:
        void load(bool isHotPlug)
        {
            auto usedCameras = std::move(cameras);
            auto detectedCamerasCount = getCountOfConnectedCameras();

            UniqueName uniqueName(usedCameras);

            for(uint32_t i = 0; i < detectedCamerasCount; i++)
            {
                const auto serialID = camerasDeviceInfo[i].cSerialNo;

                // camera already created
                if (usedCameras.find(serialID) != usedCameras.end())
                {
                    std::swap(cameras[serialID], usedCameras[serialID]);
                    continue;
                }

#ifdef LEGACY_MODE
                Kepler *kepler = new Kepler(camerasDeviceInfo[i], L"CMOSCam");
#else
                Kepler *kepler = new Kepler(camerasDeviceInfo[i], uniqueName.make(camerasDeviceInfo[i]));
#endif
                cameras[serialID] = std::shared_ptr<Kepler>(kepler);
                if (isHotPlug)
                    kepler->ISGetProperties(nullptr);
            }
        }

    public:
        class UniqueName
        {
                std::map<std::wstring, bool> used;
            public:
                UniqueName() = default;
                explicit UniqueName(const std::map<std::wstring, std::shared_ptr<Kepler>> &usedCameras)
                {

                    for (const auto &camera : usedCameras)
                    {
                        auto name = std::string(camera.second->getDeviceName());
                        auto wname = std::wstring(name.begin(), name.end());
                        used[wname] = true;
                    }
                }

                std::wstring make(const FPRODEVICEINFO &cameraInfo)
                {
                    std::wstring cameraName = std::wstring(L"FLI ") + std::wstring(cameraInfo.cFriendlyName);
                    std::wstring uniqueName = cameraName;

                    for (int index = 0; used[uniqueName] == true; )
                        uniqueName = cameraName + std::wstring(L" ") + std::to_wstring(++index);

                    used[uniqueName] = true;
                    return uniqueName;
                }
        };
} loader;

// Map of device type --> Pixel sizes.
// Pixel sizes 99 means I couldn't find information on them.
std::map<FPRODEVICETYPE, double> Kepler::SensorPixelSize
{
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_GSENSE400, 11},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_GSENSE2020, 6.5},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_GSENSE4040, 9},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_GSENSE6060, 10},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_DC230_42, 15},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_DC230_84, 15},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_DC4320, 24},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_SONYIMX183, 2.4},
    {FPRODEVICETYPE::FPRO_CAM_DEVICE_TYPE_FTM, 99}
};

/********************************************************************************
*
********************************************************************************/
void Kepler::workerStreamVideo(const std::atomic_bool &isAboutToQuit)
{

}

/********************************************************************************
*
********************************************************************************/
void Kepler::workerExposure(const std::atomic_bool &isAboutToQuit, float duration)
{
    int32_t result = FPROCtrl_SetExposure(m_CameraHandle, duration * 1e9, 0, false);
    if (result != 0)
    {
        LOGF_ERROR("%s: Failed to start exposure: %d", __PRETTY_FUNCTION__, result);
        return;
    }

#ifdef LEGACY_MODE
    initMedianVels();
    maxxfe = maxyfe = 0;
#endif

    PrimaryCCD.setExposureDuration(duration);
    LOGF_DEBUG("StartExposure->setexp : %.3fs", duration);

    // Try exposure for 3 times
    for (int i = 0; i < 3; i++)
    {
        result = FPROFrame_CaptureStart(m_CameraHandle, 1);
        if (result == 0)
            break;

        // Wait 100ms before trying again
        usleep(100 * 1000);
    }

    if (result != 0)
    {
        LOGF_ERROR("Failed to start exposure: %d", result);
        return;
    }

    INDI::ElapsedTimer exposureTimer;

    if (duration > VERBOSE_EXPOSURE)
        LOGF_INFO("Taking a %.2f seconds frame...", duration);

    // Countdown if we have a multi-second exposure.
    // For expsures less than a second, we skip this entirely.
    double timeLeft = 0.0;
    do
    {
        timeLeft = std::max(duration - exposureTimer.elapsed() / 1000.0, 0.0);
        if (isAboutToQuit)
            return;

        auto delay = std::max(timeLeft - std::trunc(timeLeft), 0.005);
        timeLeft = std::round(timeLeft);
        PrimaryCCD.setExposureLeft(timeLeft);
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(delay * 1e6)));
    }
    while (timeLeft > 0);

    uint32_t grabSize = m_TotalFrameBufferSize;

    // This is blocking?
    std::unique_lock<std::mutex> guard(ccdBufferLock);
    prepareUnpacked();
    result = FPROFrame_GetVideoFrameUnpacked(m_CameraHandle,
             m_FrameBuffer,
             &grabSize,
             timeLeft * 1000,
             &fproUnpacked,
             RequestStatSP.findOnSwitchIndex() == INDI_ENABLED ? &fproStats : nullptr);

    if (result >= 0)
    {
        FPROFrame_CaptureAbort(m_CameraHandle);

        // Send the merged image.
        switch (MergePlanesSP.findOnSwitchIndex())
        {
            case to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH):
                PrimaryCCD.setFrameBuffer(reinterpret_cast<uint8_t*>(fproUnpacked.pMergedImage));
                PrimaryCCD.setFrameBufferSize(fproUnpacked.uiMergedBufferSize, false);
                break;
            case to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_HIGHONLY):
                PrimaryCCD.setFrameBuffer(reinterpret_cast<uint8_t*>(fproUnpacked.pHighImage));
                PrimaryCCD.setFrameBufferSize(fproUnpacked.uiHighBufferSize, false);
                break;
            case to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY):
                PrimaryCCD.setFrameBuffer(reinterpret_cast<uint8_t*>(fproUnpacked.pLowImage));
                PrimaryCCD.setFrameBufferSize(fproUnpacked.uiLowBufferSize, false);
                break;
        }

        PrimaryCCD.setExposureLeft(0.0);
        if (PrimaryCCD.getExposureDuration() > VERBOSE_EXPOSURE)
            LOG_INFO("Exposure done, downloading image...");

        ExposureComplete(&PrimaryCCD);
    }
    else
    {
        PrimaryCCD.setExposureFailed();
        LOGF_ERROR("Failed to grab frame: %d", result);
    }
}

Kepler::Kepler(const FPRODEVICEINFO &info, std::wstring name) : m_CameraInfo(info)
{
    setVersion(FLI_CCD_VERSION_MAJOR, FLI_CCD_VERSION_MINOR);

    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
    auto byteName = convert.to_bytes(name);
    setDeviceName(byteName.c_str());

    memset(&fproUnpacked, 0, sizeof(fproUnpacked));
    memset(&fproStats, 0, sizeof(fproStats));

    m_TemperatureTimer.callOnTimeout(std::bind(&Kepler::readTemperature, this));
    m_TemperatureTimer.setInterval(TEMPERATURE_FREQUENCY_IDLE);

    m_GPSTimer.callOnTimeout(std::bind(&Kepler::readGPS, this));
    m_GPSTimer.setInterval(GPS_TIMER_PERIOD);
}

Kepler::~Kepler()
{

}

const char *Kepler::getDefaultName()
{
    return "FLI Kepler";
}

bool Kepler::initProperties()
{
    // Initialize parent camera properties.
    INDI::CCD::initProperties();

    // Set Camera capabilities
    SetCCDCapability(CCD_CAN_ABORT | CCD_CAN_BIN | CCD_CAN_SUBFRAME | CCD_HAS_COOLER | CCD_HAS_SHUTTER);

    // Add capture format
    CaptureFormat mono = {"INDI_MONO", "Mono", 16, true};
    addCaptureFormat(mono);

    // Set exposure range
    // TODO double check this is the supported range.
    PrimaryCCD.setMinMaxStep("CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", 0.001, 3600, 1, false);

    /*****************************************************************************************************
    // Properties
    ******************************************************************************************************/

    // Communication Method
    CommunicationMethodSP[to_underlying(FPROCONNECTION::FPRO_CONNECTION_USB)].fill("FPRO_CONNECTION_USB", "USB", ISS_ON);
    CommunicationMethodSP[to_underlying(FPROCONNECTION::FPRO_CONNECTION_FIBRE)].fill("FPRO_CONNECTION_FIBRE", "Fiber", ISS_OFF);
    CommunicationMethodSP.fill(getDeviceName(), "COMMUNICATION_METHOD", "Connect Via", OPTIONS_TAB, IP_RO, ISR_1OFMANY, 60,
                               IPS_IDLE);

    // Merge Planes
    MergePlanesSP[to_underlying(
                      FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH)].fill("to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH)", "Both", ISS_ON);
    MergePlanesSP[to_underlying(
                      FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY)].fill("to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY)", "Low Only",
                              ISS_OFF);
    MergePlanesSP[to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_HIGHONLY)].fill("HWMERGE_FRAME_HIGHONLYE", "High Only",
            ISS_OFF);
    MergePlanesSP.fill(getDeviceName(), "MERGE_PLANES", "Merging", IMAGE_SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Calibration Frames (for MERGE_HARDWARE)
    MergeCalibrationFilesTP[CALIBRATION_DARK].fill("CALIBRATION_DARK", "Dark", "");
    MergeCalibrationFilesTP[CALIBRATION_FLAT].fill("CALIBRATION_FLAT", "Flat", "");
    MergeCalibrationFilesTP.fill(getDeviceName(), "MERGE_CALIBRATION_FRAMES", "Calibration", IMAGE_SETTINGS_TAB, IP_RW, 60,
                                 IPS_IDLE);

    // Cooler Duty Cycle
    CoolerDutyNP[0].fill("CCD_COOLER_VALUE", "Cooling Power (%)", "%+06.2f", 0., 100., 5, 0.0);
    CoolerDutyNP.fill(getDeviceName(), "CCD_COOLER_POWER", "Cooling Power", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    // Fan
    FanSP[INDI_ENABLED].fill("INDI_ENABLED", "Enabled", ISS_OFF);
    FanSP[INDI_DISABLED].fill("INDI_DISABLED", "Disabled", ISS_ON);
    FanSP.fill(getDeviceName(), "FAN_CONTROL", "Fan", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Black Level
    BlackLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR)].fill("LDR", "LDR", "%.f", 0, 1000, 10, 0);
    BlackLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR)].fill("HDR", "HDR", "%.f", 0, 1000, 10, 0);
    BlackLevelNP.fill(getDeviceName(), "BLACK_LEVEL", "Black Level", IMAGE_SETTINGS_TAB, IP_RW, 60, IPS_IDLE);

    // Black Sun Level
    BlackSunLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR)].fill("LDR", "LDR", "%.f", 0, 1000, 10, 0);
    BlackSunLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR)].fill("HDR", "HDR", "%.f", 0, 1000, 10, 0);
    BlackSunLevelNP.fill(getDeviceName(), "BLACK_SUN_LEVEL", "Black Sun Level", IMAGE_SETTINGS_TAB, IP_RW, 60, IPS_IDLE);

    // GPS
    GPSStateLP[to_underlying(FPROGPSSTATE::FPRO_GPS_NOT_DETECTED)].fill("FPRO_GPS_NOT_DETECTED", "Not detected", IPS_IDLE);
    GPSStateLP[to_underlying(FPROGPSSTATE::FPRO_GPS_DETECTED_NO_SAT_LOCK)].fill("FPRO_GPS_DETECTED_NO_SAT_LOCK", "No Sat lock",
            IPS_IDLE);
    GPSStateLP[to_underlying(FPROGPSSTATE::FPRO_GPS_DETECTED_AND_SAT_LOCK)].fill("FPRO_GPS_DETECTED_AND_SAT_LOCK", "Sat locked",
            IPS_IDLE);
    GPSStateLP[to_underlying(FPROGPSSTATE::FPRO_GPS_DETECTED_SAT_LOCK_TIME_ERROR)].fill("FPRO_GPS_DETECTED_SAT_LOCK_TIME_ERROR",
            "Lock error", IPS_IDLE);
    GPSStateLP.fill(getDeviceName(), "GPS_STATE", "GPS", GPS_TAB, IPS_IDLE);

    // Request Stats
    RequestStatSP[INDI_ENABLED].fill("INDI_ENABLED", "Enabled", ISS_ON);
    RequestStatSP[INDI_DISABLED].fill("INDI_DISABLED", "Disabled", ISS_OFF);
    RequestStatSP.fill(getDeviceName(), "REQUEST_STATS", "Statistics", IMAGE_SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    /*****************************************************************************************************
    // Legacy Properties
    ******************************************************************************************************/
#ifdef LEGACY_MODE
    ExpValuesNP[ExpTime].fill("ExpTime", "ExpTime", "%.f", 0, 3600, 1, 1);
    ExpValuesNP[ROIW].fill("ROIW", "ROIW", "%.f", 0, 4096, 1, 4096);
    ExpValuesNP[ROIH].fill("ROIH", "ROIH", "%.f", 0, 4096, 1, 4096);
    ExpValuesNP[OSW].fill("OSW", "OSW", "%.f", 0, 1, 1, 0);
    ExpValuesNP[OSH].fill("OSH", "OSH", "%.f", 0, 1, 1, 0);
    ExpValuesNP[BinW].fill("BinW", "BinW", "%.f", 1, 4, 1, 1);
    ExpValuesNP[BinH].fill("BinH", "BinH", "%.f", 1, 4, 1, 1);
    ExpValuesNP[ROIX].fill("ROIX", "ROIX", "%.f", 0, 100, 1, 0);
    ExpValuesNP[ROIY].fill("ROIY", "ROIY", "%.f", 0, 100, 1, 0);
    ExpValuesNP[Shutter].fill("Shutter", "Shutter", "%.f", 0, 1, 1, 1);
    ExpValuesNP[Type].fill("Type", "Type", "%.f", 0, 4, 1, 4);
    ExpValuesNP.fill(getDeviceName(), "ExpValues", "ExpValues", LEGACY_TAB, IP_RW, 60, IPS_IDLE);
    // Trigger
    ExposureTriggerSP[0].fill("Go", "Start Exposure", ISS_OFF);
    ExposureTriggerSP.fill(getDeviceName(), "ExpGo", "Control Exposure", LEGACY_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);
    // Set Point
    TemperatureSetNP[0].fill("Target", "Target", "%.f", -40, 20, 5, 0);
    TemperatureSetNP.fill(getDeviceName(), "SetTemp", "Set Temperature", LEGACY_TAB, IP_RW, 60, IPS_IDLE);
    // Temperature readout and cooler value
    TemperatureReadNP[0].fill("Temp", "Temp", "%.f", -40, 40, 10, 0);
    TemperatureReadNP[1].fill("Drive", "Cooler", "%.f", 0, 100, 10, 0);
    TemperatureReadNP.fill(getDeviceName(), "TempNow", "Cooler Temp.", LEGACY_TAB, IP_RO, 60, IPS_IDLE);

    // Snoop TFRM devices.
    IDSnoopDevice("Telescope", "Pointing");
    IDSnoopDevice("Telescope", "SetCatalog");
    IDSnoopDevice("Telescope", "SetVelocity");
    IDSnoopDevice("Environment", "Now");
    IDSnoopDevice("1-Wire", "Now");
    IDSnoopDevice("1-Wire", "Blind");

#endif
    addAuxControls();

    return true;
}

/********************************************************************************
*
********************************************************************************/
void Kepler::ISGetProperties(const char *dev)
{
    INDI::CCD::ISGetProperties(dev);
    defineProperty(CommunicationMethodSP);

#ifdef LEGACY_MODE
    defineProperty(ExpValuesNP);
    defineProperty(ExposureTriggerSP);
    defineProperty(TemperatureSetNP);
    defineProperty(TemperatureReadNP);
#endif

}

/********************************************************************************
*
********************************************************************************/
bool Kepler::updateProperties()
{
    INDI::CCD::updateProperties();

    if (isConnected())
    {
        setup();

        defineProperty(CoolerDutyNP);
        defineProperty(MergePlanesSP);
        defineProperty(MergeCalibrationFilesTP);
        defineProperty(LowGainSP);
        defineProperty(HighGainSP);
        defineProperty(FanSP);
        defineProperty(BlackLevelNP);
        defineProperty(BlackSunLevelNP);
        defineProperty(GPSStateLP);
        defineProperty(RequestStatSP);
    }
    else
    {
        deleteProperty(CoolerDutyNP);
        deleteProperty(MergePlanesSP);
        deleteProperty(MergeCalibrationFilesTP);
        deleteProperty(LowGainSP);
        deleteProperty(HighGainSP);
        deleteProperty(FanSP);
        deleteProperty(BlackLevelNP);
        deleteProperty(BlackSunLevelNP);
        deleteProperty(GPSStateLP);
        deleteProperty(RequestStatSP);
    }

    return true;
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        // Black Level
        if (BlackLevelNP.isNameMatch(name))
        {
            // N.B. for now apply to both channels. Perhaps add channel selection in the future?
            bool LDR = FPROSensor_SetBlackLevelAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR, values[0]) >= 0;
            bool HDR = FPROSensor_SetBlackLevelAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR, values[1]) >= 0;
            if (LDR && HDR)
            {
                BlackLevelNP.update(values, names, n);
                BlackLevelNP.setState(IPS_OK);
            }
            else
                BlackLevelNP.setState(IPS_ALERT);
            BlackLevelNP.apply();
            return true;
        }

        // Black Sun Level
        if (BlackSunLevelNP.isNameMatch(name))
        {
            // N.B. for now apply to both channels. Perhaps add channel selection in the future?
            bool LDR = FPROSensor_SetBlackSunAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR, values[0]) >= 0;
            bool HDR = FPROSensor_SetBlackSunAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR, values[1]) >= 0;
            if (LDR && HDR)
            {
                BlackSunLevelNP.update(values, names, n);
                BlackSunLevelNP.setState(IPS_OK);
            }
            else
                BlackSunLevelNP.setState(IPS_ALERT);
            BlackSunLevelNP.apply();
            return true;
        }

        // Legacy Exposure Values
#ifdef LEGACY_MODE
        if (ExpValuesNP.isNameMatch(name))
        {
            ExpValuesNP.update(values, names, n);
            m_ExposureRequest = ExpValuesNP[ExpTime].value;

            // ROI
            {
                double tvalues[4] = {ExpValuesNP[ROIX].value, ExpValuesNP[ROIY].value, ExpValuesNP[ROIW].value, ExpValuesNP[ROIH].value};
                const char *tnames[4] = {"X", "Y", "WIDTH", "HEIGHT"};
                ISNewNumber(getDeviceName(), "CCD_FRAME", tvalues, const_cast<char **>(tnames), 4);
            }

            // Binning
            {
                double tvalues[2] = {ExpValuesNP[BinW].value, ExpValuesNP[BinH].value};
                const char *tnames[2] = {"HOR_BIN", "VER_BIN"};
                ISNewNumber(getDeviceName(), "CCD_BINNING", tvalues, const_cast<char **>(tnames), 2);
            }

            // Frame Type
            {
                ISState tstates[4] = {ISS_OFF, ISS_OFF, ISS_OFF, ISS_OFF};
                const char *tnames[4] = {"FRAME_LIGHT", "FRAME_BIAS", "FRAME_DARK", "FRAME_FLAT"};

                int frameType = ExpValuesNP[Type].value;
                if (frameType == 0 || frameType == 4)
                    tstates[0] = ISS_ON;
                else
                    tstates[frameType] = ISS_ON;
                ISNewSwitch(getDeviceName(), "CCD_FRAME_TYPE", tstates, const_cast<char **>(tnames), 4);
            }

            ExpValuesNP.setState(IPS_OK);
            ExpValuesNP.apply();
            return true;
        }

        if (TemperatureSetNP.isNameMatch(name))
        {
            TemperatureSetNP.update(values, names, n);
            double tvalues[1] = {TemperatureSetNP[0].value};
            const char *tnames[1] = {TemperatureN[0].name};
            ISNewNumber(getDeviceName(), "CCD_TEMPERATURE", tvalues, const_cast<char **>(tnames), 1);
            TemperatureSetNP.setState(IPS_OK);
            TemperatureSetNP.apply();
            return true;
        }

#endif
    }

    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        // Merge Planes
        if (MergePlanesSP.isNameMatch(name))
        {
            MergePlanesSP.update(states, names, n);
            MergePlanesSP.setState(IPS_OK);

            int index = MergePlanesSP.findOnSwitchIndex();
            fproUnpacked.bLowImageRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY)
                                            || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
            fproUnpacked.bHighImageRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_HIGHONLY)
                                             || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
            fproUnpacked.bMergedImageRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
            fproUnpacked.bMetaDataRequest = true;
            fproStats.bLowRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY)
                                    || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
            fproStats.bHighRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_HIGHONLY)
                                     || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
            fproStats.bMergedRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);

            MergePlanesSP.apply();
            saveConfig(MergePlanesSP);
            return true;
        }

        // Low Gain
        if (LowGainSP.isNameMatch(name))
        {
            LowGainSP.update(states, names, n);
            int index = LowGainSP.findOnSwitchIndex();
            if (FPROSensor_SetGainIndex(m_CameraHandle, FPROGAINTABLE::FPRO_GAIN_TABLE_LOW_CHANNEL,
                                        m_LowGainTable[index].uiDeviceIndex) >= 0)
                LowGainSP.setState(IPS_OK);
            else
                LowGainSP.setState(IPS_ALERT);
            LowGainSP.apply();
            saveConfig(true, LowGainSP.getName());
            return true;
        }

        // High Gain
        if (HighGainSP.isNameMatch(name))
        {
            HighGainSP.update(states, names, n);
            int index = HighGainSP.findOnSwitchIndex();
            if (FPROSensor_SetGainIndex(m_CameraHandle, FPROGAINTABLE::FPRO_GAIN_TABLE_HIGH_CHANNEL,
                                        m_HighGainTable[index].uiDeviceIndex) >= 0)
                HighGainSP.setState(IPS_OK);
            else
                HighGainSP.setState(IPS_ALERT);
            HighGainSP.apply();
            saveConfig(true, HighGainSP.getName());
            return true;
        }

        // Fan
        if (FanSP.isNameMatch(name))
        {
            FanSP.update(states, names, n);
            FanSP.setState(FPROCtrl_SetFanEnable(m_CameraHandle, FanSP.findOnSwitchIndex() == INDI_ENABLED) >= 0 ? IPS_OK : IPS_ALERT);
            FanSP.apply();
            return true;
        }

        // Request Stats
        if (RequestStatSP.isNameMatch(name))
        {
            RequestStatSP.update(states, names, n);
            RequestStatSP.setState(IPS_OK);
            RequestStatSP.apply();
            if (RequestStatSP.findOnSwitchIndex() == INDI_ENABLED)
                LOG_INFO("Statistics are enabled. Merged images would take longer to download.");
            else
                LOG_INFO("Statistics are disabled. Merged images would be faster to download.");
            saveConfig(true, RequestStatSP.getName());
            return true;
        }

        // Legacy Trigger Exposure
#ifdef LEGACY_MODE
        if (ExposureTriggerSP.isNameMatch(name))
        {
            ExposureTriggerSP.update(states, names, n);
            if (ExposureTriggerSP[0].getState() == ISS_ON)
            {
                double values[1] = {m_ExposureRequest};
                const char *names[1] = {"CCD_EXPOSURE_VALUE"};
                ISNewNumber(getDeviceName(), "CCD_EXPOSURE", values, const_cast<char **>(names), 1);
                ExposureTriggerSP.setState(IPS_BUSY);
            }
            else
            {
                ISState states[1] = {ISS_ON};
                const char *names[1] = {"ABORT"};
                ISNewSwitch(getDeviceName(), "CCD_ABORT_EXPOSURE", states, const_cast<char **>(names), 1);
                ExposureTriggerSP.reset();
                ExposureTriggerSP.setState(IPS_IDLE);
            }

            ExposureTriggerSP.apply();
            return true;
        }
#endif

    }

    return INDI::CCD::ISNewSwitch(dev, name, states, names, n);
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        if (MergeCalibrationFilesTP.isNameMatch(name))
        {
            MergeCalibrationFilesTP.update(texts, names, n);
            MergeCalibrationFilesTP.setState(IPS_OK);
            MergeCalibrationFilesTP.apply();
            saveConfig(MergeCalibrationFilesTP);
            return true;
        }

    }

    return INDI::CCD::ISNewText(dev, name, texts, names, n);
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::ISSnoopDevice(XMLEle * root)
{
    IUSnoopNumber(root, &envnow);
    IUSnoopNumber(root, &ownow);
    IUSnoopSwitch(root, &blind);
    if (IUSnoopNumber(root, &pointing) == 0)
    {
        /* add X/YVEL to median arrays and look for max FE */
        addMedianVels();
        checkMaxFE();

        return true;
    }

    /* capture commanded velocity if being set and recognized */
    if (!IUSnoopNumber(root, &setvelocity))
    {
        if (setvelocity.s != IPS_IDLE)
        {
            havel  = setvelocity.np[HA_SV].value * 3600;	/* want as/s */
            decvel = setvelocity.np[DEC_SV].value * 3600;	/* want as/s */
            IDLog("Snoop: HAVEL = %g DECVEL = %g\n", havel, decvel);
        }
        else
        {
            havel =  0.0;
            decvel = 0.0;
        }

        return true;
    }

    return INDI::CCD::ISSnoopDevice(root);
}

/********************************************************************************
* check X/YFE_TP as possible new extremes.
********************************************************************************/
void Kepler::checkMaxFE()
{
    double newxfe = pointing.np[XFE_TP].value;
    double newyfe = pointing.np[YFE_TP].value;

    if (fabs(newxfe) > fabs(maxxfe))
    {
        maxxfe = newxfe;
    }

    if (fabs(newyfe) > fabs(maxyfe))
    {
        maxyfe = newyfe;
    }
}

/********************************************************************************
* init arrays used to accumulate median velocities
********************************************************************************/
void Kepler::initMedianVels()
{
    xvels = (double *) (realloc(xvels, 1 * sizeof(double)));
    nxvels = 0;
    yvels = (double *) (realloc(yvels, 1 * sizeof(double)));
    nyvels = 0;
}

/********************************************************************************
* add X/YVEL_TP to the median arrays
********************************************************************************/
void Kepler::addMedianVels()
{
    xvels = (double *) realloc(xvels, (nxvels + 1) * sizeof(double));
    xvels[nxvels++] = pointing.np[XVEL_TP].value;
    yvels = (double *) realloc(yvels, (nyvels + 1) * sizeof(double));
    yvels[nyvels++] = pointing.np[YVEL_TP].value;
}

/********************************************************************************
* report median velocities
********************************************************************************/
void Kepler::getMedianVels(double *mxvp, double *myvp)
{
    if (nxvels > 0)
    {
        qsort(xvels, nxvels, sizeof(double), med_cmpf);
        *mxvp = xvels[nxvels / 2];
    }
    else
    {
        *mxvp = 0;
    }

    if (nyvels > 0)
    {
        qsort(yvels, nyvels, sizeof(double), med_cmpf);
        *myvp = yvels[nyvels / 2];
    }
    else
    {
        *myvp = 0;
    }
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::Connect()
{
    int32_t result = FPROCam_Open(&m_CameraInfo, &m_CameraHandle);
    if ((result >= 0) && (m_CameraHandle >= 0))
    {
        // Different camera models support a different set of capabilities.
        // The API allows you to retrieve the capabilities so that you can obtain
        // images properly and configure your applications accordingly.  In all cases,
        // you need to know the size of the Meta Data supplied by the camera that is
        // prepended to every image.  This size is contained in the capabilities structure.
        auto capNumber = static_cast<uint32_t>(FPROCAPS::FPROCAP_NUM);
        result =  FPROSensor_GetCapabilityList(m_CameraHandle, m_CameraCapabilitiesList, &capNumber);

        auto isFiber = m_CameraInfo.conInfo.eConnType == FPROCONNECTION::FPRO_CONNECTION_FIBRE;
        CommunicationMethodSP[to_underlying(FPROCONNECTION::FPRO_CONNECTION_USB)].setState(isFiber ? ISS_OFF : ISS_ON);
        CommunicationMethodSP[to_underlying(FPROCONNECTION::FPRO_CONNECTION_FIBRE)].setState(isFiber ? ISS_ON : ISS_OFF);
        CommunicationMethodSP.setState(IPS_OK);
        CommunicationMethodSP.apply();

        // Enable hardware level merging over PCIe.
        mergeEnables.bMergeEnable = true;
        mergeEnables.eMergeFrames = FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH;
        // N.B. Need to check later which format is more suitable
        //mergeEnables.eMergeFormat = FPRO_IMAGE_FORMAT::IFORMAT_RCD;
        mergeEnables.eMergeFormat = FPRO_IMAGE_FORMAT::IFORMAT_FITS;
        FPROAlgo_SetHardwareMergeEnables(m_CameraHandle, mergeEnables);

        LOGF_INFO("Established connection to camera via %s", isFiber ? "Fiber" : "USB");

        return (result == 0);
    }

    LOGF_ERROR("Failed to established connection with the camera: %d", result);
    return false;
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::Disconnect()
{
    free(m_FrameBuffer);
    m_FrameBuffer = nullptr;
    FPROCam_Close(m_CameraHandle);
    m_TemperatureTimer.stop();
    delete [] m_LowGainTable;
    delete [] m_HighGainTable;
    return true;
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::setup()
{
    // We need image data
    FPROFrame_SetImageDataEnable(m_CameraHandle, true);

    // Get # of supported formats first
    FPROFrame_GetSupportedPixelFormats(m_CameraHandle, nullptr, &m_FormatsCount);

    // Clear buffer
    delete [] m_FormatList;
    m_FormatList = new FPRO_PIXEL_FORMAT[m_FormatsCount];

    // Now get all the supported formats.
    FPROFrame_GetSupportedPixelFormats(m_CameraHandle, m_FormatList, &m_FormatsCount);

    // TODO need to add this to capture format
    //addCaptureFormat(...)

    // Get pixel format
    uint32_t pixelDepth = 16, pixelLSB = 1;
    FPRO_PIXEL_FORMAT pixelFormat;
    int32_t result = FPROFrame_GetPixelFormat(m_CameraHandle, &pixelFormat, &pixelLSB);
    if (result != 0)
    {
        LOGF_ERROR("%s: Failed to query camera pixel format: %d", __PRETTY_FUNCTION__, result);
        return false;
    }

    pixelDepth = (pixelFormat > FPRO_PIXEL_FORMAT::PFORMAT_BAYER8_BGGR) ? 16 : 8;

    auto pixelSize = SensorPixelSize[static_cast<FPRODEVICETYPE>(m_CameraCapabilitiesList[to_underlying(
                                         FPROCAPS::FPROCAP_DEVICE_TYPE)])];

    if (pixelSize > 90)
        LOG_WARN("Pixel size is unkown for this camera model! Contact INDI to supply correct pixel information.");

    const auto maxWidth = m_CameraCapabilitiesList[to_underlying(FPROCAPS::FPROCAP_MAX_PIXEL_WIDTH)];
    const auto maxHeight = m_CameraCapabilitiesList[to_underlying(FPROCAPS::FPROCAP_MAX_PIXEL_HEIGHT)];
    SetCCDParams(maxWidth,
                 maxHeight,
                 pixelDepth,
                 pixelSize,
                 pixelSize);

    FPROFrame_SetImageArea(m_CameraHandle, 0, 0, maxWidth, maxHeight);

    // Get required frame buffer size including all the metadata and extra bits added by the SDK.
    // We need to only
    m_TotalFrameBufferSize = FPROFrame_ComputeFrameSize(m_CameraHandle);

    m_FrameBuffer = static_cast<uint8_t*>(malloc(m_TotalFrameBufferSize));
    // This would allocate memory
    //PrimaryCCD.setFrameBufferSize(m_TotalFrameBufferSize);
    //    // This is actual image data size
    //    uint32_t rawFrameSize = PrimaryCCD.getXRes() * PrimaryCCD.getYRes() * PrimaryCCD.getBPP() / 8;
    //    // We set it again, but without allocating memory.
    //    PrimaryCCD.setFrameBufferSize(rawFrameSize, false);


    fproUnpacked.bLowImageRequest = true;
    fproUnpacked.bHighImageRequest = true;
    fproUnpacked.bMergedImageRequest = true;
    fproUnpacked.bMetaDataRequest = true;
    fproStats.bLowRequest = true;
    fproStats.bHighRequest = true;
    fproStats.bMergedRequest = true;
    fproUnpacked.eMergeFormat = FPRO_IMAGE_FORMAT::IFORMAT_FITS;

    // Low Gain tables
    if (m_CameraCapabilitiesList[to_underlying(FPROCAPS::FPROCAP_LOW_GAIN_TABLE_SIZE)] > 0)
    {
        uint32_t count = m_CameraCapabilitiesList[to_underlying(FPROCAPS::FPROCAP_LOW_GAIN_TABLE_SIZE)];
        delete [] m_LowGainTable;
        m_LowGainTable = new FPROGAINVALUE[count];
        if (FPROSensor_GetGainTable(m_CameraHandle, FPROGAINTABLE::FPRO_GAIN_TABLE_LOW_CHANNEL, m_LowGainTable, &count) >= 0)
        {
            LowGainSP.resize(count);
            char name[MAXINDINAME] = {0}, label[MAXINDILABEL] = {0};
            for (uint32_t i = 0; i < count; i++)
            {
                auto gain = static_cast<double>(m_LowGainTable[i].uiValue) / FPRO_GAIN_SCALE_FACTOR;
                snprintf(name, MAXINDINAME, "LOW_GAIN_%u", i);
                snprintf(name, MAXINDILABEL, "%.2f", gain);
                LowGainSP[i].fill(name, label, ISS_OFF);
            }
        }

        uint32_t index = 0;
        FPROSensor_GetGainIndex(m_CameraHandle, FPROGAINTABLE::FPRO_GAIN_TABLE_LOW_CHANNEL, &index);
        LowGainSP[index].setState(ISS_ON);
        LowGainSP.fill(getDeviceName(), "LOW_GAIN", "Low Gain", IMAGE_SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    }

    // High gain tables
    if (m_CameraCapabilitiesList[to_underlying(FPROCAPS::FPROCAP_HIGH_GAIN_TABLE_SIZE)] > 0)
    {
        uint32_t count = m_CameraCapabilitiesList[to_underlying(FPROCAPS::FPROCAP_HIGH_GAIN_TABLE_SIZE)];
        m_HighGainTable = new FPROGAINVALUE[count];
        if (FPROSensor_GetGainTable(m_CameraHandle, FPROGAINTABLE::FPRO_GAIN_TABLE_HIGH_CHANNEL, m_HighGainTable, &count) >= 0)
        {
            HighGainSP.resize(count);
            char name[MAXINDINAME] = {0}, label[MAXINDILABEL] = {0};
            for (uint32_t i = 0; i < count; i++)
            {
                auto gain = static_cast<double>(m_HighGainTable[i].uiValue) / FPRO_GAIN_SCALE_FACTOR;
                snprintf(name, MAXINDINAME, "HIGH_GAIN_%u", i);
                snprintf(name, MAXINDILABEL, "%.2f", gain);
                HighGainSP[i].fill(name, label, ISS_OFF);
            }
        }

        uint32_t index = 0;
        FPROSensor_GetGainIndex(m_CameraHandle, FPROGAINTABLE::FPRO_GAIN_TABLE_HIGH_CHANNEL, &index);
        HighGainSP[index].setState(ISS_ON);
        HighGainSP.fill(getDeviceName(), "HIGH_GAIN", "High Gain", IMAGE_SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    }

    // Fan
    bool fanOn = false;
    if (FPROCtrl_GetFanEnable(m_CameraHandle, &fanOn) >= 0)
    {
        FanSP[INDI_ENABLED].setState(fanOn ? ISS_ON : ISS_OFF);
        FanSP[INDI_DISABLED].setState(fanOn ? ISS_OFF : ISS_ON);
        FanSP.setState(IPS_OK);
    }

    // Black level
    uint32_t blackLevel = 0;
    if (FPROSensor_GetBlackLevelAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR, &blackLevel))
    {
        BlackLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR)].setValue(blackLevel);
        BlackLevelNP.setState(IPS_OK);
    }
    if (FPROSensor_GetBlackLevelAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR, &blackLevel))
    {
        BlackLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR)].setValue(blackLevel);
        BlackLevelNP.setState(IPS_OK);
    }

    // Black Sun level
    if (FPROSensor_GetBlackSunAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR, &blackLevel))
    {
        BlackSunLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_LDR)].setValue(blackLevel);
        BlackSunLevelNP.setState(IPS_OK);
    }
    if (FPROSensor_GetBlackSunAdjust(m_CameraHandle, FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR, &blackLevel))
    {
        BlackSunLevelNP[to_underlying(FPROBLACKADJUSTCHAN::FPRO_BLACK_ADJUST_CHAN_HDR)].setValue(blackLevel);
        BlackSunLevelNP.setState(IPS_OK);
    }

#ifdef LEGACY_MODE
    //    EncodeFormatSP.reset();
    //    EncodeFormatSP[FORMAT_NATIVE].setState(ISS_ON);
    //    EncodeFormatSP.apply();
    //    PrimaryCCD.setImageExtension("fit");
#endif

    m_TemperatureTimer.start();
    m_GPSTimer.start();
    return true;
}

/********************************************************************************
*
********************************************************************************/
void Kepler::prepareUnpacked()
{
    memset(&fproUnpacked, 0, sizeof(fproUnpacked));

    // Merging Planes
    int index = MergePlanesSP.findOnSwitchIndex();
    fproUnpacked.bLowImageRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY)
                                    || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
    fproUnpacked.bHighImageRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_HIGHONLY)
                                     || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
    fproUnpacked.bMergedImageRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
    fproUnpacked.bMetaDataRequest = true;

    // Statistics
    fproStats.bLowRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_LOWONLY)
                            || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
    fproStats.bHighRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_HIGHONLY)
                             || index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);
    fproStats.bMergedRequest = index == to_underlying(FPRO_HWMERGEFRAMES::HWMERGE_FRAME_BOTH);

    // Merging Method
    fproUnpacked.eMergeFormat = FPRO_IMAGE_FORMAT::IFORMAT_FITS;

}
/********************************************************************************
*
********************************************************************************/
int Kepler::SetTemperature(double temperature)
{
    // Return OK for
    if (std::abs(temperature - TemperatureN[0].value) < TEMPERATURE_THRESHOLD)
        return 1;
    int result = FPROCtrl_SetTemperatureSetPoint(m_CameraHandle, temperature);
    if (result >= 0)
    {
        m_TargetTemperature = temperature;
        m_TemperatureTimer.start(TEMPERATURE_FREQUENCY_BUSY);

#ifdef LEGACY_MODE
        TemperatureReadNP.setState(IPS_BUSY);
        TemperatureReadNP.apply();
#endif

        return 0;
    }

    return -1;
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::StartExposure(float duration)
{
    m_Worker.start(std::bind(&Kepler::workerExposure, this, std::placeholders::_1, duration));
    return true;
}

bool Kepler::AbortExposure()
{
    LOG_DEBUG("Aborting exposure...");
    m_Worker.quit();
    return (FPROFrame_CaptureStop(m_CameraHandle) == 0);
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::UpdateCCDFrameType(INDI::CCDChip::CCD_FRAME fType)
{
    int result = 0;
    switch (fType)
    {
        case INDI::CCDChip::LIGHT_FRAME:
            result = FPROFrame_SetFrameType(m_CameraHandle, FPRO_FRAME_TYPE::FPRO_FRAMETYPE_NORMAL);
            break;
        case INDI::CCDChip::BIAS_FRAME:
            result = FPROFrame_SetFrameType(m_CameraHandle, FPRO_FRAME_TYPE::FPRO_FRAMETYPE_BIAS);
            break;
        case INDI::CCDChip::DARK_FRAME:
            result = FPROFrame_SetFrameType(m_CameraHandle, FPRO_FRAME_TYPE::FPRO_FRAMETYPE_DARK);
            break;
        case INDI::CCDChip::FLAT_FRAME:
            result = FPROFrame_SetFrameType(m_CameraHandle, FPRO_FRAME_TYPE::FPRO_FRAMETYPE_LIGHTFLASH);
            break;
    }

    return (result >= 0);
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::UpdateCCDFrame(int x, int y, int w, int h)
{
    int result = FPROFrame_SetImageArea(m_CameraHandle, x, y, w, h);
    if (result >= 0)
    {
        // Set UNBINNED coords
        PrimaryCCD.setFrame(x, y, w, h);

        // Get required frame buffer size including all the metadata and extra bits added by the SDK.
        // We need to only
        m_TotalFrameBufferSize = FPROFrame_ComputeFrameSize(m_CameraHandle);
        m_FrameBuffer = static_cast<uint8_t*>(realloc(m_FrameBuffer, m_TotalFrameBufferSize));
        return true;
    }
    else
    {
        LOGF_ERROR("Failed to update frame ROI: %d", result);
        return false;
    }
}

/********************************************************************************
*
********************************************************************************/
bool Kepler::UpdateCCDBin(int binx, int biny)
{
    int result = FPROSensor_SetBinning(m_CameraHandle, binx, biny);
    if (result >= 0)
    {
        PrimaryCCD.setBin(binx, biny);
        return UpdateCCDFrame(PrimaryCCD.getSubX(), PrimaryCCD.getSubY(), PrimaryCCD.getSubW(), PrimaryCCD.getSubH());
    }
    else
    {
        LOGF_ERROR("Error updating bin: %d", result);
        return false;
    }
}

/********************************************************************************
*
********************************************************************************/
void Kepler::readTemperature()
{
    double ambient = 0, base = 0, cooler = 0;
    int result = FPROCtrl_GetTemperatures(m_CameraHandle, &ambient, &base, &cooler);
    if (result < 0)
    {
        TemperatureNP.s = IPS_ALERT;
        IDSetNumber(&TemperatureNP, nullptr);

#ifdef LEGACY_MODE
        TemperatureReadNP.setState(IPS_ALERT);
        TemperatureReadNP.apply();
#endif
        LOGF_WARN("FPROCtrl_GetTemperatures failed: %d", result);
    }

    switch (TemperatureNP.s)
    {
        case IPS_IDLE:
        case IPS_OK:
            if (std::abs(cooler - TemperatureN[0].value) > TEMPERATURE_THRESHOLD)
            {
                TemperatureN[0].value = cooler;
                IDSetNumber(&TemperatureNP, nullptr);

#ifdef LEGACY_MODE
                TemperatureReadNP.setState(IPS_OK);
                TemperatureReadNP[0].value = cooler;
                TemperatureReadNP.apply();
#endif
            }
            break;

        case IPS_BUSY:
            if (std::abs(cooler - m_TargetTemperature) <= TEMPERATURE_THRESHOLD)
            {
                TemperatureNP.s = IPS_OK;
#ifdef LEGACY_MODE
                TemperatureReadNP.setState(IPS_OK);
#endif
                // Reset now to idle frequency checks.
                m_TemperatureTimer.setInterval(TEMPERATURE_FREQUENCY_IDLE);
            }
            TemperatureN[0].value = cooler;
            IDSetNumber(&TemperatureNP, nullptr);
#ifdef LEGACY_MODE
            TemperatureReadNP[0].value = cooler;
            TemperatureReadNP.apply();
#endif
            break;

        case IPS_ALERT:
            break;
    }

    uint32_t dutycycle = 0;
    result = FPROCtrl_GetCoolerDutyCycle(m_CameraHandle, &dutycycle);
    // Set alert, if not set already in case there is SDK error.
    if (result < 0 && CoolerDutyNP.getState() != IPS_ALERT)
    {
        CoolerDutyNP.setState(IPS_ALERT);
        CoolerDutyNP.apply();

#ifdef LEGACY_MODE
        TemperatureReadNP.setState(IPS_ALERT);
        TemperatureReadNP.apply();
#endif
    }
    // Only send updates if we are above 1 percent threshold
    else if (std::abs(dutycycle - CoolerDutyNP[0].getValue()) >= 1)
    {
        CoolerDutyNP[0].setValue(dutycycle);
        CoolerDutyNP.setState(dutycycle > 0 ? IPS_BUSY : IPS_IDLE);
        CoolerDutyNP.apply();

#ifdef LEGACY_MODE
        TemperatureReadNP[1].value = dutycycle;
        TemperatureReadNP.apply();
#endif
    }
}

/********************************************************************************
*
********************************************************************************/
void Kepler::readGPS()
{
    FPROGPSSTATE state;
    uint32_t trackingoptions;
    int result = 0;
    if ( (result = FPROCtrl_GetGPSState(m_CameraHandle, &state, &trackingoptions)) >= 0)
    {
        // TODO check tracking options and report it.
        if (state != m_LastGPSState)
        {
            m_LastGPSState = state;
            for (auto &lp : GPSStateLP)
                lp.setState(IPS_IDLE);
            GPSStateLP[to_underlying(state)].setState(IPS_OK);
            GPSStateLP.setState(IPS_OK);
            GPSStateLP.apply();
        }
    }
    else
    {
        GPSStateLP.setState(IPS_ALERT);
        GPSStateLP.apply();
        LOGF_WARN("FPROCtrl_GetGPSState failed: %d", result);
    }
}
/********************************************************************************
*
********************************************************************************/
bool Kepler::saveConfigItems(FILE * fp)
{
    INDI::CCD::saveConfigItems(fp);

    MergePlanesSP.save(fp);
    MergeCalibrationFilesTP.save(fp);
    RequestStatSP.save(fp);
    if (LowGainSP.size() > 0)
        LowGainSP.save(fp);
    if (HighGainSP.size() > 0)
        HighGainSP.save(fp);

    return true;
}

/********************************************************************************
*
********************************************************************************/
void Kepler::debugTriggered(bool enable)
{
    FPRODebug_EnableLevel(true, enable ? FPRO_DEBUG_DEBUG : FPRO_DEBUG_NONE);
}

/********************************************************************************
*
********************************************************************************/
void Kepler::addFITSKeywords(INDI::CCDChip *targetChip, std::vector<INDI::FITSRecord> &fitsKeywords)
{
    INDI::CCD::addFITSKeywords(targetChip, fitsKeywords);

    if (RequestStatSP.findOnSwitchIndex() == INDI_ENABLED)
    {
        if (fproStats.bLowRequest)
        {
            fitsKeywords.push_back({"LOW_MEAN", fproStats.statsLowImage.dblMean, 3, "Low Mean"});
            fitsKeywords.push_back({"LOW_MEDIAN", fproStats.statsLowImage.dblMedian, 3, "Low Median"});
            fitsKeywords.push_back({"LOW_STDDEV", fproStats.statsLowImage.dblStandardDeviation, 3, "Low Standard Deviation"});
        }
        if (fproStats.bHighRequest)
        {
            fitsKeywords.push_back({"HIGH_MEAN", fproStats.statsHighImage.dblMean, 3, "High Mean"});
            fitsKeywords.push_back({"HIGH_MEDIAN", fproStats.statsHighImage.dblMedian, 3, "High Median"});
            fitsKeywords.push_back({"HIGH_STDDEV", fproStats.statsHighImage.dblStandardDeviation, 3, "High Standard Deviation"});
        }
        if (fproStats.bMergedRequest)
        {
            fitsKeywords.push_back({"MERGED_MEAN", fproStats.statsMergedImage.dblMean, 3, "Merged Mean"});
            fitsKeywords.push_back({"MERGED_MEDIAN", fproStats.statsMergedImage.dblMedian, 3, "Merged Median"});
            fitsKeywords.push_back({"MERGED_STDDEV", fproStats.statsMergedImage.dblStandardDeviation, 3, "Merged Standard Deviation"});
        }
    }

    FPROFrame_MetaValueInitBin(fproUnpacked.pMergedMetaData, fproUnpacked.uiMetaDataSize);

    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_CAMERA_MODEL, "MODEL", "Model");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_BACK_SIDE_ILLUMINATED, "BKSDILL", "Back Side Illuminated", 0);
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_BLACK_LEVEL_ADJUST, "BLKLVLAD", "Black Level Adjust value", 0);
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_BLACK_SUN_ADJUST, "BLKLVLSN", "Black Level Sun value", 0);
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_DEAD_PIXEL_CORRECTION, "DEADPXCR",
                          "Dead Pixel Correction flag", 0);
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_FRAME_NUMBER, "FRAMENUM", "Frame Number", 0);
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_GAIN_LOW, "GAINLOW", "Low Gain Value");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_GAIN_GLOBAL, "GAINGLOBAL", "Global Gain Value");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_GAIN_HIGH, "GAINHIGH", "High Gain Value");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_GEO_LAT_RAW, "GEO_LAT", "Geocentric latitude [deg]");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_GEO_LONG_RAW, "GEO_LONG", "Geocentric longitude [deg]");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_SERIAL_NUMBER, "SERIAL", "Serial Number");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_GPS_LOCK, "GPS_LOCK", "GPS Present at Capture Time", 0);
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_HDR_MODE, "HDR-MODE", "Image captured in HDR Mode");
    addMetadataFITSHeader(fitsKeywords, FPRO_META_KEYS::META_KEY_HORIZONTAL_SCAN_DIRECTION_INVERT, "HRZDIRIN",
                          "Horizontal Scan Direction Invert flag", 0);

    // TODO
    /*
    META_KEY_HIGH_ADUE,
    META_KEY_HORIZONTAL_PIXELS,
    META_KEY_HORIZONTAL_PIXEL_SIZE,
    META_KEY_ILLUMINATION_START_DELAY,
    META_KEY_ILLUMINATION_STOP_DELAY,
    META_KEY_IMAGE_HEIGHT,
    META_KEY_IMAGE_START_COLUMN,
    META_KEY_IMAGE_START_ROW,
    META_KEY_IMAGE_STOP_COLUMN,
    META_KEY_IMAGE_STOP_ROW,
    META_KEY_IMAGE_START_EXPOSURE_ROW,
    META_KEY_IS_HIGH_FRAME,
    META_KEY_IS_HIGH_GAIN_ONLY_FRAME,
    META_KEY_IS_MERGED_FRAME,
    META_KEY_IS_SOFTWARE_BINNING,
    META_KEY_IS_STACKED_FRAME,
    META_KEY_LOW_DARK_CURRENT,
    META_KEY_LOW_NOISE,
    META_KEY_MERGE_GAIN_RATIO,
    META_KEY_MERGE_LINE_OFFSET,
    META_KEY_NON_ROW_ALLIGNED_IMAGE,
    META_KEY_NUM_OF_DATA_CHANNELS,
    META_KEY_PIXEL_ORDERED_IMAGE,
    META_KEY_POST_REFERENCE_ROW,
    META_KEY_PRE_REFERENCE_ROW,
    META_KEY_POST_REFERENCE_PIXELS_PER_ROW,
    META_KEY_PRE_REFERENCE_PIXELS_PER_ROW,
    META_KEY_SENSOR_PIXEL_BIT_DEPTH,
    META_KEY_SENSOR_READ_QUADRANTS,
    META_KEY_SENSOR_CHIP_TEMPERATURE,
    META_KEY_SHUTTER_CLOSE_DELAY,
    META_KEY_SHUTTER_OPEN_DELAY,
    META_KEY_TEMPERATURE_SETPOINT,
    META_KEY_TRACKING_FRAMES_PER_IMAGE,
    META_KEY_TRACKING_START_COLUMN,
    META_KEY_TRACKING_START_ROW,
    META_KEY_TRACKING_STOP_COLUMN,
    META_KEY_TRACKING_STOP_ROW,
    META_KEY_USE_SHIFTED_AVERAGING,
    META_KEY_VERSION_API,
    META_KEY_VERSION_APPLICATION,
    META_KEY_VERSION_FIRMWARE,
    META_KEY_VERTICAL_PIXELS,
    META_KEY_VERTICAL_PIXEL_SIZE,
    META_KEY_VERTICAL_SCAN_DIRECTION_INVERT,
    */


#ifdef LEGACY_MODE
    double mxv, myv;
    getMedianVels(&mxv, &myv);
    if (mxv || myv)
    {
        fitsKeywords.push_back({"HA_RATE", mxv, 3, "Median HA rate, arcsec/s"});
        fitsKeywords.push_back({"DEC_RATE", myv, 3, "Median Dec rate, arcsec/s"});
        fitsKeywords.push_back({"MAXHTERR", maxxfe, 3, "Max HA tracking error, arcsecs"});
        fitsKeywords.push_back({"MAXDTERR", maxyfe, 3, "Max Dec tracking error, arcsecs"});
    }

    if (pointing.np[RA2K_TP].value > 0)
    {
        // Telescope
        char buf[64] = {0};
        fs_sexa(buf, pointing.np[RA2K_TP].value, 4, 36000);
        fitsKeywords.push_back({"RA2K", buf, "RA J2K H:M:S"});
        fs_sexa(buf, pointing.np[RAEOD_TP].value, 4, 36000);
        fitsKeywords.push_back({"RA", buf, "RA EOD H:M:S"});
        fs_sexa(buf, pointing.np[DEC2K_TP].value, 4, 36000);
        fitsKeywords.push_back({"DEC2K", buf, "Dec J2K D:M:S"});
        fs_sexa(buf, pointing.np[DECEOD_TP].value, 4, 36000);
        fitsKeywords.push_back({"DEC", buf, "Dec EOD D:M:S"});
        fs_sexa(buf, pointing.np[HA_TP].value, 4, 36000);
        fitsKeywords.push_back({"HA", buf, "Hour angle H:M:S"});
        fs_sexa(buf, pointing.np[AZ_TP].value, 4, 36000);
        fitsKeywords.push_back({"AZ", buf, "Azimuth D:M:S"});
        fs_sexa(buf, pointing.np[ALT_TP].value, 4, 36000);
        fitsKeywords.push_back({"ALT", buf, "Altitude D:M:S"});
    }

    if (envnow.np[HUMIDITY_NOW].value > 0)
    {
        // Environment
        fitsKeywords.push_back({"HUMIDITY", envnow.np[HUMIDITY_NOW].value, 3, "Exterior humidity, percent"});
        fitsKeywords.push_back({"AIRTEMP", envnow.np[TEMP_NOW].value, 3, "Exterior temp, deg C"});
        fitsKeywords.push_back({"WINDSPD", envnow.np[WINDSPD_NOW].value, 3, "Wind speed, mps"});
        fitsKeywords.push_back({"WINDDIR", envnow.np[WINDDIR_NOW].value, 3, "Wind dir, degs E of N"});
    }

    // Building
    if (ownow.np[H2_OWNOW].value > 0)
    {
        fitsKeywords.push_back({"BLDGT1", ownow.np[T1_OWNOW].value, 3, "Focus motor temp, C"});
        fitsKeywords.push_back({"BLDGH1", ownow.np[H1_OWNOW].value, 3, "Camera humidity, %"});
        fitsKeywords.push_back({"BLDGT2", ownow.np[T2_OWNOW].value, 3, "Lens temp, C"});
        fitsKeywords.push_back({"BLDGH2", ownow.np[H2_OWNOW].value, 3, "Objective lens humidity, %"});
        fitsKeywords.push_back({"BLDGT4", ownow.np[T4_OWNOW].value, 3, "Temperature at spider vane, C"});
        fitsKeywords.push_back({"LBLIND", blind.sp[0].s == ISS_ON ? "Open" : "Closed", "Lens blind state"});
    }

#endif
}

/********************************************************************************
*
********************************************************************************/
void Kepler::addMetadataFITSHeader(std::vector<INDI::FITSRecord> &fitsKeywords,
                                   FPRO_META_KEYS id,
                                   const std::string &keyword,
                                   const std::string &comment,
                                   int precision)
{
    FPROMETAVALUE value;
    FPROFrame_MetaValueGet(id, &value);
    if (value.iByteLength > 0)
        fitsKeywords.push_back({keyword.c_str(), reinterpret_cast<char *>(value.cStringValue), comment.c_str()});
    else
        fitsKeywords.push_back({keyword.c_str(), value.dblValue, precision, comment.c_str()});
}

/********************************************************************************
*
********************************************************************************/
void Kepler::UploadComplete(INDI::CCDChip *targetChip)
{
    INDI_UNUSED(targetChip);
#ifdef LEGACY_MODE
    ExposureTriggerSP[0].setState(ISS_OFF);
    ExposureTriggerSP.setState(IPS_OK);
    ExposureTriggerSP.apply();
#endif

    if (RequestStatSP.findOnSwitchIndex() == INDI_ENABLED)
        FPROFrame_FreeUnpackedBuffers(&fproUnpacked);
    FPROFrame_FreeUnpackedStatistics(&fproStats);
}

/********************************************************************************
*
********************************************************************************/
void Kepler::activeDevicesUpdated()
{
#ifdef LEGACY_MODE
    IDSnoopDevice("Telescope", "Pointing");
    IDSnoopDevice("Telescope", "SetCatalog");
    IDSnoopDevice("Telescope", "SetVelocity");
    IDSnoopDevice("Environment", "Now");
    IDSnoopDevice("1-Wire", "Now");
    IDSnoopDevice("1-Wire", "Blind");
#endif
}
