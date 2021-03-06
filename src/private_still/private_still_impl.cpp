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

#include <fstream>
#include "private_still_impl.h"
#include "mmal/mmal_buffer.h"
#include "mmal/util/mmal_default_components.h"
#include "mmal/util/mmal_util.h"
#include "mmal/util/mmal_util_params.h"
#include <iostream>
#include <semaphore.h>
#include <chrono>

using namespace std;
using namespace std::chrono;
namespace raspicam
{
    namespace _private
    {
        typedef struct
        {
            Private_Impl_Still *cameraBoard;
            MMAL_POOL_T *encoderPool;
            imageTakenCallback imageCallback;
            sem_t *mutex;
            unsigned char *data;
            unsigned int bufferPosition;
            unsigned int startingOffset;
            unsigned int offset;
            unsigned int length;
            FILE *file_handle; /// File handle to write buffer data to.
        } RASPICAM_USERDATA;

        static void control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
        {
            Private_Impl_Still *cameraBoard = NULL;
            if (port->userdata)
            {
                cameraBoard = (Private_Impl_Still *)port->userdata;
            }
            if (buffer->cmd == MMAL_EVENT_PARAMETER_CHANGED && cameraBoard)
            {
                MMAL_EVENT_PARAMETER_CHANGED_T *param = (MMAL_EVENT_PARAMETER_CHANGED_T *)buffer->data;
                switch (param->hdr.id)
                {
                case MMAL_PARAMETER_CAMERA_SETTINGS:
                {
                    MMAL_PARAMETER_CAMERA_SETTINGS_T *settings = (MMAL_PARAMETER_CAMERA_SETTINGS_T *)param;
                    cameraBoard->updateSettings(settings);
                    if (cameraBoard->getControlCallback())
                    {
                        cameraBoard->getControlCallback()->CameraSettingChanged();
                    }

                    // printf("Exposure now %u, analog gain %u/%u, digital gain %u/%u\n",
                    //                 settings->exposure,
                    //                 settings->analog_gain.num, settings->analog_gain.den,
                    //                 settings->digital_gain.num, settings->digital_gain.den);
                    // printf("AWB R=%u/%u, B=%u/%u\n",
                    //                 settings->awb_red_gain.num, settings->awb_red_gain.den,
                    //                 settings->awb_blue_gain.num, settings->awb_blue_gain.den);
                }
                break;
                case MMAL_PARAMETER_CAPTURE_STATUS:
                {
                    MMAL_PARAMETER_CAPTURE_STATUS_T *status = (MMAL_PARAMETER_CAPTURE_STATUS_T *)param;
                    if (status->status == MMAL_PARAM_CAPTURE_STATUS_CAPTURE_STARTED)
                    {
                        if (cameraBoard->getControlCallback())
                        {
                            cameraBoard->getControlCallback()->CaptureStarted();
                        }
                        printf("%i MMAL_PARAM_CAPTURE_STATUS_CAPTURE_STARTED\n", duration_cast<milliseconds>(system_clock::now().time_since_epoch()));
                    }

                    else if (status->status == MMAL_PARAM_CAPTURE_STATUS_CAPTURE_ENDED)
                    {
                        if (cameraBoard->getControlCallback())
                        {
                            cameraBoard->getControlCallback()->CaptureEnded();
                        }
                        printf("%i MMAL_PARAM_CAPTURE_STATUS_CAPTURE_ENDED\n", duration_cast<milliseconds>(system_clock::now().time_since_epoch()));
                    }
                }
                break;
                }
            }
            else
            {
                // Unexpected control callback event!
            }
            mmal_buffer_header_release(buffer);
        }

        static void buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
        {
            RASPICAM_USERDATA *userdata = (RASPICAM_USERDATA *)port->userdata;
            if (userdata == NULL || userdata->cameraBoard == NULL)
            {
            }
            else
            {
                unsigned int flags = buffer->flags;
                mmal_buffer_header_mem_lock(buffer);
                if (buffer->length && userdata->file_handle)
                {
                    int bytes_written = buffer->length;
                    bytes_written = fwrite(buffer->data, 1, buffer->length, userdata->file_handle);
                    // We need to check we wrote what we wanted - it's possible we have run out of storage.
                    if (bytes_written != buffer->length)
                    {
                        cout << userdata->cameraBoard->API_NAME << "Unable to write buffer to file - aborting";
                    }
                }

                else if (userdata->length && userdata->data)
                {
                    for (unsigned int i = 0; i < buffer->length; i++, userdata->bufferPosition++)
                    {
                        if (userdata->offset >= userdata->length)
                        {
                            cout << userdata->cameraBoard->API_NAME << ": Buffer provided was too small! Failed to copy data into buffer.\n";
                            userdata->cameraBoard = NULL;
                            break;
                        }
                        else
                        {
                            if (userdata->cameraBoard->getEncoding() == RASPICAM_ENCODING_RGB)
                            {
                                // Determines if the byte is an RGB value
                                if (userdata->bufferPosition >= 54)
                                {
                                    userdata->data[userdata->offset] = buffer->data[i];
                                    userdata->offset++;
                                }
                            }
                            else
                            {
                                userdata->data[userdata->offset] = buffer->data[i];
                                userdata->offset++;
                            }
                        }
                    }
                }

                mmal_buffer_header_mem_unlock(buffer);
                unsigned int END_FLAG = 0;
                END_FLAG |= MMAL_BUFFER_HEADER_FLAG_FRAME_END;
                END_FLAG |= MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED;
                END_FLAG &= flags;
                if (END_FLAG != 0)
                {
                    if (userdata->mutex == NULL)
                    {
                        userdata->imageCallback(userdata->data, userdata->startingOffset, userdata->length - userdata->startingOffset);
                    }
                    else
                    {
                        sem_post(userdata->mutex);
                    }
                }
            }
            mmal_buffer_header_release(buffer);
            if (port->is_enabled)
            {
                MMAL_BUFFER_HEADER_T *new_buffer = mmal_queue_get(userdata->encoderPool->queue);
                if (new_buffer)
                    mmal_port_send_buffer(port, new_buffer);
            }
        }

        void Private_Impl_Still::updateSettings(MMAL_PARAMETER_CAMERA_SETTINGS_T *settings)
        {
            if (settings)
            {
                measured_shutter_speed = settings->exposure;
                analogGain = (float)settings->analog_gain.num / (float)(settings->analog_gain.den ? settings->analog_gain.den : 1.0f);
                digitalGain = (float)settings->digital_gain.num / (float)(settings->digital_gain.den ? settings->digital_gain.den : 1.0f);
                measured_awbRedGain = (float)settings->awb_red_gain.num / (float)(settings->awb_red_gain.den ? settings->awb_red_gain.den : 1.0f);
                measured_awbBlueGain = (float)settings->awb_blue_gain.num / (float)(settings->awb_blue_gain.den ? settings->awb_blue_gain.den : 1.0f);
            }
        }

        void Private_Impl_Still::setDefaults()
        {
            burst_mode = false;
            width = 640;
            height = 480;
            encoding = RASPICAM_ENCODING_BMP;
            encoder = NULL;
            encoder_connection = NULL;
            sharpness = 0;
            contrast = 0;
            brightness = 50;
            quality = 85;
            shutter_speed = 0;
            measured_shutter_speed = 0;
            saturation = 0;
            iso = 400;
            //videoStabilisation = 0;
            //exposureCompensation = 0;
            exposure = RASPICAM_EXPOSURE_AUTO;
            metering = RASPICAM_METERING_AVERAGE;
            awb = RASPICAM_AWB_AUTO;
            imageEffect = RASPICAM_IMAGE_EFFECT_NONE;
            //colourEffects.enable = 0;
            //colourEffects.u = 128;
            //colourEffects.v = 128;
            rotation = 0;
            changedSettings = true;
            changedResolution = false;
            horizontalFlip = false;
            verticalFlip = false;
            analogGain = 0.0;
            digitalGain = 0.0;
            awbBlueGain = 0.0;
            awbRedGain = 0.0;
            measured_awbBlueGain = 0.0;
            measured_awbRedGain = 0.0;
            userControlCallback = NULL;
            //roi.x = params->roi.y = 0.0;
            //roi.w = params->roi.h = 1.0;
        }

