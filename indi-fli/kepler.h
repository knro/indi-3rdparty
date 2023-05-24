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

#pragma once

#include <libflipro.h>
#include <indiccd.h>
#include <indipropertyswitch.h>
#include <indipropertytext.h>
#include <indipropertylight.h>
#include <indipropertyblob.h>

#include <inditimer.h>
#include <indisinglethreadpool.h>

#ifdef LEGACY_MODE
/* Pointing parameter */
typedef enum {	/* N.B. order must match array in vector pointing_elements[] */
  RA2K_TP, DEC2K_TP, RAEOD_TP, DECEOD_TP, HA_TP, ALT_TP, AZ_TP, AM_TP,
  PA_TP, XVEL_TP, YVEL_TP, XFE_TP, YFE_TP, FOCUS_TP, JD_TP, N_TP
} PointingIndex;
extern INumberVectorProperty pointing;


/* SetCatalog parameter */
typedef enum {	/* N.B. order must match array in vector setcatalog_elements[]*/
  ENTRY_SC, N_SC
} SetCatalogIndex;
extern ITextVectorProperty setcatalog;


/* SetAltAz parameter */
typedef enum {	/* N.B. order must match array in vector setaltaz_elements[] */
  ALT_SAA, AZ_SAA, N_SAA
} SetAltAzIndex;
extern INumberVectorProperty setaltaz;

/* SetHADec parameter */
typedef enum {	/* N.B. order must match array in vector sethadec_elements[] */
  HA_SHD, DEC_SHD, N_SHD
} SetHADecIndex;
extern INumberVectorProperty sethadec;

/* SetRADec2K parameter */
typedef enum {	/* N.B. order must match array in vector setradec2k_elements[]*/
  RA_SRD2K, DEC_SRD2K, N_SRD2K
} SetRADec2KIndex;
extern INumberVectorProperty setradec2k;

/* SetVelocity parameter */
typedef enum {	/*N.B. order must match array in vector setvelocity_elements[]*/
  HA_SV, DEC_SV, N_SV
} SetVelocityIndex;
extern INumberVectorProperty setvelocity;

/* Now parameter */
typedef enum {	/* N.B. order must match array now_elements[] */
  TEMP_NOW, DP_NOW, WINDC_NOW, AIRPR_NOW, HUMIDITY_NOW, WINDDIR_NOW,
  WINDSPD_NOW, GUST_NOW, RAINACCUM_NOW, RAINDET_NOW, EFIELD_NOW,
  EFIELDJD_NOW, N_NOW
} NowIndex;
extern INumberVectorProperty envnow;

/* roof/ram states.
 * N.B. values must match ROOF/RAM_NOW
 */
typedef enum {
    RRMIDWAY = -1, RRCLOSED = 0, RROPENED = 1
} RRState;

/* Now parameter */
typedef enum {	/* N.B. order must match array below */
  H1_OWNOW, D1_OWNOW, T1_OWNOW,
  H2_OWNOW, D2_OWNOW, T2_OWNOW,
  H3_OWNOW, D3_OWNOW, T3_OWNOW,
  T4_OWNOW,
  T5_OWNOW,
  ROOF_OWNOW, RAM_OWNOW,
  N_OWNOW
} OWNowIndex;
extern INumberVectorProperty ownow;

/* Blind parameter */
typedef enum {  /* N.B. order must match array in vector blind_elements[] */
  OPEN_BLD, N_BLD
} BlindIndex;
extern ISwitchVectorProperty blind;

#endif

class Kepler : public INDI::CCD
{
    public:
        Kepler(const FPRODEVICEINFO &info, std::wstring name);
        virtual ~Kepler() override;

        const char *getDefaultName() override;

        bool initProperties() override;
        void ISGetProperties(const char *dev) override;
        bool updateProperties() override;

        bool Connect() override;
        bool Disconnect() override;

        bool StartExposure(float duration) override;
        bool AbortExposure() override;

        virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
        virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
        virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;
        virtual bool ISSnoopDevice(XMLEle * root) override;

    protected:
        virtual int SetTemperature(double temperature) override;
        virtual bool UpdateCCDFrame(int x, int y, int w, int h) override;
        virtual bool UpdateCCDBin(int binx, int biny) override;
        virtual bool UpdateCCDFrameType(INDI::CCDChip::CCD_FRAME fType) override;

        virtual void debugTriggered(bool enable) override;
        virtual bool saveConfigItems(FILE *fp) override;

        virtual void addFITSKeywords(INDI::CCDChip *targetChip, std::vector<INDI::FITSRecord> &fitsKeywords) override;
        virtual void UploadComplete(INDI::CCDChip *targetChip) override;

    private:

