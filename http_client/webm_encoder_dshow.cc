// Copyright (c) 2011 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#include "http_client_base.h"
#include "webm_encoder.h"
#include "webm_encoder_dshow.h"

#include <cstdio>
#include <sstream>

#include <vfwmsgs.h>

#include "debug_util.h"
#include "webmdshow/common/hrtext.hpp"
#include "webmdshow/IDL/vp8encoderidl.h"
#include "webmdshow/IDL/webmmuxidl.h"

namespace WebmLive {

const wchar_t* kVideoSourceName = L"VideoSource";
const wchar_t* kAudioSourceName = L"AudioSource";
const wchar_t* kVpxEncoderName =  L"VP8Encoder";
const wchar_t* kVorbisEncoderName = L"VorbisEncoder";
const int kVpxEncoderBitrate = 500;

WebmEncoderImpl::WebmEncoderImpl()
{
  CoInitialize(NULL);
}

WebmEncoderImpl::~WebmEncoderImpl()
{
  CoUninitialize();
}

int WebmEncoderImpl::Init(std::wstring out_file_name)
{
  int status = CreateGraph();
  if (status) {
    DBGLOG("CreateGraphInterfaces failed: " << status);
    return WebmEncoder::kInitFailed;
  }
  std::wstring video_src;
  status = CreateVideoSource(video_src);
  if (status) {
    DBGLOG("CreateVideoSource failed: " << status);
    return WebmEncoder::kNoVideoSource;
  }
  status = CreateVpxEncoder();
  if (status) {
    DBGLOG("CreateVpxEncoder failed: " << status);
    return WebmEncoder::kVideoEncoderError;
  }
  status = ConnectVideoSourceToVpxEncoder();
  if (status) {
    DBGLOG("ConnectVideoSourceToVpxEncoder failed: " << status);
    return WebmEncoder::kVideoEncoderError;
  }
  std::wstring audio_src;
  status = CreateAudioSource(audio_src);
  if (status) {
    DBGLOG("CreateAudioSource failed: " << status);
    return WebmEncoder::kNoAudioSource;
  }
  return kSuccess;
}

int WebmEncoderImpl::Run()
{
  return WebmEncoder::kRunFailed;
}

int WebmEncoderImpl::Stop()
{
  return kSuccess;
}

int WebmEncoderImpl::CreateGraph()
{
  HRESULT hr = graph_builder_.CreateInstance(CLSID_FilterGraph);
  if (FAILED(hr)) {
    DBGLOG("ERROR: graph builder creation failed." << HRLOG(hr));
    return kCannotCreateGraph;
  }
  hr = capture_graph_builder_.CreateInstance(CLSID_CaptureGraphBuilder2);
  if (FAILED(hr)) {
    DBGLOG("ERROR: capture graph builder creation failed." << HRLOG(hr));
    return kCannotCreateGraph;
  }
  hr = capture_graph_builder_->SetFiltergraph(graph_builder_);
  if (FAILED(hr)) {
    DBGLOG("ERROR: could not set capture builder graph." << HRLOG(hr));
    return kCannotCreateGraph;
  }
  return kSuccess;
}

int WebmEncoderImpl::CreateVideoSource(std::wstring video_src)
{
  if (!video_src.empty()) {
    DBGLOG("ERROR: specifying video source externally is not implemented.");
    return WebmEncoder::kNotImplemented;
  }
  CaptureSourceLoader loader;
  int status = loader.Init(CLSID_VideoInputDeviceCategory);
  if (status) {
    DBGLOG("ERROR: no video source!");
    return WebmEncoder::kNoVideoSource;
  }
  for (int i = 0; i < loader.GetNumSources(); ++i) {
    DBGLOG("[" << i+1 << "] " << loader.GetSourceName(i).c_str());
  }
  // TODO(tomfinegan): Add device selection.
  // For now, use the first device found.
  video_source_ = loader.GetSource(0);
  if (!video_source_) {
    DBGLOG("ERROR: cannot create video source!");
    return WebmEncoder::kNoVideoSource;
  }
  HRESULT hr = graph_builder_->AddFilter(video_source_, kVideoSourceName);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot add video source to graph." << HRLOG(hr));
    return kCannotAddFilter;
  }
  // TODO(tomfinegan): set video format instead of hoping for sane defaults.
  return kSuccess;
}

