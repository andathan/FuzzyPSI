#pragma once

#include <droidCrypto/BitVector.h>
#include <droidCrypto/psi/PhasedPSIClient.h>

namespace droidCrypto {

class ECNRPSIClient : public PhasedPSIClient {
 public:
  ECNRPSIClient(ChannelWrapper &chan);

  void Base(size_t num_elements) override;
  void OnlineOPRF(std::vector<block> &elements, uint8_t * ptr) override; 

 private:
  std::vector<block> ots_;
  BitVector ot_choices_;
};
}  // namespace droidCrypto