        void Private_Impl_Still::commitParameters()
        {
            if (changedResolution)
            {
                commitResolution();
                changedResolution = false;
            }
            if (!changedSettings)
                return;
            cout << API_NAME << ": Commit parameters !.\n";
            commitSharpness();
            commitContrast();
            commitBrightness();
            commitQuality();
            commitSaturation();
            commitISO();
            commitExposure();
            commitShutterSpeed();
            commitMetering();
            commitAWB();
            commitAwbGains();
            commitImageEffect();
            commitRotation();
            commitFlips();
            commitGains();

            if (burst_mode)
            {
                mmal_port_parameter_set_boolean(camera->control, MMAL_PARAMETER_CAMERA_BURST_CAPTURE, 1);
            }
            else
            {
                mmal_port_parameter_set_boolean(camera->control, MMAL_PARAMETER_CAMERA_BURST_CAPTURE, 0);
            }

            cout << API_NAME << ": setting FLASH MODE parameter.\n";
            // MMAL_PARAMETER_FLASH_T param = {{MMAL_PARAMETER_FLASH, sizeof(param)}, MMAL_PARAM_FLASH_ON};
            // if (mmal_port_parameter_set(camera->control, &param.hdr) != MMAL_SUCCESS)
            //     cout << API_NAME << ": Failed to set FLASH MODE parameter.\n";

            // Set Video Stabilization
            if (mmal_port_parameter_set_boolean(camera->control, MMAL_PARAMETER_VIDEO_STABILISATION, 0) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set video stabilization parameter.\n";
            // Set Exposure Compensation
            if (mmal_port_parameter_set_int32(camera->control, MMAL_PARAMETER_EXPOSURE_COMP, 0) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set exposure compensation parameter.\n";
            // Set Color Efects
            MMAL_PARAMETER_COLOURFX_T colfx = {{MMAL_PARAMETER_COLOUR_EFFECT, sizeof(colfx)}, 0, 0, 0};
            colfx.enable = 0;
            colfx.u = 128;
            colfx.v = 128;
            if (mmal_port_parameter_set(camera->control, &colfx.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set color effects parameter.\n";
            // Set ROI
            MMAL_PARAMETER_INPUT_CROP_T crop = {{MMAL_PARAMETER_INPUT_CROP, sizeof(MMAL_PARAMETER_INPUT_CROP_T)}};
            crop.rect.x = (65536 * 0);
            crop.rect.y = (65536 * 0);
            crop.rect.width = (65536 * 1);
            crop.rect.height = (65536 * 1);
            if (mmal_port_parameter_set(camera->control, &crop.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set ROI parameter.\n";
            // Set encoder encoding
            if (encoder_output_port != NULL)
            {
                encoder_output_port->format->encoding = convertEncoding(encoding);
                mmal_port_format_commit(encoder_output_port);
            }
            MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T change_event_request =
                {
                    {MMAL_PARAMETER_CHANGE_EVENT_REQUEST, sizeof(MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T)},
                    MMAL_PARAMETER_CAMERA_SETTINGS,
                    1};

            MMAL_STATUS_T status = mmal_port_parameter_set(camera->control, &change_event_request.hdr);
            if (status != MMAL_SUCCESS)
            {
                printf("No camera settings events\n");
            }

            MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T change_event_request_capture =
                {
                    {MMAL_PARAMETER_CHANGE_EVENT_REQUEST, sizeof(MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T)},
                    MMAL_PARAMETER_CAPTURE_STATUS,
                    1};

            status = mmal_port_parameter_set(camera->control, &change_event_request_capture.hdr);
            if (status != MMAL_SUCCESS)
            {
                printf("No capture events\n");
            }

            //mmal_port_parameter_set_boolean(camera->control, MMAL_PARAMETER_CAPTURE_STATS_PASS, 1);

            changedSettings = false;
        }

        MMAL_STATUS_T Private_Impl_Still::connectPorts(MMAL_PORT_T *output_port, MMAL_PORT_T *input_port, MMAL_CONNECTION_T **connection)
        {
            MMAL_STATUS_T status = mmal_connection_create(connection, output_port, input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
            if (status == MMAL_SUCCESS)
            {
                status = mmal_connection_enable(*connection);
                if (status != MMAL_SUCCESS)
                    mmal_connection_destroy(*connection);
            }

            return status;
        }

        void Private_Impl_Still::disconnectPorts()
        {
            // disable connection if enabled
            if (encoder_connection && encoder_connection->is_enabled && mmal_connection_disable(encoder_connection) != MMAL_SUCCESS)
            {
                cout << API_NAME << ": fail to disable encoder connection\n";
            }

            // destroy encoder connection
            // mmal_connection_disable call is mandatory before calling the destroy function otherwise it fails
            if (encoder_connection && mmal_connection_destroy(encoder_connection) != MMAL_SUCCESS)
            {
                cout << API_NAME << ": fail to destroy encoder connection\n";
            }
            encoder_connection = NULL;
        }

        int Private_Impl_Still::setFPSRange()
        {
            MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                    {999, 1000},
                                                    {120, 1}};

            if (shutter_speed > 6000000)
            {
                fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                             {5, 1000},
                             {166, 1000}};
            }
            else if (shutter_speed > 1000000)
            {
                MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                        {166, 1000},
                                                        {999, 1000}};
            }
            int result = MMAL_SUCCESS;
            for (int i = 0; i < MMAL_CAMERA_PORT_COUNT; ++i)
            {
                if (mmal_port_parameter_set(camera->output[i], &fps_range.hdr) != MMAL_SUCCESS)
                {
                    cout << API_NAME << ": Failed to set fps range: " << fps_range.fps_low.num << "/" << fps_range.fps_low.den << " .. " << fps_range.fps_high.num << "/" << fps_range.fps_high.den << " on port: " << i << "\n";
                    result = -1;
                }
            }
            return result;
        }

        int Private_Impl_Still::setPortFormats()
        {

            // /************************************************/
            // /*               SETUP preview port               */
            // /************************************************/
            // // Now set up the port formats
            MMAL_ES_FORMAT_T *format = preview_port->format;
            format->encoding = MMAL_ENCODING_OPAQUE;
            format->encoding_variant = MMAL_ENCODING_I420;
            format->es->video.width = 1024;
            format->es->video.height = 768;
            format->es->video.crop.x = 0;
            format->es->video.crop.y = 0;
            format->es->video.crop.width = 1024;
            format->es->video.crop.height = 768;
            format->es->video.frame_rate.num = 0;
            format->es->video.frame_rate.den = 1;

            MMAL_STATUS_T status = mmal_port_format_commit(preview_port);
            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << "camera preview format couldn't be set" << endl;
                return -1;
            }

            /************************************************/
            /*               SETUP video port (todo)        */
            /************************************************/

            // Set the same format on the video  port (which we don't use here)
            mmal_format_full_copy(video_port->format, format);
            status = mmal_port_format_commit(video_port);

            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << " camera video format couldn't be set" << endl;
                return -1;
            }

            //Ensure there are enough buffers to avoid dropping frames
            if (video_port->buffer_num < 3)
                video_port->buffer_num = 3;

            /************************************************/
            /*               SETUP still port         */
            /************************************************/

            format = camera_still_port->format;
            format->encoding = MMAL_ENCODING_OPAQUE;
            format->es->video.width = width;
            format->es->video.height = height;
            format->es->video.crop.x = 0;
            format->es->video.crop.y = 0;
            format->es->video.crop.width = width;
            format->es->video.crop.height = height;
            format->es->video.frame_rate.num = STILLS_FRAME_RATE_NUM;
            format->es->video.frame_rate.den = STILLS_FRAME_RATE_DEN;

            if (camera_still_port->buffer_size < camera_still_port->buffer_size_min)
                camera_still_port->buffer_size = camera_still_port->buffer_size_min;

            camera_still_port->buffer_num = camera_still_port->buffer_num_recommended;

            if (mmal_port_format_commit(camera_still_port))
            {
                cout << API_NAME << ": Camera still format could not be set.\n";
                return -1;
            }

            /* Ensure there are enough buffers to avoid dropping frames */
            if (camera_still_port->buffer_num < 3)
                camera_still_port->buffer_num = 3;

            return MMAL_SUCCESS;
        }

        int Private_Impl_Still::createCamera()
        {
            MMAL_STATUS_T status = MMAL_SUCCESS;

            if (mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera))
            {
                cout << API_NAME << ": Failed to create camera component.\n";
                destroyCamera();
                return -1;
            }

            if (!camera->output_num)
            {
                cout << API_NAME << ": Camera does not have output ports!\n";
                destroyCamera();
                return -1;
            }
            MMAL_PARAMETER_INT32_T camera_num = {{MMAL_PARAMETER_CAMERA_NUM, sizeof(camera_num)}, 0};

            status = mmal_port_parameter_set(camera->control, &camera_num.hdr);
            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Could not select camera!\n";
                destroyCamera();
                return -1;
            }

            status = mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG, 0);
            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Could not select sensor mode 0!\n";
                destroyCamera();
                return -1;
            }
            // Enable the camera, and tell it its control callback function
            camera->control->userdata = (struct MMAL_PORT_USERDATA_T *)this;
            if (mmal_port_enable(camera->control, control_callback))
            {
                cout << API_NAME << ": Could not enable control port.\n";
                destroyCamera();
                return -1;
            }