int WebmEncoderImpl::CreateVpxEncoder()
{
  HRESULT hr = vpx_encoder_.CreateInstance(CLSID_VP8Encoder);
  if (FAILED(hr)) {
    DBGLOG("ERROR: VP8 encoder creation failed." << HRLOG(hr));
    return kCannotCreateVpxEncoder;
  }
  hr = graph_builder_->AddFilter(vpx_encoder_, kVpxEncoderName);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot add VP8 encoder to graph." << HRLOG(hr));
    return kCannotAddFilter;
  }
  _COM_SMARTPTR_TYPEDEF(IVP8Encoder, __uuidof(IVP8Encoder));
  IVP8EncoderPtr vp8_config(vpx_encoder_);
  if (!vp8_config) {
    DBGLOG("ERROR: cannot create VP8 encoder interface.");
    return kCannotConfigureVpxEncoder;
  }
  // TODO(tomfinegan): Obtain VP8 encoder settings from user.
  // Set minimal defaults for a live encode...
  hr = vp8_config->SetDeadline(kDeadlineRealtime);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot set VP8 encoder deadline." << HRLOG(hr));
    return kVpxConfigureError;
  }
  hr = vp8_config->SetEndUsage(kEndUsageCBR);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot set VP8 encoder bitrate mode." << HRLOG(hr));
    return kVpxConfigureError;
  }
  hr = vp8_config->SetTargetBitrate(kVpxEncoderBitrate);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot set VP8 encoder bitrate." << HRLOG(hr));
    return kVpxConfigureError;
  }
  return kSuccess;
}

int WebmEncoderImpl::ConnectVideoSourceToVpxEncoder()
{
  PinFinder pin_finder;
  int status = pin_finder.Init(video_source_);
  if (status) {
    DBGLOG("ERROR: cannot look for pins on video source!");
    return kVideoConnectError;
  }
  IPinPtr video_src_pin = pin_finder.FindVideoOutputPin(0);
  if (!video_src_pin) {
    DBGLOG("ERROR: cannot find output pin on video source!");
    return kVideoConnectError;
  }
  status = pin_finder.Init(vpx_encoder_);
  if (status) {
    DBGLOG("ERROR: cannot look for pins on video source!");
    return kVideoConnectError;
  }
  IPinPtr vpx_input_pin = pin_finder.FindVideoInputPin(0);
  if (!vpx_input_pin) {
    DBGLOG("ERROR: cannot find video input pin on VP8 encoder!");
    return kVideoConnectError;
  }
  // TODO(tomfinegan): Add WebM Color Conversion filter when |ConnectDirect|
  //                   fails here.
  HRESULT hr = graph_builder_->ConnectDirect(video_src_pin, vpx_input_pin,
                                             NULL);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot connect video source to VP8 encoder." << HRLOG(hr));
    return kVideoConnectError;
  }
  return kSuccess;
}

int WebmEncoderImpl::CreateAudioSource(std::wstring audio_src)
{
  if (!audio_src.empty()) {
    DBGLOG("ERROR: specifying audio source externally is not implemented.");
    return WebmEncoder::kNotImplemented;
  }
  // Check for an audio pin on the video source.
  // TODO(tomfinegan): We assume that the user wants to use the audio feed
  //                   exposed by the video capture source.  This behavior
  //                   should be configurable.
  PinFinder pin_finder;
  int status = pin_finder.Init(video_source_);
  if (status) {
    DBGLOG("ERROR: cannot check video source for audio pins!");
    return WebmEncoder::kInitFailed;
  }
  if (pin_finder.FindAudioOutputPin(0)) {
    // Use the video source filter audio output pin.
    DBGLOG("Using video source filter audio output pin.");
    audio_source_ = video_source_;
  } else {
    // The video source doesn't have an audio output pin. Find an audio
    // capture source.
    CaptureSourceLoader loader;
    status = loader.Init(CLSID_AudioInputDeviceCategory);
    if (status) {
      DBGLOG("ERROR: no audio source!");
      return WebmEncoder::kNoAudioSource;
    }
    for (int i = 0; i < loader.GetNumSources(); ++i) {
      DBGLOG("[" << i+1 << "] " << loader.GetSourceName(i).c_str());
    }
    // TODO(tomfinegan): Add device selection.
    // For now, use the first device found.
    audio_source_ = loader.GetSource(0);
    if (!audio_source_) {
      DBGLOG("ERROR: cannot create audio source!");
      return WebmEncoder::kNoAudioSource;
    }
    HRESULT hr = graph_builder_->AddFilter(audio_source_, kAudioSourceName);
    if (FAILED(hr)) {
      DBGLOG("ERROR: cannot add audio source to graph." << HRLOG(hr));
      return kCannotAddFilter;
    }
  }
  // TODO(tomfinegan): set audio format instead of hoping for sane defaults.
  return kSuccess;
}

