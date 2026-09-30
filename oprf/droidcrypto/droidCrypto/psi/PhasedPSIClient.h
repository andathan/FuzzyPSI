#pragma once

#include <droidCrypto/Defines.h>
#include <chrono>
#include <vector>

namespace droidCrypto {
class ChannelWrapper;

class PhasedPSIClient {
 public:
  PhasedPSIClient(ChannelWrapper &chan)
      : channel_(chan), time_setup(0), time_base(0), time_online(0){};

  virtual ~PhasedPSIClient(){};

  virtual void doOPRF(std::vector<block> &elements, uint8_t * res) {
    Base(elements.size());
    OnlineOPRF(elements, res);
  }

  virtual void Base(size_t num_elements) = 0;
  virtual void OnlineOPRF(std::vector<block> &elements, uint8_t * ptr) = 0;

 protected:
  ChannelWrapper &channel_;
  std::chrono::duration<double> time_setup;
  std::chrono::duration<double> time_base;
  std::chrono::duration<double> time_online;
};
}  // namespace droidCrypto
