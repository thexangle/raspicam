/**********************************************************
 Software developed by AVA ( Ava Group of the University of Cordoba, ava  at uco dot es)
 Main author Rafael Munoz Salinas (rmsalinas at uco dot es)
 This software is released under BSD license as expressed below
-------------------------------------------------------------------
Copyright (c) 2013, AVA ( Ava Group University of Cordoba, ava  at uco dot es)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
   must display the following acknowledgement:

   This product includes software developed by the Ava group of the University of Cordoba.

4. Neither the name of the University nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY AVA ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL AVA BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
****************************************************************/

#ifndef _Private_RaspiCam_STILL_IMPL_H
#define _Private_RaspiCam_STILL_IMPL_H
#include "raspicamtypes.h"
#include "mmal/mmal.h"
#include "mmal/util/mmal_connection.h"
#include <string>
#define MMAL_CAMERA_PORT_COUNT 3
#define MMAL_CAMERA_CAPTURE_PORT 2
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_PREVIEW_PORT 0


// Stills format information
// 0 implies variable
#define STILLS_FRAME_RATE_NUM 0
#define STILLS_FRAME_RATE_DEN 1
namespace raspicam
{
   namespace _private
   {
      typedef void (*imageTakenCallback)(unsigned char *data, unsigned int image_offset, unsigned int length);

      class Private_Impl_Still
      {

      private:
         MMAL_COMPONENT_T *camera;              /// Pointer to the camera component
         MMAL_COMPONENT_T *encoder;             /// Pointer to the encoder component
         MMAL_COMPONENT_T *preview_component;   /// Pointer to the preview component
         MMAL_CONNECTION_T *encoder_connection; // Connection from the camera to the encoder
         MMAL_CONNECTION_T *preview_connection; // Connection from the camera to the encoder
         MMAL_POOL_T *encoder_pool;             /// Pointer to the pool of buffers used by encoder output port
         MMAL_PORT_T *camera_still_port;
         MMAL_PORT_T *preview_port;
         MMAL_PORT_T *video_port;
         MMAL_PORT_T *preview_input_port;
         MMAL_PORT_T *encoder_input_port;
         MMAL_PORT_T *encoder_output_port;
         unsigned int width;
         unsigned int height;
         unsigned int rotation;      // 0 to 359
         unsigned int brightness;    // 0 to 100
         unsigned int quality;       // 0 to 100
         unsigned int shutter_speed; //in microseconds
         unsigned int measured_shutter_speed; //in microseconds
         bool burst_mode;
         int iso;
         int sharpness;  // -100 to 100
         int contrast;   // -100 to 100
         int saturation; // -100 to 100
         RASPICAM_ENCODING encoding;
         RASPICAM_EXPOSURE exposure;
         RASPICAM_AWB awb;
         RASPICAM_IMAGE_EFFECT imageEffect;
         RASPICAM_METERING metering;
         bool changedSettings;
         bool changedResolution;
         bool horizontalFlip;
         bool verticalFlip;
         float analogGain;
         float digitalGain;
         float awbRedGain;
         float awbBlueGain;
         float measured_awbRedGain;
         float measured_awbBlueGain;

         ControlCallback *userControlCallback;

         MMAL_FOURCC_T convertEncoding(RASPICAM_ENCODING encoding);
         MMAL_PARAM_EXPOSUREMETERINGMODE_T convertMetering(RASPICAM_METERING metering);
         MMAL_PARAM_EXPOSUREMODE_T convertExposure(RASPICAM_EXPOSURE exposure);
         MMAL_PARAM_AWBMODE_T convertAWB(RASPICAM_AWB awb);
         MMAL_PARAM_IMAGEFX_T convertImageEffect(RASPICAM_IMAGE_EFFECT imageEffect);
         void commitBrightness();
         void commitQuality();
         void commitShutterSpeed();
         void commitRotation();
         void commitISO();
         void commitSharpness();
         void commitContrast();
         void commitSaturation();
         void commitExposure();
         void commitAWB();
         void commitImageEffect();
         void commitMetering();
         void commitFlips();
         void commitResolution();
         void commitGains();
         void commitAwbGains();
         int startCapture();
         int startPreviewCapture();
         int createCamera();
         int createPreview(bool noPreview = true);
         int createEncoders();