void WebmEncoderImpl::WebmEncoderThread()
{
}

CaptureSourceLoader::CaptureSourceLoader()
    : source_type_(GUID_NULL)
{
}

CaptureSourceLoader::~CaptureSourceLoader()
{
}

int CaptureSourceLoader::Init(CLSID source_type)
{
  if (source_type != CLSID_AudioInputDeviceCategory &&
      source_type != CLSID_VideoInputDeviceCategory) {
    DBGLOG("ERROR: unknown device category!");
    return WebmEncoder::kInvalidArg;
  }
  source_type_ = source_type;
  return FindAllSources();
}

int CaptureSourceLoader::FindAllSources()
{
  ICreateDevEnumPtr sys_enum;
  HRESULT hr = sys_enum.CreateInstance(CLSID_SystemDeviceEnum);
  if (FAILED(hr)) {
    DBGLOG("ERROR: source enumerator creation failed." << HRLOG(hr));
    return kNoDeviceFound;
  }
  const DWORD kNoEnumFlags = 0;
  hr = sys_enum->CreateClassEnumerator(source_type_, &source_enum_,
                                       kNoEnumFlags);
  if (FAILED(hr) || hr == S_FALSE) {
    DBGLOG("ERROR: moniker creation failed (no devices)." << HRLOG(hr));
    return kNoDeviceFound;
  }
  int i = 0;
  for (;; ++i) {
    IMonikerPtr source_moniker;
    hr = source_enum_->Next(1, &source_moniker, NULL);
    if (FAILED(hr) || hr == S_FALSE || !source_moniker) {
      DBGLOG("Done enumerating sources, found " << i << ".");
      break;
    }
    IPropertyBagPtr props;
    hr = source_moniker->BindToStorage(0, 0, IID_IPropertyBag,
                                       reinterpret_cast<void**>(&props));
    if (FAILED(hr) || hr == S_FALSE) {
      DBGLOG("source=" << i << " has no property bag, skipping.");
      continue;
    }
    const wchar_t* const kFriendlyName = L"FriendlyName";
    std::wstring name = GetStringProperty(props, kFriendlyName);
    if (name.empty()) {
      DBGLOG("source=" << i << " has no " << kFriendlyName << ", skipping.");
      continue;
    }
    DBGLOG("source=" << i << " name=" << name.c_str());
    sources_[i] = name;
  }
  if (sources_.size() == 0) {
    DBGLOG("No devices found!");
    return kNoDeviceFound;
  }
  return kSuccess;
}

IBaseFilterPtr CaptureSourceLoader::GetSource(int index)
{
  if (static_cast<size_t>(index) >= sources_.size()) {
    DBGLOG("ERROR: " << index << " is not a valid source index");
    return NULL;
  }
  HRESULT hr = source_enum_->Reset();
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot reset source enumerator!" << HRLOG(hr));
    return NULL;
  }
  IMonikerPtr source_moniker;
  for (int i = 0; i <= index; ++i) {
    hr = source_enum_->Next(1, &source_moniker, NULL);
    if (FAILED(hr) || hr == S_FALSE || !source_moniker) {
      DBGLOG("ERROR: ran out of devices before reaching requested index!");
      return NULL;
    }
  }
  IBaseFilterPtr filter = NULL;
  hr = source_moniker->BindToObject(NULL, NULL, IID_IBaseFilter,
                                    reinterpret_cast<void**>(&filter));
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot bind filter!" << HRLOG(hr));
  }
  return filter;
}

std::wstring CaptureSourceLoader::GetStringProperty(IPropertyBagPtr &prop_bag,
                                                     std::wstring prop_name)
{
  VARIANT var;
  VariantInit(&var);
  HRESULT hr = prop_bag->Read(prop_name.c_str(), &var, NULL);
  std::wstring name;
  if (SUCCEEDED(hr)) {
    name = V_BSTR(&var);
  }
  VariantClear(&var);
  return name;
}

PinFinder::PinFinder()
{
}

PinFinder::~PinFinder()
{
}

int PinFinder::Init(IBaseFilterPtr& filter)
{
  if (!filter) {
    DBGLOG("ERROR: NULL filter.");
    return WebmEncoder::kInvalidArg;
  }
  HRESULT hr = filter->EnumPins(&pin_enum_);
  if (FAILED(hr)) {
    DBGLOG("ERROR: cannot enum filter pins!" << HRLOG(hr));
    return WebmEncoder::kInitFailed;
  }
  return WebmEncoder::kSuccess;
}

