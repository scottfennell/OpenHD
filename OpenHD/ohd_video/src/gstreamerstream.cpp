
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <regex>

#include <gst/gst.h>

#include "openhd-log.hpp"
#include "OHDGstHelper.hpp"
#include "gstreamerstream.h"

GStreamerStream::GStreamerStream(PlatformType platform,
								 Camera &camera,
								 uint16_t video_udp_port)
	: CameraStream(platform, camera, video_udp_port) {
  std::cout << "GStreamerStream::GStreamerStream()\n";
  // Since the dummy camera is SW, we generally cannot do more than 640x480@30 anyways.
  // (640x48@30 might already be too much on embedded devices).
  if (camera.type == CameraTypeDummy &&
      camera.settings.userSelectedVideoFormat.width > 640 ||
      camera.settings.userSelectedVideoFormat.height > 480 ||
      camera.settings.userSelectedVideoFormat.framerate > 30) {
    std::cout<<"Warning- Dummy camera is done in sw, high resolution/framerate might not work\n";
	std::cout<<"Configured dummy for:" << m_camera.settings.userSelectedVideoFormat.toString() << "\n";
  }
  // sanity checks
  if(!check_bitrate_sane(m_camera.settings.bitrateKBits)){
    std::cerr << "manually fixing insane camera bitrate" << m_camera.settings.bitrateKBits << "\n";
    m_camera.settings.bitrateKBits=DEFAULT_BITRATE_KBITS;
  }
  assert(m_camera.settings.userSelectedVideoFormat.isValid());
}

void GStreamerStream::setup() {
  std::cout << "GStreamerStream::setup()" << std::endl;
  OHDGstHelper::initGstreamerOrThrow();
  std::cout << "Creating GStreamer pipeline" << std::endl;
  m_pipeline.str("");
  m_pipeline.clear();
  switch (m_camera.type) {
	case CameraTypeRaspberryPiCSI: {
	  setup_raspberrypi_csi();
	  break;
	}
	case CameraTypeJetsonCSI: {
	  setup_jetson_csi();
	  break;
	}
	case CameraTypeUVC: {
	  setup_usb_uvc();
	  break;
	}
	case CameraTypeUVCH264: {
	  setup_usb_uvch264();
	  break;
	}
	case CameraTypeIP: {
	  setup_ip_camera();
	  break;
	}
	case CameraTypeDummy: {
	  m_pipeline << OHDGstHelper::createDummyStream(m_camera.settings.userSelectedVideoFormat);
	  break;
	}
	case CameraTypeRaspberryPiVEYE:
	case CameraTypeRockchipCSI:
	  std::cerr << "Veye and rockchip are unsupported at the time\n";
	  return;
	case CameraTypeUnknown: {
	  std::cerr << "Unknown camera type" << std::endl;
	  return;
	}
  }
  // quick check,here the pipeline should end with a "! ";
  if(!OHDUtil::endsWith(m_pipeline.str(),"! ")){
	std::cerr<<"Probably ill-formatted pipeline:"<<m_pipeline.str()<<"\n";
  }
  // TODO: ground recording is not working yet, since we cannot properly close the file at the time.
  m_camera.settings.enableAirRecordingToFile= false;
  // for lower latency we only add the tee command at the right place if recording is enabled.
  if(m_camera.settings.enableAirRecordingToFile){
	std::cout<<"Air recording active\n";
	m_pipeline<<"tee name=t ! queue ! ";
  }
  // After we've written the parts for the different camera implementation(s) we just need to append the rtp part and the udp out
  // add rtp part
  m_pipeline << OHDGstHelper::createRtpForVideoCodec(m_camera.settings.userSelectedVideoFormat.videoCodec);
  // Allows users to fully write a manual pipeline, this must be used carefully.
  if (!m_camera.settings.manual_pipeline.empty()) {
	m_pipeline.str("");
	m_pipeline << m_camera.settings.manual_pipeline;
  }
  // add udp out part
  m_pipeline << OHDGstHelper::createOutputUdpLocalhost(m_video_udp_port);
  if(m_camera.settings.enableAirRecordingToFile){
	m_pipeline<<OHDGstHelper::createRecordingForVideoCodec(m_camera.settings.userSelectedVideoFormat.videoCodec);
  }
  std::cout << "Starting pipeline:" << m_pipeline.str() << std::endl;
  GError *error = nullptr;
  gst_pipeline = gst_parse_launch(m_pipeline.str().c_str(), &error);
  if (error) {
	std::cerr << "Failed to create pipeline: " << error->message << std::endl;
	return;
  }
}

void GStreamerStream::setup_raspberrypi_csi() {
  std::cout << "Setting up Raspberry Pi CSI camera" << std::endl;
  // similar to jetson, for now we assume there is only one CSI camera connected.
  m_pipeline<< OHDGstHelper::createRpicamsrcStream(-1, m_camera.settings.bitrateKBits, m_camera.settings.userSelectedVideoFormat);
}

void GStreamerStream::setup_jetson_csi() {
  std::cout << "Setting up Jetson CSI camera" << std::endl;
  // Well, i fixed the bug in the detection, with v4l2_open.
  // But still, /dev/video1 can be camera index 0 on jetson.
  // Therefore, for now, we just default to no camera index rn and let nvarguscamerasrc figure out the camera index.
  // This will work as long as there is no more than 1 CSI camera.
  m_pipeline << OHDGstHelper::createJetsonStream(-1, m_camera.settings.bitrateKBits, m_camera.settings.userSelectedVideoFormat);
}