        /**
         * @brief addMetadataFITSHeader Add new FITS header value
         * @param fitsKeywords Reference to FITS keywords list to add new header to.
         * @param id LIBFLIPRO Metadata ID
         * @param keyword Header keyword field
         * @param comment Header comment field
         * @param precision for double keywords, the precision that should be used.
         */
      void addMetadataFITSHeader(std::vector<INDI::FITSRecord> &fitsKeywords,
                                 FPRO_META_KEYS id,
                                 const std::string &keyword,
                                 const std::string &comment, int precision = 3);

        //****************************************************************************************
        // INDI Properties
        //****************************************************************************************
        INDI::PropertySwitch CommunicationMethodSP {2};


        // Gain
        INDI::PropertySwitch LowGainSP {0};
        INDI::PropertySwitch HighGainSP {0};

        // Cooler & Fan
        INDI::PropertyNumber CoolerDutyNP {1};
        INDI::PropertySwitch FanSP {2};

        // Merging        
        INDI::PropertySwitch MergePlanesSP {3};
        INDI::PropertySwitch RequestStatSP {2};
        INDI::PropertyText MergeCalibrationFilesTP {2};
        enum
        {
            CALIBRATION_DARK,
            CALIBRATION_FLAT
        };

        // Black Level Adjust
        INDI::PropertyNumber BlackLevelNP {2};

        // Black Sun Level Adjust
        INDI::PropertyNumber BlackSunLevelNP {2};

        // GPS State
        INDI::PropertyLight GPSStateLP {4};

#ifdef LEGACY_MODE
        //****************************************************************************************
        // Legacy INDI Properties
        //****************************************************************************************
        INDI::PropertyNumber ExpValuesNP {11};
        enum
        {
            ExpTime,
            ROIW,
            ROIH,
            OSW,
            OSH,
            BinW,
            BinH,
            ROIX,
            ROIY,
            Shutter,
            Type
        };
        INDI::PropertySwitch ExposureTriggerSP {1};
        INDI::PropertyNumber TemperatureSetNP {1};
        INDI::PropertyNumber TemperatureReadNP {2};
        double m_ExposureRequest {1};
#endif


        //****************************************************************************************
        // Communication Functions
        //****************************************************************************************
        bool setup();
        void prepareUnpacked();
        void readTemperature();
        void readGPS();

        //****************************************************************************************
        // Workers
        //****************************************************************************************
        void workerStreamVideo(const std::atomic_bool &isAboutToQuit);
        void workerExposure(const std::atomic_bool &isAboutToQuit, float duration);

        //****************************************************************************************
        // Legacy
        //****************************************************************************************
#ifdef LEGACY_MODE
        void initMedianVels();
        void addMedianVels();
        void checkMaxFE();
        void getMedianVels(double *mxvp, double *myvp);
#endif

        //****************************************************************************************
        // Variables
        //****************************************************************************************
        FPRODEVICEINFO m_CameraInfo;
        int32_t m_CameraHandle;
        uint32_t m_CameraCapabilitiesList[static_cast<uint32_t>(FPROCAPS::FPROCAP_NUM)];;

        uint8_t m_ExposureRetry {0};
        INDI::SingleThreadPool m_Worker;
        uint32_t m_TotalFrameBufferSize {0};

        // Merging
        uint8_t *m_FrameBuffer {nullptr};
        FPROUNPACKEDIMAGES fproUnpacked;
        FPROUNPACKEDSTATS  fproStats;
        FPRO_HWMERGEENABLE mergeEnables;

        // Format
        uint32_t m_FormatsCount;
        FPRO_PIXEL_FORMAT *m_FormatList {nullptr};

        // GPS
        FPROGPSSTATE m_LastGPSState {FPROGPSSTATE::FPRO_GPS_NOT_DETECTED};

        // Temperature
        INDI::Timer m_TemperatureTimer;
        INDI::Timer m_GPSTimer;

        // Gain Tables
        FPROGAINVALUE *m_LowGainTable {nullptr};
        FPROGAINVALUE *m_HighGainTable {nullptr};

#ifdef LEGACY_MODE
        /* collect and find median X/YVEL_TP and max X/YFE */
        double *xvels {nullptr}, *yvels {nullptr};      /* malloced arrays */
        int nxvels {0}, nyvels {0};                     /* n entries in each array */
        double maxxfe {0}, maxyfe {0};                  /* max values */
        double havel {0}, decvel {0};                   /* commanded velocity, if applicable */
        char *OBJECT {nullptr};                         /* last-known target name */
#endif

        static std::map<FPRODEVICETYPE, double> SensorPixelSize;
        static constexpr double TEMPERATURE_THRESHOLD {0.1};
        static constexpr double TEMPERATURE_FREQUENCY_BUSY {1000};
        static constexpr double TEMPERATURE_FREQUENCY_IDLE {5000};
        static constexpr uint32_t GPS_TIMER_PERIOD {5000};

        static constexpr const char *GPS_TAB {"GPS"};
        static constexpr const char *LEGACY_TAB {"Legacy"};
};