IPinPtr PinFinder::FindAudioInputPin(int index) const
{
  IPinPtr pin;
  int num_found = 0;
  for (;;) {
    HRESULT hr = pin_enum_->Next(1, &pin, NULL);
    if (hr != S_OK) {
      break;
    }
    PinInfo pin_info(pin);
    if (pin_info.IsInput() && pin_info.IsAudio()) {
      ++num_found;
      if (num_found == index+1) {
        break;
      }
    }
    pin.Release();
  }
  return pin;
}

IPinPtr PinFinder::FindAudioOutputPin(int index) const
{
  IPinPtr pin;
  int num_found = 0;
  for (;;) {
    HRESULT hr = pin_enum_->Next(1, &pin, NULL);
    if (hr != S_OK) {
      break;
    }
    PinInfo pin_info(pin);
    if (pin_info.IsOutput() && pin_info.IsAudio()) {
      ++num_found;
      if (num_found == index+1) {
        break;
      }
    }
    pin.Release();
  }
  return pin;
}

IPinPtr PinFinder::FindVideoInputPin(int index) const
{
  IPinPtr pin;
  int num_found = 0;
  for (;;) {
    HRESULT hr = pin_enum_->Next(1, &pin, NULL);
    if (hr != S_OK) {
      break;
    }
    PinInfo pin_info(pin);
    if (pin_info.IsInput() && pin_info.IsVideo()) {
      ++num_found;
      if (num_found == index+1) {
        break;
      }
    }
    pin.Release();
  }
  return pin;
}

IPinPtr PinFinder::FindVideoOutputPin(int index) const
{
  IPinPtr pin;
  int num_found = 0;
  for (;;) {
    HRESULT hr = pin_enum_->Next(1, &pin, NULL);
    if (hr != S_OK) {
      break;
    }
    PinInfo pin_info(pin);
    if (pin_info.IsOutput() && pin_info.IsVideo()) {
      ++num_found;
      if (num_found == index+1) {
        break;
      }
    }
    pin.Release();
  }
  return pin;
}

PinInfo::PinInfo(IPinPtr& ptr_pin)
    : pin_(ptr_pin)
{
}

PinInfo::~PinInfo()
{
}

void free_media_type(AM_MEDIA_TYPE* ptr_media_type)
{
  if (ptr_media_type) {
    AM_MEDIA_TYPE& mt = *ptr_media_type;
    if (mt.cbFormat != 0) {
      CoTaskMemFree((PVOID)mt.pbFormat);
      mt.cbFormat = 0;
      mt.pbFormat = NULL;
    }
    if (mt.pUnk != NULL) {
      // Unecessary because pUnk should not be used, but safest.
      mt.pUnk->Release();
      mt.pUnk = NULL;
    }
  }
}

bool PinInfo::HasMajorType(GUID major_type) const
{
  bool has_type = false;
  if (pin_) {
    IEnumMediaTypesPtr mediatype_enum;
    HRESULT hr = pin_->EnumMediaTypes(&mediatype_enum);
    if (SUCCEEDED(hr)) {
      for (;;) {
        AM_MEDIA_TYPE* ptr_media_type = NULL;
        hr = mediatype_enum->Next(1, &ptr_media_type, 0);
        if (hr != S_OK) {
          break;
        }
        has_type = (ptr_media_type && ptr_media_type->majortype == major_type);
        free_media_type(ptr_media_type);
        if (has_type) {
          break;
        }
      }
    }
  }
  return has_type;
}

bool PinInfo::IsAudio() const
{
  bool is_audio_pin = false;
  if (pin_) {
    is_audio_pin = HasMajorType(MEDIATYPE_Audio);
  }
  return is_audio_pin;
}

bool PinInfo::IsInput() const
{
  bool is_input = false;
  if (pin_) {
    PIN_DIRECTION direction;
    HRESULT hr = pin_->QueryDirection(&direction);
    is_input = (hr == S_OK && direction == PINDIR_INPUT);
  }
  return is_input;
}

bool PinInfo::IsOutput() const
{
  bool is_output = false;
  if (pin_) {
    PIN_DIRECTION direction;
    HRESULT hr = pin_->QueryDirection(&direction);
    is_output = (hr == S_OK && direction == PINDIR_OUTPUT);
  }
  return is_output;
}

bool PinInfo::IsVideo() const
{
  bool is_video_pin = false;
  if (pin_) {
    is_video_pin = HasMajorType(MEDIATYPE_Video);
  }
  return is_video_pin;
}

} // WebmLive
