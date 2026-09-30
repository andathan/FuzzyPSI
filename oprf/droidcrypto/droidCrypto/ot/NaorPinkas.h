#pragma once

#include <droidCrypto/BitVector.h>
#include <droidCrypto/Defines.h>
#include <droidCrypto/ot/TwoChooseOne/OTExtInterface.h>

namespace droidCrypto {
class ChannelWrapper;

class NaorPinkas : public OtSender, public OtReceiver {
 public:
  NaorPinkas() = default;

  virtual void receive(const BitVector &choices, span<block> messages,
                       PRNG &prng, ChannelWrapper &chan);
  virtual void send(span<std::array<block, 2>> messages, PRNG &prng,
                    ChannelWrapper &chan);
};

}  // namespace droidCrypto