            camera_still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];
            preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
            video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];

            MMAL_PARAMETER_CAMERA_CONFIG_T camConfig = {
                {MMAL_PARAMETER_CAMERA_CONFIG, sizeof(camConfig)},
                width,                              // max_stills_w
                height,                             // max_stills_h
                0,                                  // stills_yuv422
                1,                                  // one_shot_stills
                1024,                               // max_preview_video_w
                768,                                // max_preview_video_h
                3,                                  // num_preview_video_frames
                0,                                  // stills_capture_circular_buffer_height
                0,                                  // fast_preview_resume
                MMAL_PARAM_TIMESTAMP_MODE_RESET_STC // use_stc_timestamp
            };
            if (mmal_port_parameter_set(camera->control, &camConfig.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Could not set port parameters.\n";

            changedResolution = false; //we just did set the resolution + camera is not fully started
            changedSettings = true;
            commitParameters();

            setPortFormats();
            setFPSRange();

            if (mmal_component_enable(camera))
            {
                cout << API_NAME << ": Camera component could not be enabled.\n";
                destroyCamera();
                return -1;
            }

            // This is the reason I could not free fully the camera because this pool could never be freed since
            // the pointer encoder_pool was reassigned in createEncoders function, we lost its reference
            // so this allocation is useless or misused -> camera_pool ?
            /*if ( ! ( encoder_pool = mmal_port_pool_create ( camera_still_port, camera_still_port->buffer_num, camera_still_port->buffer_size ) ) ) {
                    cout << API_NAME << ": Failed to create buffer header pool for camera.\n";
                    destroyCamera();
                    return -1;
            }*/

            return 0;
        }

        void Private_Impl_Still::setControlCallback(ControlCallback *callback)
        {
            this->userControlCallback = callback;
        }

        ControlCallback *Private_Impl_Still::getControlCallback()
        {
            return this->userControlCallback;
        }

        int Private_Impl_Still::createPreview(bool noPreview)
        {
            MMAL_COMPONENT_T *preview = 0;
            MMAL_PORT_T *preview_port = NULL;
            MMAL_STATUS_T status;
            if (noPreview)
            {
                // No preview required, so create a null sink component to take its place
                status = mmal_component_create("vc.null_sink", &preview);

                if (status != MMAL_SUCCESS)
                {
                    cout << API_NAME << "Unable to create null sink component" << std::endl;
                    goto error;
                }
            }
            else
            {


                status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER,
                                            &preview);

                if (status != MMAL_SUCCESS)
                {
                    cout << API_NAME << "Unable to create preview component" << endl;
                    goto error;
                }

                if (!preview->input_num)
                {
                    status = MMAL_ENOSYS;
                    cout << API_NAME << "No input ports found on component" << endl;
                    goto error;
                }

                preview_port = preview->input[0];

                MMAL_DISPLAYREGION_T param;
                param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
                param.hdr.size = sizeof(MMAL_DISPLAYREGION_T);

                param.set = MMAL_DISPLAY_SET_LAYER;
                param.layer = 2; // PREVIEW_LAYER;

                param.set |= MMAL_DISPLAY_SET_ALPHA;
                param.alpha = 255;

                MMAL_RECT_T previewWindow;
                previewWindow.x = 0;
                previewWindow.y = 0;
                previewWindow.width = 1024;
                previewWindow.height = 768;

                param.set |= (MMAL_DISPLAY_SET_DEST_RECT | MMAL_DISPLAY_SET_FULLSCREEN);
                param.fullscreen = 0;
                param.dest_rect = previewWindow;

                status = mmal_port_parameter_set(preview_port, &param.hdr);

                if (status != MMAL_SUCCESS && status != MMAL_ENOSYS)
                {
                    cout << API_NAME << "unable to set preview port parameters : " << status << endl;
                    goto error;
                }
            }
            /* Enable component */
            status = mmal_component_enable(preview);

            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << "Unable to enable preview/null sink component " << status << endl;
                goto error;
            }

            preview_component = preview;
            return 0;

        error:

            if (preview)
                mmal_component_destroy(preview);

            return -1;
        }

        int Private_Impl_Still::createEncoders()
        {
            if (mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_ENCODER, &encoder))
            {
                cout << API_NAME << ": Could not create encoder component.\n";
                destroyEncoders();
                return -1;
            }
            if (!encoder->input_num || !encoder->output_num)
            {
                cout << API_NAME << ": Encoder does not have input/output ports.\n";
                destroyEncoders();
                return -1;
            }

            encoder_input_port = encoder->input[0];
            encoder_output_port = encoder->output[0];

            mmal_format_copy(encoder_output_port->format, encoder_input_port->format);

            encoder_output_port->format->encoding = convertEncoding(encoding); // Set output encoding
            encoder_output_port->buffer_size = encoder_output_port->buffer_size_recommended;
            if (encoder_output_port->buffer_size < encoder_output_port->buffer_size_min)
                encoder_output_port->buffer_size = encoder_output_port->buffer_size_min;

            encoder_output_port->buffer_num = encoder_output_port->buffer_num_recommended;
            if (encoder_output_port->buffer_num < encoder_output_port->buffer_num_min)
                encoder_output_port->buffer_num = encoder_output_port->buffer_num_min;

            if (mmal_port_format_commit(encoder_output_port))
            {
                cout << API_NAME << ": Could not set format on encoder output port.\n";
                destroyEncoders();
                return -1;
            }

            // Set the JPEG quality level
            if (mmal_port_parameter_set_uint32(encoder_output_port, MMAL_PARAMETER_JPEG_Q_FACTOR, quality) != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Could not set jpeg quality.\n";
            }

            if (mmal_component_enable(encoder))
            {
                cout << API_NAME << ": Could not enable encoder component.\n";
                destroyEncoders();
                return -1;
            }
            if (!(encoder_pool = mmal_port_pool_create(encoder_output_port, encoder_output_port->buffer_num, encoder_output_port->buffer_size)))
            {
                cout << API_NAME << ": Failed to create buffer header pool for encoder output port.\n";
                destroyEncoders();
                return -1;
            }

            return 0;
        }

        int Private_Impl_Still::disableCamera()
        {
            cout << API_NAME << ": Disabling camera\n";

            if (encoder_output_port && encoder_output_port->is_enabled)
                mmal_port_disable(encoder_output_port);

            disconnectPorts();

            if (camera)
                mmal_component_disable(camera);
            return 0;
        }

        int Private_Impl_Still::enableCamera()
        {
            cout << API_NAME << ": Enabling camera\n";
            if (mmal_component_enable(camera))
            {
                cout << API_NAME << ": Camera component could not be enabled.\n";
                return -1;
            }

            if (connectPorts(camera_still_port, encoder_input_port, &encoder_connection) != MMAL_SUCCESS)
            {
                cout << "ERROR: Could not connect encoder ports!\n";
                return -1;
            }
            cout << API_NAME << ": camera enabled\n";

            return 0;
        }

        void Private_Impl_Still::destroyCamera()
        {
            if (camera)
            {
                camera->control->userdata = NULL;
                mmal_component_destroy(camera);
                camera = NULL;
                camera_still_port = NULL;
                preview_port = NULL;
                video_port = NULL;
            }
        }

        void Private_Impl_Still::destroyEncoders()
        {
            if (encoder_pool)
            {
                mmal_port_pool_destroy(encoder->output[0], encoder_pool);
                encoder_pool = NULL;
            }
            if (encoder)
            {
                mmal_component_destroy(encoder);
                encoder = NULL;
                encoder_output_port = NULL;
                encoder_input_port = NULL;
            }
        }

        int Private_Impl_Still::initialize()
        {
            if (_isInitialized)
                return 0;
            if (createCamera())
            {
                cout << API_NAME << ": Failed to create camera component.\n";
                destroyCamera();
                return -1;
            }
            if (createPreview())
            {
                cout << API_NAME << ": Failed to create preview component.\n";
                destroyCamera();
                return -1;
            }
            if (createEncoders())
            {
                cout << API_NAME << ": Failed to create encoder component.\n";
                destroyCamera();
                return -1;
            }
            camera_still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];
            preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
            video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];

            encoder_input_port = encoder->input[0];
            encoder_output_port = encoder->output[0];
            preview_input_port = preview_component->input[0];

            if (connectPorts(preview_port, preview_input_port, &preview_connection) != MMAL_SUCCESS)
            {
                cout << "ERROR: Could not connect preview ports!\n";
                return -1;
            }

            if (connectPorts(camera_still_port, encoder_input_port, &encoder_connection) != MMAL_SUCCESS)
            {
                cout << "ERROR: Could not connect encoder ports!\n";
                return -1;
            }

            _isInitialized = true;
            return 0;
        }

        /**
         * Checks if specified port is valid and enabled, then disables it
         *
         * @param port  Pointer the port
         *
         */
        void check_disable_port(MMAL_PORT_T *port)
        {
            if (port && port->is_enabled)
                mmal_port_disable(port);
        }

        void Private_Impl_Still::release()
        {
            if (!_isInitialized)
                return;

            // Disable all our ports that are not handled by connections
            check_disable_port(video_port);
            check_disable_port(encoder_output_port);

            if (preview_connection)
                mmal_connection_destroy(preview_connection);

            if (encoder_connection)
                mmal_connection_destroy(encoder_connection);

            /* Disable components */
            if (encoder)
                mmal_component_disable(encoder);

            if (preview_component)
                mmal_component_disable(preview_component);

            if (camera)
                mmal_component_disable(camera);

            destroyEncoders();

            if (preview_component)
            {
                mmal_component_destroy(preview_component);
                preview_component = NULL;
            }

            //disconnectPortsdisconnectPorts();

            /* Disable components */
            // if (encoder)
            //     mmal_component_disable(encoder);

            // if (preview_component)
            //     mmal_component_disable(preview_component);

            // if (camera)
            //     mmal_component_disable(camera);

            // destroyEncoders();
            destroyCamera();

            _isInitialized = false;
        }

        bool Private_Impl_Still::takePicture(unsigned char *preallocated_data, unsigned int length)
        {
            initialize();
            int ret = 0;
            sem_t mutex;
            sem_init(&mutex, 0, 0);
            RASPICAM_USERDATA *userdata = new RASPICAM_USERDATA();
            userdata->cameraBoard = this;
            userdata->encoderPool = encoder_pool;
            userdata->mutex = &mutex;
            userdata->data = preallocated_data;
            userdata->bufferPosition = 0;
            userdata->offset = 0;
            userdata->startingOffset = 0;
            userdata->length = length;
            userdata->imageCallback = NULL;
            userdata->file_handle = NULL;
            encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)userdata;
            if ((ret = startCapture()) != 0)
            {
                delete userdata;
                return false;
            }
            sem_wait(&mutex);
            sem_destroy(&mutex);
            stopCapture(encoder_output_port);
            delete userdata;

            return true;
        }

        bool Private_Impl_Still::emptyCapture()
        {

            cout << API_NAME << "empty capture !" << endl;
            initialize();
            int ret = 0;
            sem_t mutex;
            sem_init(&mutex, 0, 0);
            RASPICAM_USERDATA *userdata = new RASPICAM_USERDATA();
            userdata->cameraBoard = this;
            userdata->encoderPool = NULL;
            userdata->mutex = &mutex;
            userdata->data = 0;
            userdata->bufferPosition = 0;
            userdata->offset = 0;
            userdata->startingOffset = 0;
            userdata->length = 0;
            userdata->imageCallback = NULL;
            userdata->file_handle = NULL;
            encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)userdata;
            if ((ret = startCapture()) != 0)
            {
                delete userdata;
                return false;
            }
            sem_wait(&mutex);
            sem_destroy(&mutex);
            stopCapture(encoder_output_port);
            delete userdata;
            return true;
        }

        bool Private_Impl_Still::takePicture(const char *filename)
        {

            FILE *output_file = NULL;
            output_file = fopen(filename, "wb");

            if (!output_file)
            {
                // Notify user, carry on but discarding encoded output buffers
                cout << API_NAME << "Error opening output file: " << filename << " No output file will be generated" << endl;
                return false;
            }

            initialize();
            int ret = 0;
            sem_t mutex;
            sem_init(&mutex, 0, 0);
            RASPICAM_USERDATA *userdata = new RASPICAM_USERDATA();
            userdata->cameraBoard = this;
            userdata->encoderPool = encoder_pool;
            userdata->mutex = &mutex;
            userdata->data = 0;
            userdata->bufferPosition = 0;
            userdata->offset = 0;
            userdata->startingOffset = 0;
            userdata->length = 0;
            userdata->imageCallback = NULL;
            userdata->file_handle = output_file;
            encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)userdata;
            if ((ret = startCapture()) != 0)
            {
                delete userdata;
                return false;
            }
            sem_wait(&mutex);
            sem_destroy(&mutex);
            stopCapture(encoder_output_port);
            delete userdata;
            fclose(output_file);

            return true;
        }

        bool Private_Impl_Still::take_picture_in_mem(char **dynamically_allocated_data, size_t *output_size)
        {

            if (!dynamically_allocated_data || !output_size)
            {
                return false;
            }
            //open memory stream
            FILE *memfp = open_memstream(dynamically_allocated_data, output_size);

            if (!memfp)
            {
                // Notify user, carry on but discarding encoded output buffers
                cout << API_NAME << "Error opening memory stream for capture" << endl;
                *output_size = 0;
                return false;
            }

            initialize();
            int ret = 0;
            sem_t mutex;
            sem_init(&mutex, 0, 0);
            RASPICAM_USERDATA *userdata = new RASPICAM_USERDATA();
            userdata->cameraBoard = this;
            userdata->encoderPool = encoder_pool;
            userdata->mutex = &mutex;
            userdata->data = 0;
            userdata->bufferPosition = 0;
            userdata->offset = 0;
            userdata->startingOffset = 0;
            userdata->length = 0;
            userdata->imageCallback = NULL;
            userdata->file_handle = memfp;
            encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)userdata;
            if ((ret = startCapture()) != 0)
            {
                delete userdata;
                return false;
            }
            sem_wait(&mutex);
            sem_destroy(&mutex);
            stopCapture(encoder_output_port);
            delete userdata;
            fclose(memfp);

            return true;
        }

        size_t Private_Impl_Still::getImageBufferSize() const
        {
            if (encoding == raspicam::RASPICAM_ENCODING_BMP)
            {
                return width * height * 3 + 54; //oversize the buffer so to fit BMP images
            }
            return width * height * 3 + 54; //oversize the buffer so to fit BMP images
        }

        void Private_Impl_Still::get_sensor_defaults(int camera_num, char *camera_name, int *width, int *height)
        {
            MMAL_COMPONENT_T *camera_info;
            MMAL_STATUS_T status;

            // Default to the OV5647 setup
            strncpy(camera_name, "OV5647", MMAL_PARAMETER_CAMERA_INFO_MAX_STR_LEN);

            // Try to get the camera name and maximum supported resolution
            status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA_INFO, &camera_info);
            if (status == MMAL_SUCCESS)
            {
                MMAL_PARAMETER_CAMERA_INFO_T param;
                param.hdr.id = MMAL_PARAMETER_CAMERA_INFO;
                param.hdr.size = sizeof(param) - 4; // Deliberately undersize to check firmware version
                status = mmal_port_parameter_get(camera_info->control, &param.hdr);

                if (status != MMAL_SUCCESS)
                {
                    // Running on newer firmware
                    param.hdr.size = sizeof(param);
                    status = mmal_port_parameter_get(camera_info->control, &param.hdr);
                    if (status == MMAL_SUCCESS && param.num_cameras > camera_num)
                    {
                        // Take the parameters from the first camera listed.
                        if (*width == 0)
                            *width = param.cameras[camera_num].max_width;
                        if (*height == 0)
                            *height = param.cameras[camera_num].max_height;
                        strncpy(camera_name, param.cameras[camera_num].camera_name, MMAL_PARAMETER_CAMERA_INFO_MAX_STR_LEN);
                        camera_name[MMAL_PARAMETER_CAMERA_INFO_MAX_STR_LEN - 1] = 0;
                    }
                    else
                        cout << API_NAME << "Cannot read camera info, keeping the defaults for OV5647" << endl;
                }
                else
                {
                    // Older firmware
                    // Nothing to do here, keep the defaults for OV5647
                }

                mmal_component_destroy(camera_info);
            }
            else
            {
                cout << API_NAME << "Failed to create camera_info component" << endl;
            }

            // default to OV5647 if nothing detected..
            if (*width == 0)
                *width = 2592;
            if (*height == 0)
                *height = 1944;
        }

        int Private_Impl_Still::startCapture(imageTakenCallback userCallback, unsigned char *preallocated_data, unsigned int offset, unsigned int length)
        {
            RASPICAM_USERDATA *userdata = new RASPICAM_USERDATA();
            userdata->cameraBoard = this;
            userdata->encoderPool = encoder_pool;
            userdata->mutex = NULL;
            userdata->data = preallocated_data;
            userdata->bufferPosition = 0;
            userdata->offset = offset;
            userdata->startingOffset = offset;
            userdata->length = length;
            userdata->imageCallback = userCallback;
            encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)userdata;
            return startCapture();
        }

        int Private_Impl_Still::startCapture()
        {
            // If the parameters were changed and this function wasn't called, it will be called here
            // However if the parameters weren't changed, the function won't do anything - it will return right away
            commitParameters();

            // //switch to exposure mode off... to freeze the gains
            // MMAL_PARAMETER_EXPOSUREMODE_T exp_mode = {{MMAL_PARAMETER_EXPOSURE_MODE, sizeof(exp_mode)}, MMAL_PARAM_EXPOSUREMODE_OFF};
            // if (mmal_port_parameter_set(camera->control, &exp_mode.hdr) != MMAL_SUCCESS)
            //     cout << API_NAME << ": Failed to set exposure parameter.\n";

            // There is a possibility that shutter needs to be set each loop.
            commitShutterSpeed();

            if (encoder_output_port->is_enabled)
            {
                cout << API_NAME << ": Could not enable encoder output port. Try waiting longer before attempting to take another picture.\n";
                return -1;
            }
            if (encoder_output_port && encoder_output_port->userdata)
            {
                if (mmal_port_enable(encoder_output_port, buffer_callback) != MMAL_SUCCESS)
                {
                    cout << API_NAME << ": Could not enable encoder output port.\n";
                    return -1;
                }
                int num = mmal_queue_length(encoder_pool->queue);
                for (int b = 0; b < num; b++)
                {
                    MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(encoder_pool->queue);

                    if (!buffer)
                        cout << API_NAME << ": Could not get buffer (#" << b << ") from pool queue.\n";

                    if (mmal_port_send_buffer(encoder_output_port, buffer) != MMAL_SUCCESS)
                        cout << API_NAME << ": Could not send a buffer (#" << b << ") to encoder output port.\n";
                }
            }

            if (burst_mode)
            {
                mmal_port_parameter_set_boolean(camera->control, MMAL_PARAMETER_CAMERA_BURST_CAPTURE, 1);
            }
            else
            {
                mmal_port_parameter_set_boolean(camera->control, MMAL_PARAMETER_CAMERA_BURST_CAPTURE, 0);
            }

            if (userControlCallback)
            {
                userControlCallback->CaptureRequested();
            }
            if (mmal_port_parameter_set_boolean(camera_still_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Failed to start capture.\n";
                return -1;
            }
            return 0;
        }

        void Private_Impl_Still::stopCapture(MMAL_PORT_T *port)
        {
            if (!port->is_enabled)
                return;
            if (mmal_port_disable(port))
                delete (RASPICAM_USERDATA *)port->userdata;

            //restore exposure mode
            commitExposure();
        }

        void Private_Impl_Still::setBurstMode(bool mode)
        {
            if (mode != burst_mode)
            {
                burst_mode = mode;
                // if(mode) {
                //     emptyCapture();
                // }
            }
        }

        void Private_Impl_Still::setWidth(unsigned int width)
        {
            this->width = width;
            changedSettings = true;
            changedResolution = true;
        }

        void Private_Impl_Still::setHeight(unsigned int height)
        {
            this->height = height;
            changedSettings = true;
            changedResolution = true;
        }

        void Private_Impl_Still::setCaptureSize(unsigned int width, unsigned int height)
        {
            setWidth(width);
            setHeight(height);
            commitResolution();
            commitParameters();
        }

        void Private_Impl_Still::setBrightness(unsigned int brightness)
        {
            if (brightness > 100)
                brightness = brightness % 100;
            this->brightness = brightness;
            changedSettings = true;
        }

        void Private_Impl_Still::setQuality(unsigned int quality)
        {
            if (quality > 100)
                quality = 100;
            this->quality = quality;
            changedSettings = true;
        }

        void Private_Impl_Still::setShutterSpeed(unsigned int ss)
        {
            this->shutter_speed = ss;
            changedSettings = true;
        }

        void Private_Impl_Still::setMeasuredShutterSpeed(unsigned int ss)
        {
            this->measured_shutter_speed = ss;
        }

        void Private_Impl_Still::setRotation(int rotation)
        {
            while (rotation < 0)
                rotation += 360;
            if (rotation >= 360)
                rotation = rotation % 360;
            this->rotation = rotation;
            changedSettings = true;
        }

        void Private_Impl_Still::setISO(int iso)
        {
            this->iso = iso;
            changedSettings = true;
        }

        void Private_Impl_Still::setSharpness(int sharpness)
        {
            if (sharpness < -100)
                sharpness = -100;
            if (sharpness > 100)
                sharpness = 100;
            this->sharpness = sharpness;
            changedSettings = true;
        }

        void Private_Impl_Still::setContrast(int contrast)
        {
            if (contrast < -100)
                contrast = -100;
            if (contrast > 100)
                contrast = 100;
            this->contrast = contrast;
            changedSettings = true;
        }

        void Private_Impl_Still::setSaturation(int saturation)
        {
            if (saturation < -100)
                saturation = -100;
            if (saturation > 100)
                saturation = 100;
            this->saturation = saturation;
            changedSettings = true;
        }

        void Private_Impl_Still::setEncoding(RASPICAM_ENCODING encoding)
        {
            this->encoding = encoding;
            changedSettings = true;
        }

        void Private_Impl_Still::setExposure(RASPICAM_EXPOSURE exposure)
        {
            this->exposure = exposure;
            changedSettings = true;
        }

        void Private_Impl_Still::setAWB(RASPICAM_AWB awb)
        {
            this->awb = awb;
            changedSettings = true;
        }

        void Private_Impl_Still::setImageEffect(RASPICAM_IMAGE_EFFECT imageEffect)
        {
            this->imageEffect = imageEffect;
            changedSettings = true;
        }

        void Private_Impl_Still::setMetering(RASPICAM_METERING metering)
        {
            this->metering = metering;
            changedSettings = true;
        }

        void Private_Impl_Still::setHorizontalFlip(bool hFlip)
        {
            horizontalFlip = hFlip;
            changedSettings = true;
        }

        void Private_Impl_Still::setVerticalFlip(bool vFlip)
        {
            verticalFlip = vFlip;
            changedSettings = true;
        }

        void Private_Impl_Still::setAnalogGain(float gain)
        {
            analogGain = gain;
            changedSettings = true;
        }

        void Private_Impl_Still::setDigitalGain(float gain)
        {
            digitalGain = gain;
            changedSettings = true;
        }

        void Private_Impl_Still::setAwbRedGain(float gain)
        {
            awbRedGain = gain;
            changedSettings = true;
        }

        void Private_Impl_Still::setAwbBlueGain(float gain)
        {
            awbBlueGain = gain;
            changedSettings = true;
        }

        void Private_Impl_Still::setMeasuredAwbRedGain(float gain)
        {
            measured_awbRedGain = gain;
        }

        void Private_Impl_Still::setMeasuredAwbBlueGain(float gain)
        {
            measured_awbBlueGain = gain;
        }

        bool Private_Impl_Still::getBurstMode()
        {
            return burst_mode;
        }

        unsigned int Private_Impl_Still::getWidth()
        {
            return width;
        }

        unsigned int Private_Impl_Still::getHeight()
        {
            return height;
        }

        unsigned int Private_Impl_Still::getBrightness()
        {
            return brightness;
        }

        unsigned int Private_Impl_Still::getRotation()
        {
            return rotation;
        }

        unsigned int Private_Impl_Still::getQuality()
        {
            return quality;
        }

        unsigned int Private_Impl_Still::getShutterSpeed()
        {
            return shutter_speed;
        }

        unsigned int Private_Impl_Still::getMeasuredShutterSpeed()
        {
            return measured_shutter_speed;
        }

        int Private_Impl_Still::getISO()
        {
            return iso;
        }

        int Private_Impl_Still::getSharpness()
        {
            return sharpness;
        }

        int Private_Impl_Still::getContrast()
        {
            return contrast;
        }

        int Private_Impl_Still::getSaturation()
        {
            return saturation;
        }

        RASPICAM_ENCODING Private_Impl_Still::getEncoding()
        {
            return encoding;
        }

        RASPICAM_EXPOSURE Private_Impl_Still::getExposure()
        {
            return exposure;
        }

        RASPICAM_AWB Private_Impl_Still::getAWB()
        {
            return awb;
        }

        RASPICAM_IMAGE_EFFECT Private_Impl_Still::getImageEffect()
        {
            return imageEffect;
        }

        RASPICAM_METERING Private_Impl_Still::getMetering()
        {
            return metering;
        }

        bool Private_Impl_Still::isHorizontallyFlipped()
        {
            return horizontalFlip;
        }

        bool Private_Impl_Still::isVerticallyFlipped()
        {
            return verticalFlip;
        }

        float Private_Impl_Still::getAnalogGain()
        {
            return analogGain;
        }

        float Private_Impl_Still::getDigitalGain()
        {
            return digitalGain;
        }

        float Private_Impl_Still::getAwbRedGain()
        {
            return awbRedGain;
        }

        float Private_Impl_Still::getAwbBlueGain()
        {
            return awbBlueGain;
        }

          float Private_Impl_Still::getMeasuredAwbRedGain()
        {
            return measured_awbRedGain;
        }

        float Private_Impl_Still::getMeasuredAwbBlueGain()
        {
            return measured_awbBlueGain;
        }

        void Private_Impl_Still::commitBrightness()
        {
            mmal_port_parameter_set_rational(camera->control, MMAL_PARAMETER_BRIGHTNESS, (MMAL_RATIONAL_T){brightness, 100});
        }

        void Private_Impl_Still::commitQuality()
        {
            if (encoder_output_port != NULL)
                mmal_port_parameter_set_uint32(encoder_output_port, MMAL_PARAMETER_JPEG_Q_FACTOR, quality);
        }

        void Private_Impl_Still::commitShutterSpeed()
        {
            if (!camera)
                return;
            mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_SHUTTER_SPEED, shutter_speed);
            // if(shutter_speed != 0){
            //     MMAL_PARAMETER_EXPOSUREMODE_T exp_mode = {{MMAL_PARAMETER_EXPOSURE_MODE, sizeof(exp_mode)}, MMAL_PARAM_EXPOSUREMODE_OFF};
            //     if (mmal_port_parameter_set(camera->control, &exp_mode.hdr) != MMAL_SUCCESS)
            //         cout << API_NAME << ": Failed to set exposure parameter.\n";
            // }
        }

        void Private_Impl_Still::commitRotation()
        {
            int rotation = int(this->rotation / 90) * 90;
            mmal_port_parameter_set_int32(camera->output[0], MMAL_PARAMETER_ROTATION, rotation);
            mmal_port_parameter_set_int32(camera->output[1], MMAL_PARAMETER_ROTATION, rotation);
            mmal_port_parameter_set_int32(camera->output[2], MMAL_PARAMETER_ROTATION, rotation);
        }

        void Private_Impl_Still::commitISO()
        {
            if (mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_ISO, iso) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set ISO parameter.\n";
        }

        void Private_Impl_Still::commitSharpness()
        {
            if (mmal_port_parameter_set_rational(camera->control, MMAL_PARAMETER_SHARPNESS, (MMAL_RATIONAL_T){sharpness, 100}) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set sharpness parameter.\n";
        }

        void Private_Impl_Still::commitContrast()
        {
            if (mmal_port_parameter_set_rational(camera->control, MMAL_PARAMETER_CONTRAST, (MMAL_RATIONAL_T){contrast, 100}) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set contrast parameter.\n";
        }

        void Private_Impl_Still::commitSaturation()
        {
            if (mmal_port_parameter_set_rational(camera->control, MMAL_PARAMETER_SATURATION, (MMAL_RATIONAL_T){saturation, 100}) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set saturation parameter.\n";
        }

        void Private_Impl_Still::commitExposure()
        {
            MMAL_PARAMETER_EXPOSUREMODE_T exp_mode = {{MMAL_PARAMETER_EXPOSURE_MODE, sizeof(exp_mode)}, convertExposure(exposure)};
            if (mmal_port_parameter_set(camera->control, &exp_mode.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set exposure parameter.\n";
        }

        void Private_Impl_Still::commitAWB()
        {
            MMAL_PARAMETER_AWBMODE_T param = {{MMAL_PARAMETER_AWB_MODE, sizeof(param)}, convertAWB(awb)};
            if (mmal_port_parameter_set(camera->control, &param.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set AWB parameter.\n";
        }

        void Private_Impl_Still::commitImageEffect()
        {
            MMAL_PARAMETER_IMAGEFX_T imgFX = {{MMAL_PARAMETER_IMAGE_EFFECT, sizeof(imgFX)}, convertImageEffect(imageEffect)};
            if (mmal_port_parameter_set(camera->control, &imgFX.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set image effect parameter.\n";
        }

        void Private_Impl_Still::commitMetering()
        {
            MMAL_PARAMETER_EXPOSUREMETERINGMODE_T meter_mode = {{MMAL_PARAMETER_EXP_METERING_MODE, sizeof(meter_mode)}, convertMetering(metering)};
            if (mmal_port_parameter_set(camera->control, &meter_mode.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to set metering parameter.\n";
        }

        void Private_Impl_Still::commitFlips()
        {
            MMAL_PARAMETER_MIRROR_T mirror = {{MMAL_PARAMETER_MIRROR, sizeof(MMAL_PARAMETER_MIRROR_T)}, MMAL_PARAM_MIRROR_NONE};
            if (horizontalFlip && verticalFlip)
                mirror.value = MMAL_PARAM_MIRROR_BOTH;
            else if (horizontalFlip)
                mirror.value = MMAL_PARAM_MIRROR_HORIZONTAL;
            else if (verticalFlip)
                mirror.value = MMAL_PARAM_MIRROR_VERTICAL;
            if (mmal_port_parameter_set(camera->output[0], &mirror.hdr) != MMAL_SUCCESS ||
                mmal_port_parameter_set(camera->output[1], &mirror.hdr) != MMAL_SUCCESS ||
                mmal_port_parameter_set(camera->output[2], &mirror.hdr))
                cout << API_NAME << ": Failed to set horizontal/vertical flip parameter.\n";
        }

        void Private_Impl_Still::commitResolution()
        {
            cout << API_NAME << ": Changing resolution to: " << width << "x" << height << endl;
            disableCamera();

            MMAL_PARAMETER_CAMERA_CONFIG_T camConfig = {
                {MMAL_PARAMETER_CAMERA_CONFIG, sizeof(camConfig)},
                width,                              // max_stills_w
                height,                             // max_stills_h
                0,                                  // stills_yuv422
                1,                                  // one_shot_stills
                1024,                               // max_preview_video_w
                768,                                // max_preview_video_h
                3,                                  // num_preview_video_frames
                0,                                  // stills_capture_circular_buffer_height
                0,                                  // fast_preview_resume
                MMAL_PARAM_TIMESTAMP_MODE_RESET_STC // use_stc_timestamp
            };

            if (mmal_port_parameter_set(camera->control, &camConfig.hdr) != MMAL_SUCCESS)
                cout << API_NAME << ": Failed to change resolution...\n";

            setPortFormats();
            setFPSRange();
            enableCamera();
            changedResolution = false;
        }

        void Private_Impl_Still::commitGains()
        {
            MMAL_RATIONAL_T rational = {0, 65536};
            MMAL_STATUS_T status;

            if (!camera)
                return;

            rational.num = (unsigned int)(analogGain * 65536);
            status = mmal_port_parameter_set_rational(camera->control, MMAL_PARAMETER_ANALOG_GAIN, rational);
            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Failed to set analog gain.\n";
            }

            rational.num = (unsigned int)(digitalGain * 65536);
            status = mmal_port_parameter_set_rational(camera->control, MMAL_PARAMETER_DIGITAL_GAIN, rational);
            if (status != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Failed to set digital gain.\n";
            }
        }

        void Private_Impl_Still::commitAwbGains()
        {
            //ignore if one of the gain is auto
            //cout << API_NAME << ": setting awb gains " << awbRedGain <<","<< awbBlueGain << "\n";
            if (awbBlueGain * awbRedGain == 0.0)
            {
                return;
            }

            MMAL_PARAMETER_AWB_GAINS_T param = {{MMAL_PARAMETER_CUSTOM_AWB_GAINS, sizeof(param)}, {0, 0}, {0, 0}};

            if (!camera)
                return;

            cout << API_NAME << ": setting awb gains " << awbRedGain << "," << awbBlueGain << "\n";
            param.r_gain.num = (unsigned int)(awbRedGain * 65536);
            param.b_gain.num = (unsigned int)(awbBlueGain * 65536);
            param.r_gain.den = param.b_gain.den = 65536;
            if (mmal_port_parameter_set(camera->control, &param.hdr) != MMAL_SUCCESS)
            {
                cout << API_NAME << ": Failed to set custom awb gains.\n";
            }
        }

        MMAL_FOURCC_T Private_Impl_Still::convertEncoding(RASPICAM_ENCODING encoding)
        {
            switch (encoding)
            {
            case RASPICAM_ENCODING_JPEG:
                return MMAL_ENCODING_JPEG;
            case RASPICAM_ENCODING_BMP:
                return MMAL_ENCODING_BMP;
            case RASPICAM_ENCODING_GIF:
                return MMAL_ENCODING_GIF;
            case RASPICAM_ENCODING_PNG:
                return MMAL_ENCODING_PNG;
            case RASPICAM_ENCODING_RGB:
                return MMAL_ENCODING_BMP;
            default:
                return -1;
            }
        }

        MMAL_PARAM_EXPOSUREMETERINGMODE_T Private_Impl_Still::convertMetering(RASPICAM_METERING metering)
        {
            switch (metering)
            {
            case RASPICAM_METERING_AVERAGE:
                return MMAL_PARAM_EXPOSUREMETERINGMODE_AVERAGE;
            case RASPICAM_METERING_SPOT:
                return MMAL_PARAM_EXPOSUREMETERINGMODE_SPOT;
            case RASPICAM_METERING_BACKLIT:
                return MMAL_PARAM_EXPOSUREMETERINGMODE_BACKLIT;
            case RASPICAM_METERING_MATRIX:
                return MMAL_PARAM_EXPOSUREMETERINGMODE_MATRIX;
            default:
                return MMAL_PARAM_EXPOSUREMETERINGMODE_AVERAGE;
            }
        }

        MMAL_PARAM_EXPOSUREMODE_T Private_Impl_Still::convertExposure(RASPICAM_EXPOSURE exposure)
        {
            switch (exposure)
            {
            case RASPICAM_EXPOSURE_OFF:
                return MMAL_PARAM_EXPOSUREMODE_OFF;
            case RASPICAM_EXPOSURE_AUTO:
                return MMAL_PARAM_EXPOSUREMODE_AUTO;
            case RASPICAM_EXPOSURE_NIGHT:
                return MMAL_PARAM_EXPOSUREMODE_NIGHT;
            case RASPICAM_EXPOSURE_NIGHTPREVIEW:
                return MMAL_PARAM_EXPOSUREMODE_NIGHTPREVIEW;
            case RASPICAM_EXPOSURE_BACKLIGHT:
                return MMAL_PARAM_EXPOSUREMODE_BACKLIGHT;
            case RASPICAM_EXPOSURE_SPOTLIGHT:
                return MMAL_PARAM_EXPOSUREMODE_SPOTLIGHT;
            case RASPICAM_EXPOSURE_SPORTS:
                return MMAL_PARAM_EXPOSUREMODE_SPORTS;
            case RASPICAM_EXPOSURE_SNOW:
                return MMAL_PARAM_EXPOSUREMODE_SNOW;
            case RASPICAM_EXPOSURE_BEACH:
                return MMAL_PARAM_EXPOSUREMODE_BEACH;
            case RASPICAM_EXPOSURE_VERYLONG:
                return MMAL_PARAM_EXPOSUREMODE_VERYLONG;
            case RASPICAM_EXPOSURE_FIXEDFPS:
                return MMAL_PARAM_EXPOSUREMODE_FIXEDFPS;
            case RASPICAM_EXPOSURE_ANTISHAKE:
                return MMAL_PARAM_EXPOSUREMODE_ANTISHAKE;
            case RASPICAM_EXPOSURE_FIREWORKS:
                return MMAL_PARAM_EXPOSUREMODE_FIREWORKS;
            default:
                return MMAL_PARAM_EXPOSUREMODE_AUTO;
            }
        }

        MMAL_PARAM_AWBMODE_T Private_Impl_Still::convertAWB(RASPICAM_AWB awb)
        {
            switch (awb)
            {
            case RASPICAM_AWB_OFF:
                return MMAL_PARAM_AWBMODE_OFF;
            case RASPICAM_AWB_AUTO:
                return MMAL_PARAM_AWBMODE_AUTO;
            case RASPICAM_AWB_SUNLIGHT:
                return MMAL_PARAM_AWBMODE_SUNLIGHT;
            case RASPICAM_AWB_CLOUDY:
                return MMAL_PARAM_AWBMODE_CLOUDY;
            case RASPICAM_AWB_SHADE:
                return MMAL_PARAM_AWBMODE_SHADE;
            case RASPICAM_AWB_TUNGSTEN:
                return MMAL_PARAM_AWBMODE_TUNGSTEN;
            case RASPICAM_AWB_FLUORESCENT:
                return MMAL_PARAM_AWBMODE_FLUORESCENT;
            case RASPICAM_AWB_INCANDESCENT:
                return MMAL_PARAM_AWBMODE_INCANDESCENT;
            case RASPICAM_AWB_FLASH:
                return MMAL_PARAM_AWBMODE_FLASH;
            case RASPICAM_AWB_HORIZON:
                return MMAL_PARAM_AWBMODE_HORIZON;
            default:
                return MMAL_PARAM_AWBMODE_AUTO;
            }
        }

        MMAL_PARAM_IMAGEFX_T Private_Impl_Still::convertImageEffect(RASPICAM_IMAGE_EFFECT imageEffect)
        {
            switch (imageEffect)
            {
            case RASPICAM_IMAGE_EFFECT_NONE:
                return MMAL_PARAM_IMAGEFX_NONE;
            case RASPICAM_IMAGE_EFFECT_NEGATIVE:
                return MMAL_PARAM_IMAGEFX_NEGATIVE;
            case RASPICAM_IMAGE_EFFECT_SOLARIZE:
                return MMAL_PARAM_IMAGEFX_SOLARIZE;
            case RASPICAM_IMAGE_EFFECT_SKETCH:
                return MMAL_PARAM_IMAGEFX_SKETCH;
            case RASPICAM_IMAGE_EFFECT_DENOISE:
                return MMAL_PARAM_IMAGEFX_DENOISE;
            case RASPICAM_IMAGE_EFFECT_EMBOSS:
                return MMAL_PARAM_IMAGEFX_EMBOSS;
            case RASPICAM_IMAGE_EFFECT_OILPAINT:
                return MMAL_PARAM_IMAGEFX_OILPAINT;
            case RASPICAM_IMAGE_EFFECT_HATCH:
                return MMAL_PARAM_IMAGEFX_HATCH;
            case RASPICAM_IMAGE_EFFECT_GPEN:
                return MMAL_PARAM_IMAGEFX_GPEN;
            case RASPICAM_IMAGE_EFFECT_PASTEL:
                return MMAL_PARAM_IMAGEFX_PASTEL;
            case RASPICAM_IMAGE_EFFECT_WATERCOLOR:
                return MMAL_PARAM_IMAGEFX_WATERCOLOUR;
            case RASPICAM_IMAGE_EFFECT_FILM:
                return MMAL_PARAM_IMAGEFX_FILM;
            case RASPICAM_IMAGE_EFFECT_BLUR:
                return MMAL_PARAM_IMAGEFX_BLUR;
            case RASPICAM_IMAGE_EFFECT_SATURATION:
                return MMAL_PARAM_IMAGEFX_SATURATION;
            case RASPICAM_IMAGE_EFFECT_COLORSWAP:
                return MMAL_PARAM_IMAGEFX_COLOURSWAP;
            case RASPICAM_IMAGE_EFFECT_WASHEDOUT:
                return MMAL_PARAM_IMAGEFX_WASHEDOUT;
            case RASPICAM_IMAGE_EFFECT_POSTERISE:
                return MMAL_PARAM_IMAGEFX_POSTERISE;
            case RASPICAM_IMAGE_EFFECT_COLORPOINT:
                return MMAL_PARAM_IMAGEFX_COLOURPOINT;
            case RASPICAM_IMAGE_EFFECT_COLORBALANCE:
                return MMAL_PARAM_IMAGEFX_COLOURBALANCE;
            case RASPICAM_IMAGE_EFFECT_CARTOON:
                return MMAL_PARAM_IMAGEFX_CARTOON;
            }
        }

        //Returns an id of the camera. We assume the camera id is the one of the raspberry
        //the id is obtained using raspberry serial number obtained in /proc/cpuinfo
        string Private_Impl_Still::getId() const
        {
            char serial[1024];
            serial[0] = '\0';
            ifstream file("/proc/cpuinfo");
            if (!file)
            {
                cerr << __FILE__ << " " << __LINE__ << ":" << __func__ << "Could not read /proc/cpuinfo" << endl;
                return serial;
            }
            //read lines until find serial
            bool found = false;
            while (!file.eof() && !found)
            {
                char line[1024];
                file.getline(line, 1024);
                string str(line);
                char aux[100];

                if (str.find("Serial") != string::npos)
                {
                    if (sscanf(line, "%s : %s", aux, serial) != 2)
                    {
                        cerr << __FILE__ << " " << __LINE__ << ":" << __func__ << "Error parsing /proc/cpuinfo" << endl;
                    }
                    else
                        found = true;
                }
            };
            return serial;
        }
    } // namespace _private

} // namespace raspicam
