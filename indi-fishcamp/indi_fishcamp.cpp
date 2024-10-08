/*
 Fishcamp INDI CCD Driver
 Copyright (C) 2013 Jasem Mutlaq (mutlaqja@ikarustech.com)

 Multiple device support Copyright (C) 2013 Peter Polakovic (peter.polakovic@cloudmakers.eu)

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

#include <memory>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <deque>

#include <indidevapi.h>
#include <eventloop.h>

#include "indi_fishcamp.h"
#include "config.h"

#define MAX_CCD_TEMP   45   /* Max CCD temperature */
#define MIN_CCD_TEMP   -55  /* Min CCD temperature */
#define MAX_X_BIN      16   /* Max Horizontal binning */
#define MAX_Y_BIN      16   /* Max Vertical binning */
#define MAX_PIXELS     4096 /* Max number of pixels in one dimension */
#define TEMP_THRESHOLD .25  /* Differential temperature threshold (C)*/

static class Loader
{
        std::deque<std::unique_ptr<FishCampCCD>> cameras;
    public:
        Loader()
        {
            // initialize the driver framework
            IDLog("About to call fcUsb_init()\n");
            fcUsb_init();

            IDLog("About to call set logging\n");
            fcUsb_setLogging(false);

            IDLog("About to call find Cameras\n");
            int cameraCount = fcUsb_FindCameras();

            if(cameraCount == -1)
            {
                IDLog("Calling FindCameras again because at least 1 RAW camera was found\n");
                cameraCount = fcUsb_FindCameras();
            }

            IDLog("Found %d fishcamp cameras.\n", cameraCount);

            for (int i = 0; i < cameraCount; i++)
                cameras.push_back(std::unique_ptr<FishCampCCD>(new FishCampCCD(i + 1)));
        }
} loader;

FishCampCCD::FishCampCCD(int CamNum)
{
    cameraNum = CamNum;

    int rc = fcUsb_OpenCamera(cameraNum);

    IDLog("fcUsb_OpenCamera opening cam #%d, returns %d\n", cameraNum, rc);

    rc = fcUsb_cmd_getinfo(cameraNum, &camInfo);

    IDLog("fcUsb_cmd_getinfo opening cam #%d, returns %d\n", cameraNum, rc);

    strncpy(name, (char *)&camInfo.camNameStr, MAXINDINAME);

    IDLog("Cam #%d with name %s\n", cameraNum, name);

    setDeviceName(name);

    setVersion(FISHCAMP_VERSION_MAJOR, FISHCAMP_VERSION_MINOR);

    sim = false;
}

FishCampCCD::~FishCampCCD()
{
    fcUsb_CloseCamera(cameraNum);
}

const char *FishCampCCD::getDefaultName()
{
    return "Starfish CCD";
}