         int disableCamera();
         int enableCamera();

         void destroyCamera();
         void destroyEncoders();
         void setDefaults();
         MMAL_STATUS_T connectPorts(MMAL_PORT_T *output_port, MMAL_PORT_T *input_port, MMAL_CONNECTION_T **connection);
         void disconnectPorts();
         bool emptyCapture();


         int setFPSRange();
         int setPortFormats();

         bool _isInitialized;

      public:
         const char *API_NAME;
         Private_Impl_Still()
         {
            API_NAME = "Private_Impl_Still";
            setDefaults();
            camera = NULL;
            encoder = NULL;
            preview_component = NULL;
            encoder_connection = NULL;
            preview_connection = NULL;
            encoder_pool = NULL;
            camera_still_port = NULL;
            preview_port = NULL;
            video_port = NULL;
            preview_input_port = NULL;
            encoder_input_port = NULL;
            encoder_output_port = NULL;
            userControlCallback = NULL;
            _isInitialized = false;
         }
         //called by control callback
         void updateSettings(MMAL_PARAMETER_CAMERA_SETTINGS_T *settings);

         int initialize();
         int startCapture(imageTakenCallback userCallback, unsigned char *preallocated_data, unsigned int offset, unsigned int length);
         void stopCapture(MMAL_PORT_T *port);
         bool takePicture(unsigned char *preallocated_data, unsigned int length);
         bool takePicture(const char *filename);
         bool take_picture_in_mem(char **dynamically_allocated_data, size_t *output_size);
         ;
         void release();

         size_t getImageBufferSize() const;
         void get_sensor_defaults(int camera_num, char *camera_name, int *width, int *height);
         void bufferCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);
         void commitParameters();
         void setBurstMode(bool mode);
         void setWidth(unsigned int width);
         void setHeight(unsigned int height);
         void setCaptureSize(unsigned int width, unsigned int height);
         void setBrightness(unsigned int brightness);
         void setQuality(unsigned int quality);
         void setShutterSpeed(unsigned int ss);
         void setMeasuredShutterSpeed(unsigned int ss);
         void setRotation(int rotation);
         void setISO(int iso);
         void setSharpness(int sharpness);
         void setContrast(int contrast);
         void setSaturation(int saturation);
         void setEncoding(RASPICAM_ENCODING encoding);
         void setExposure(RASPICAM_EXPOSURE exposure);
         void setAWB(RASPICAM_AWB awb);
         void setImageEffect(RASPICAM_IMAGE_EFFECT imageEffect);
         void setMetering(RASPICAM_METERING metering);
         void setHorizontalFlip(bool hFlip);
         void setVerticalFlip(bool vFlip);
         void setAnalogGain(float gain);
         void setDigitalGain(float gain);
         void setAwbRedGain(float gain);
         void setAwbBlueGain(float gain);
         void setMeasuredAwbRedGain(float gain);
         void setMeasuredAwbBlueGain(float gain);
         void setControlCallback(ControlCallback *callback);

         bool getBurstMode();
         unsigned int getWidth();
         unsigned int getHeight();
         unsigned int getBrightness();
         unsigned int getRotation();
         unsigned int getQuality();
         unsigned int getShutterSpeed();
         unsigned int getMeasuredShutterSpeed();
         
         int getISO();
         int getSharpness();
         int getContrast();
         int getSaturation();
         RASPICAM_ENCODING getEncoding();
         RASPICAM_EXPOSURE getExposure();
         RASPICAM_AWB getAWB();
         RASPICAM_IMAGE_EFFECT getImageEffect();
         RASPICAM_METERING getMetering();
         bool isHorizontallyFlipped();
         bool isVerticallyFlipped();
         float getDigitalGain();
         float getAnalogGain();
         float getAwbRedGain();
         float getAwbBlueGain();
         float getMeasuredAwbRedGain();
         float getMeasuredAwbBlueGain();

         ControlCallback *getControlCallback();

         //Returns an id of the camera. We assume the camera id is the one of the raspberry
         //the id is obtained using raspberry serial number obtained in /proc/cpuinfo
         std::string getId() const;
      };

   } // namespace _private
} // namespace raspicam
#endif // RASPICAM_H
