#include "capture_controls.h"
#include "rec_ui.h"

#ifdef __linux__
#include <fcntl.h>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace {
struct Control {
  const char* name;
  int type, channels;
  int max, defaultWhenZero;
  bool micTrim = false;
  snd_ctl_elem_value previous{}, applied{};
  bool changed = false;
};

class CaptureControls {
 public:
  ~CaptureControls() { release(); }
  bool acquire(const char* device, bool microphone) {
    if (fd_ >= 0) return true;
    fd_ = open(device, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) return false;
    snd_ctl_card_info card{};
    if (ioctl(fd_, SNDRV_CTL_IOCTL_CARD_INFO, &card) < 0 ||
        std::strcmp(reinterpret_cast<const char*>(card.id), "audiocodec") != 0) {
      release();
      return false;
    }
    for (auto& c : settings_) {
      if (c.micTrim && !microphone) continue;
      if (!readSetting(c)) {
        release();
        return false;
      }
    }
    for (auto& c : settings_) {
      if (c.micTrim && !microphone) continue;
      if (std::memcmp(&c.previous.value, &c.applied.value, sizeof(c.applied.value)) == 0) continue;
      if (ioctl(fd_, SNDRV_CTL_IOCTL_ELEM_WRITE, &c.applied) < 0) {
        release();
        return false;
      }
      c.changed = true;
    }
    microphone_ = microphone;
    return true;
  }
  void release() {
    if (fd_ < 0) return;
    for (auto it = settings_.rbegin(); it != settings_.rend(); ++it) {
      if (it->changed) ioctl(fd_, SNDRV_CTL_IOCTL_ELEM_WRITE, &it->previous);
      it->changed = false;
    }
    close(fd_);
    fd_ = -1;
    microphone_ = false;
  }
  bool microphone() const { return microphone_; }

 private:
  // Mixer numids vary with kernel versions. Validate each control by name and shape
  // before changing any of them. Preserve an existing ADC volume and restore the mic preamp on close.
  bool readSetting(Control& c) {
    snd_ctl_elem_info info{};
    info.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
    std::strncpy(reinterpret_cast<char*>(info.id.name), c.name, sizeof(info.id.name) - 1);
    if (ioctl(fd_, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0 || info.type != c.type ||
        info.count != static_cast<unsigned>(c.channels))
      return false;
    if (c.type == SNDRV_CTL_ELEM_TYPE_INTEGER && (info.value.integer.min != 0 || info.value.integer.max != c.max))
      return false;
    c.previous = {};
    c.previous.id = info.id;
    if (ioctl(fd_, SNDRV_CTL_IOCTL_ELEM_READ, &c.previous) < 0) return false;
    c.applied = c.previous;
    for (int i = 0; i < c.channels; ++i) {
      auto& value = c.applied.value.integer.value[i];
      if (c.micTrim) value = 0;  // The built-in left mic clips in the codec at its stock +31 dB preamp setting.
      else if (!value) value = c.defaultWhenZero;
    }
    return true;
  }
  int fd_ = -1;
  bool microphone_ = false;
  std::array<Control, 4> settings_{{{"ADCL Input MIC1 Boost Switch", SNDRV_CTL_ELEM_TYPE_BOOLEAN, 1, 1, 1},
                                    {"ADCR Input MIC2 Boost Switch", SNDRV_CTL_ELEM_TYPE_BOOLEAN, 1, 1, 1},
                                    {"ADC volume", SNDRV_CTL_ELEM_TYPE_INTEGER, 2, 255, 127},  // fallback -24 dB
                                    {"MIC1 gain volume", SNDRV_CTL_ELEM_TYPE_INTEGER, 1, 31, 0, true}}};
};
CaptureControls controls;
}  // namespace
#endif

namespace brick {
bool acquireCaptureControls(bool microphone) {
#ifdef __linux__
  const char* device = std::getenv("DTRK_BRICK_ADC");
  if (device && *device && !std::getenv("DTRK_CAPTURE_DEVICE")) return controls.acquire(device, microphone);
#else
  (void)microphone;
#endif
  return true;
}
void releaseCaptureControls() {
#ifdef __linux__
  controls.release();
#endif
}
int microphoneGainDb(int gain) {
#ifdef __linux__
  if (controls.microphone()) return gain - mt::recGainDefault(mt::RecSource::Mic);
#endif
  return gain;
}
}  // namespace brick