bool FishCampCCD::initProperties()
{
    // Init parent properties first
    INDI::CCD::initProperties();

    CaptureFormat mono = {"INDI_MONO", "Mono", 16, true};
    addCaptureFormat(mono);

    IUFillNumber(&GainN[0], "Gain", "", "%g", 1, 15, 1., 4.);
    IUFillNumberVector(&GainNP, GainN, 1, getDeviceName(), "CCD_GAIN", "Gain", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    IUFillNumber(&CoolerN[0], "Power %", "", "%g", 1, 100, 0, 0.0);
    IUFillNumberVector(&CoolerNP, CoolerN, 1, getDeviceName(), "Cooler", "", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    char *strBuf = new char[MAXINDINAME];

    IUFillText(&CamInfoT[0], "Name", "", name);
    IUFillText(&CamInfoT[1], "Serial #", "", (char *)&camInfo.camSerialStr);

    snprintf(strBuf, MAXINDINAME, "%d", camInfo.boardVersion);
    IUFillText(&CamInfoT[2], "Board version", "", strBuf);

    snprintf(strBuf, MAXINDINAME, "%d", camInfo.boardRevision);
    IUFillText(&CamInfoT[3], "Board revision", "", strBuf);

    snprintf(strBuf, MAXINDINAME, "%d", camInfo.fpgaVersion);
    IUFillText(&CamInfoT[4], "FPGA version", "", strBuf);

    snprintf(strBuf, MAXINDINAME, "%d", camInfo.fpgaRevision);
    IUFillText(&CamInfoT[5], "FPGA revision", "", strBuf);

    IUFillTextVector(&CamInfoTP, CamInfoT, 6, getDeviceName(), "Camera Info", "", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    SetCCDParams(camInfo.width, camInfo.height, 16, camInfo.pixelWidth / 10.0, camInfo.pixelHeight / 10.0);

    int nbuf;
    nbuf = PrimaryCCD.getXRes() * PrimaryCCD.getYRes() * PrimaryCCD.getBPP() / 8; //  this is pixel cameraCount
    nbuf += 512;                                                                  //  leave a little extra at the end
    PrimaryCCD.setFrameBufferSize(nbuf);

    SetCCDCapability(CCD_CAN_ABORT | CCD_CAN_SUBFRAME | CCD_HAS_COOLER | CCD_HAS_ST4_PORT);

    delete[] strBuf;

    return true;
}

void FishCampCCD::ISGetProperties(const char *dev)
{
    INDI::CCD::ISGetProperties(dev);

    // Add Debug, Simulator, and Configuration controls
    addAuxControls();
}

bool FishCampCCD::updateProperties()
{
    INDI::CCD::updateProperties();

    if (isConnected())
    {
        defineProperty(&CamInfoTP);
        defineProperty(&CoolerNP);
        defineProperty(&GainNP);

        timerID = SetTimer(getCurrentPollingPeriod());
    }
    else
    {
        deleteProperty(CamInfoTP.name);
        deleteProperty(CoolerNP.name);
        deleteProperty(GainNP.name);

        rmTimer(timerID);
    }

    return true;
}

bool FishCampCCD::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (!strcmp(name, GainNP.name))
        {
            IUUpdateNumber(&GainNP, values, names, n);
            setGain(GainN[0].value);
            GainNP.s = IPS_OK;
            IDSetNumber(&GainNP, nullptr);
            return true;
        }
    }

    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

bool FishCampCCD::setGain(double gain)
{
    int rc = fcUsb_cmd_setCameraGain(cameraNum, ((int)gain));

    LOGF_DEBUG("fcUsb_cmd_setCameraGain returns %d", rc);

    return true;
}

int FishCampCCD::SetTemperature(double temperature)
{
    TemperatureRequest = temperature;

    int rc = fcUsb_cmd_setTemperature(cameraNum, TemperatureRequest);

    LOGF_DEBUG("fcUsb_cmd_setTemperature returns %d", rc);

    if (fcUsb_cmd_getTECInPowerOK(cameraNum))
        CoolerNP.s = IPS_OK;
    else
        CoolerNP.s = IPS_IDLE;

    TemperatureNP.setState(IPS_BUSY);
    TemperatureNP.apply();

    LOGF_INFO("Setting CCD temperature to %+06.2f C", temperature);

    return 0;
}

void FishCampCCD::simulationTriggered(bool enable)
{
    sim = enable;
    fcUsb_setSimulation(enable);
}

bool FishCampCCD::Connect()
{
    sim = isSimulation();

    if (sim)
    {
        LOG_INFO("Simulated Fishcamp is online.");
        return true;
    }

    if (fcUsb_haveCamera())
    {
        fcUsb_cmd_setReadMode(cameraNum, fc_classicDataXfr, fc_16b_data);
        fcUsb_cmd_setCameraGain(cameraNum, GainN[0].value);
        fcUsb_cmd_setRoi(cameraNum, 0, 0, camInfo.width - 1, camInfo.height - 1);
        if (fcUsb_cmd_getTECInPowerOK(cameraNum))
            CoolerNP.s = IPS_OK;
        LOG_INFO("Fishcamp CCD is online.");
        return true;
    }
    else
    {
        LOG_ERROR("Cannot find Fishcamp CCD. Please check the logfile and try again.");
        return false;
    }
}

bool FishCampCCD::Disconnect()
{
    LOG_INFO("Fishcamp CCD is offline.");

    if (sim)
        return true;

    fcUsb_CloseCamera(cameraNum);

    return true;
}

bool FishCampCCD::StartExposure(float duration)
{
    PrimaryCCD.setExposureDuration(duration);
    ExposureRequest = duration;

    bool rc = false;

    LOGF_DEBUG("Exposure Time (s) is: %g", duration);

    // setup the exposure time in ms.
    rc = fcUsb_cmd_setIntegrationTime(cameraNum, (UInt32)(duration * 1000.0));

    LOGF_DEBUG("fcUsb_cmd_setIntegrationTime returns %d", rc);

    rc = fcUsb_cmd_startExposure(cameraNum);

    LOGF_DEBUG("fcUsb_cmd_startExposure returns %d", rc);

    gettimeofday(&ExpStart, nullptr);

    LOGF_INFO("Taking a %g seconds frame...", ExposureRequest);

    InExposure = true;

    return (rc == 0);
}

bool FishCampCCD::AbortExposure()
{
    int rc = 0;

    rc = fcUsb_cmd_abortExposure(cameraNum);

    LOGF_DEBUG("fcUsb_cmd_abortExposure returns %d", rc);

    InExposure = false;
    return true;
}

bool FishCampCCD::UpdateCCDFrameType(INDI::CCDChip::CCD_FRAME fType)
{
    // We only support light frames
    if (fType != INDI::CCDChip::LIGHT_FRAME)
    {
        LOG_ERROR("Only light frames are supported in this camera.");
        return false;
    }

    PrimaryCCD.setFrameType(fType);
    return true;
}

bool FishCampCCD::UpdateCCDFrame(int x, int y, int w, int h)
{
    int rc = 0;
    /* Add the X and Y offsets */
    long x_1 = x;
    long y_1 = y;

    long bin_width  = x_1 + (w / PrimaryCCD.getBinX());
    long bin_height = y_1 + (h / PrimaryCCD.getBinY());

    if (bin_width > PrimaryCCD.getXRes() / PrimaryCCD.getBinX())
    {
        IDMessage(getDeviceName(), "Error: invalid width requested %d", w);
        return false;
    }
    else if (bin_height > PrimaryCCD.getYRes() / PrimaryCCD.getBinY())
    {
        IDMessage(getDeviceName(), "Error: invalid height request %d", h);
        return false;
    }

    LOGF_DEBUG("The Final image area is (%ld, %ld), (%ld, %ld)\n", x_1, y_1, bin_width,
               bin_height);

    rc = fcUsb_cmd_setRoi(cameraNum, x_1, y_1,  x_1 + w - 1, y_1 + h - 1);

    LOGF_DEBUG("fcUsb_cmd_setRoi returns %d", rc);

    // Set UNBINNED coords
    PrimaryCCD.setFrame(x_1, y_1, w, h);

    int nbuf;
    nbuf = (bin_width * bin_height * PrimaryCCD.getBPP() / 8); //  this is pixel count
    nbuf += 512;                                               //  leave a little extra at the end
    PrimaryCCD.setFrameBufferSize(nbuf);

    LOGF_DEBUG("Setting frame buffer size to %d bytes.\n", nbuf);

    return true;
}

bool FishCampCCD::UpdateCCDBin(int binx, int biny)
{
    if (binx != 1 || biny != 1)
    {
        LOG_ERROR("Camera currently does not support binning.");
        return false;
    }

    return true;
}

float FishCampCCD::CalcTimeLeft()
{
    double timesince;
    double timeleft;
    struct timeval now;
    gettimeofday(&now, nullptr);

    timesince = (double)(now.tv_sec * 1000.0 + now.tv_usec / 1000) -
                (double)(ExpStart.tv_sec * 1000.0 + ExpStart.tv_usec / 1000);
    timesince = timesince / 1000;

    timeleft = ExposureRequest - timesince;
    return timeleft;
}

/* Downloads the image from the CCD.*/
int FishCampCCD::grabImage()
{
    std::unique_lock<std::mutex> guard(ccdBufferLock);
    uint8_t *image = PrimaryCCD.getFrameBuffer();
    UInt16 *frameBuffer = (UInt16 *)image;
    int numBytes = fcUsb_cmd_getRawFrame(cameraNum, PrimaryCCD.getSubW(), PrimaryCCD.getSubH(), frameBuffer);
    guard.unlock();
    if(numBytes != 0)
    {
        LOG_INFO("Download complete.");
        ExposureComplete(&PrimaryCCD);
        return 0;
    }
    else
    {
        LOG_INFO("Download error. Please check the log for details.");
        ExposureComplete(&PrimaryCCD);  //This should be an error. It is not complete, it messed up!
        return -1;
    }

}

void FishCampCCD::TimerHit()
{
    int timerHitID = -1, state = -1, rc = -1;
    float timeleft;
    double ccdTemp;

    if (!isConnected())
        return; //  No need to reset timer if we are not connected anymore

    if (InExposure)
    {
        timeleft = CalcTimeLeft();

        if (timeleft < 1.0)
        {
            if (timeleft > 0.25)
            {
                //  a quarter of a second or more
                //  just set a tighter timer
                timerHitID = SetTimer(250);
            }
            else
            {
                if (timeleft > 0.07)
                {
                    //  use an even tighter timer
                    timerHitID = SetTimer(50);
                }
                else
                {
                    //  it's real close now, so spin on it
                    //  We have to wait till the camera is ready to download
                    bool ready = false;
                    int slv;
                    slv = fabs(100000 * timeleft); //0.05s (50000 us) is checked every 5000 us or so
                    while (!sim && !ready)
                    {
                        state = fcUsb_cmd_getState(cameraNum);
                        if (state == 0)
                            ready = true;
                        else
                            usleep(slv);
                    }

                    /* We're done exposing */
                    LOG_DEBUG("Exposure done, downloading image...");

                    PrimaryCCD.setExposureLeft(0);
                    InExposure = false;
                    /* grab and save image */
                    grabImage();
                }
            }
        }
        else
        {
            LOGF_DEBUG("Image not yet ready. With time left %ld\n", timeleft);
            PrimaryCCD.setExposureLeft(timeleft);
        }
    }

    switch (TemperatureNP.getState())
    {
        case IPS_IDLE:
        case IPS_OK:
            rc = fcUsb_cmd_getTemperature(cameraNum);

            LOGF_DEBUG("fcUsb_cmd_getTemperature returns %d", rc);

            ccdTemp = rc / 100.0;

            LOGF_DEBUG("Temperature %g", ccdTemp);

            if (fabs(TemperatureNP[0].getValue() - ccdTemp) >= TEMP_THRESHOLD)
            {
                TemperatureNP[0].setValue(ccdTemp);
                TemperatureNP.apply();
            }

            break;

        case IPS_BUSY:
            if (sim)
            {
                TemperatureNP[0].setValue(TemperatureRequest);
            }
            else
            {
                rc = fcUsb_cmd_getTemperature(cameraNum);

                LOGF_DEBUG("fcUsb_cmd_getTemperature returns %d", rc);

                TemperatureNP[0].setValue(rc / 100.0);
            }

            // If we're within threshold, let's make it BUSY ---> OK
            //            if (fabs(TemperatureRequest - TemperatureN[0].value) <= TEMP_THRESHOLD)
            //                TemperatureNP.s = IPS_OK;

            TemperatureNP.apply();
            break;

        case IPS_ALERT:
            break;
    }

    switch (CoolerNP.s)
    {
        case IPS_OK:
            CoolerN[0].value = fcUsb_cmd_getTECPowerLevel(cameraNum);
            IDSetNumber(&CoolerNP, nullptr);
            LOGF_DEBUG("Cooler power level %g %", CoolerN[0].value);
            break;

        default:
            break;
    }

    if (timerHitID == -1)
        SetTimer(getCurrentPollingPeriod());
    return;
}

IPState FishCampCCD::GuideNorth(uint32_t ms)
{
    if (sim)
        return IPS_OK;

    int rc = 0;

    rc = fcUsb_cmd_pulseRelay(cameraNum, fcRELAYNORTH, ms, 0, false);

    LOGF_DEBUG("fcUsb_cmd_pulseRelay fcRELAYNORTH returns %d", rc);

    return IPS_OK;
}

IPState FishCampCCD::GuideSouth(uint32_t ms)
{
    if (sim)
        return IPS_OK;

    int rc = 0;

    rc = fcUsb_cmd_pulseRelay(cameraNum, fcRELAYSOUTH, ms, 0, false);

    LOGF_DEBUG("fcUsb_cmd_pulseRelay fcRELAYSOUTH returns %d", rc);

    return IPS_OK;
}

IPState FishCampCCD::GuideEast(uint32_t ms)
{
    if (sim)
        return IPS_OK;

    int rc = 0;

    rc = fcUsb_cmd_pulseRelay(cameraNum, fcRELAYEAST, ms, 0, false);

    LOGF_DEBUG("fcUsb_cmd_pulseRelay fcRELAYEAST returns %d", rc);

    return IPS_OK;
}

IPState FishCampCCD::GuideWest(uint32_t ms)
{
    if (sim)
        return IPS_OK;

    int rc = 0;

    rc = fcUsb_cmd_pulseRelay(cameraNum, fcRELAYWEST, ms, 0, false);

    LOGF_DEBUG("fcUsb_cmd_pulseRelay fcRELAYWEST returns %d", rc);

    return IPS_OK;
}
