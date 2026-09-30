#pragma once
// This file and the associated implementation has been placed in the public
// domain, waiving all copyright. No restrictions are placed on its use.
#include <droidCrypto/PRNG.h>
#include <droidCrypto/ot/TwoChooseOne/OTExtInterface.h>

#include <array>

namespace droidCrypto {

class IknpOtExtReceiver : public OtExtReceiver {
 public:
  IknpOtExtReceiver() : mHasBase(false) {}

  virtual ~IknpOtExtReceiver() {}

  bool hasBaseOts() const override { return mHasBase; }

  bool mHasBase;
  std::array<std::array<PRNG, 2>, gOtExtBaseOtCount> mGens;

  void setBaseOts(span<std::array<block, 2>> baseSendOts) override;
  std::unique_ptr<OtExtReceiver> split() override;

  void receive(const BitVector &choices, span<block> messages, PRNG &prng,
               ChannelWrapper &chan) override;
};
}  // namespace droidCrypto