void GStreamerStream::setup_usb_uvc() {
  std::cout << "Setting up usb UVC camera Name:" << m_camera.name << " type:" << m_camera.type << std::endl;
  // First we try and start a hw encoded path, where v4l2src directly provides encoded video buffers
  for (const auto &endpoint: m_camera.endpoints) {
	if (m_camera.settings.userSelectedVideoFormat.videoCodec == VideoCodecH264 && endpoint.support_h264) {
	  std::cerr << "h264" << std::endl;
	  const auto device_node = endpoint.device_node;
	  m_pipeline << OHDGstHelper::createV4l2SrcAlreadyEncodedStream(device_node, m_camera.settings.userSelectedVideoFormat);
	  return;
	}
	if (m_camera.settings.userSelectedVideoFormat.videoCodec == VideoCodecMJPEG && endpoint.support_mjpeg) {
	  std::cerr << "MJPEG" << std::endl;
	  const auto device_node = endpoint.device_node;
	  m_pipeline << OHDGstHelper::createV4l2SrcAlreadyEncodedStream(device_node, m_camera.settings.userSelectedVideoFormat);
	  return;
	}
  }
  // If we land here, we need to do SW encoding, the v4l2src can only do raw video formats like YUV
  for (const auto &endpoint: m_camera.endpoints) {
	std::cout << "empty" << std::endl;
	if (endpoint.support_raw) {
	  const auto device_node = endpoint.device_node;
	  m_pipeline << OHDGstHelper::createV4l2SrcRawAndSwEncodeStream(device_node,
																	m_camera.settings.userSelectedVideoFormat.videoCodec,
																	m_camera.settings.bitrateKBits);
	  return;
	}
  }
  // If we land here, we couldn't create a stream for this camera.
  std::cerr << "Setup USB UVC failed\n";
}

void GStreamerStream::setup_usb_uvch264() {
  std::cout << "Setting up UVC H264 camera" << std::endl;
  const auto endpoint = m_camera.endpoints.front();
  // uvch265 cameras don't seem to exist, codec setting is ignored
  m_pipeline << OHDGstHelper::createUVCH264Stream(endpoint.device_node,
												  m_camera.settings.bitrateKBits,
												  m_camera.settings.userSelectedVideoFormat);
}

void GStreamerStream::setup_ip_camera() {
  std::cout << "Setting up IP camera" << std::endl;
  if (m_camera.settings.url.empty()) {
	m_camera.settings.url = "rtsp://192.168.0.10:554/user=admin&password=&channel=1&stream=0.sdp";
  }
  m_pipeline << OHDGstHelper::createIpCameraStream(m_camera.settings.url);
}

std::string GStreamerStream::createDebug()const {
  std::stringstream ss;
  GstState state;
  GstState pending;
  auto returnValue = gst_element_get_state(gst_pipeline, &state, &pending, 1000000000);
  ss << "GStreamerStream for camera:"<<m_camera.debugName()<<" State:"<< returnValue << "." << state << "." << pending << ".";
  return ss.str();
}

void GStreamerStream::start() {
  std::cout << "GStreamerStream::start()" << std::endl;
  gst_element_set_state(gst_pipeline, GST_STATE_PLAYING);
  GstState state;
  GstState pending;
  auto returnValue = gst_element_get_state(gst_pipeline, &state, &pending, 1000000000);
  std::cout << "Gst state:" << returnValue << "." << state << "." << pending << "." << std::endl;
}

void GStreamerStream::stop() {
  std::cout << "GStreamerStream::stop()" << std::endl;
  gst_element_set_state(gst_pipeline, GST_STATE_PAUSED);
}

bool GStreamerStream::supports_bitrate() {
  std::cout << "GStreamerStream::supports_bitrate()" << std::endl;
  return false;
}

void GStreamerStream::set_bitrate(int bitrate) {
  std::cout << "Unmplemented GStreamerStream::set_bitrate(" << bitrate << ")" << std::endl;
}

bool GStreamerStream::supports_cbr() {
  std::cout << "GStreamerStream::supports_cbr()" << std::endl;
  return false;
}

void GStreamerStream::set_cbr(bool enable) {
  std::cout << "Unsupported GStreamerStream::set_cbr(" << enable << ")" << std::endl;
}

VideoFormat GStreamerStream::get_format() {
  std::cout << "GStreamerStream::get_format()" << std::endl;
  return m_camera.settings.userSelectedVideoFormat;
}

void GStreamerStream::set_format(VideoFormat videoFormat) {
  std::stringstream ss;
  ss<< "GStreamerStream::set_format(" << videoFormat.toString() << ")" << std::endl;
  ohd_log(STATUS_LEVEL_INFO,ss.str());
  m_camera.settings.userSelectedVideoFormat = videoFormat;
  restart_after_new_setting();
}

void GStreamerStream::restartIfStopped() {
  GstState state;
  GstState pending;
  auto returnValue = gst_element_get_state(gst_pipeline, &state, &pending, 1000000000);
  if (returnValue == 0) {
	std::cerr<<"Panic gstreamer pipeline state is not running, restarting camera stream for camera:"<<m_camera.index<<"\n";
	stop();
	sleep(3);
	start();
  }
}

// Restart after a new settings value has been applied
void GStreamerStream::restart_after_new_setting() {
  stop();
  setup();
  start();
}





