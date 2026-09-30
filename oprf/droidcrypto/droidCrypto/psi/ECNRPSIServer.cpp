#include <assert.h>
#include <droidCrypto/ChannelWrapper.h>
#include <droidCrypto/PRNG.h>
#include <droidCrypto/SHA1.h>
#include <droidCrypto/SHAKE128.h>
#include <droidCrypto/ot/TwoChooseOne/KosOtExtSender.h>
#include <droidCrypto/ot/VerifiedSimplestOT.h>
#include <droidCrypto/endian_compat.h>
#include <droidCrypto/psi/ECNRPSIServer.h>
#include <droidCrypto/utils/Log.h>
#include <thread>
#include <iostream>

namespace droidCrypto {

ECNRPSIServer::ECNRPSIServer(ChannelWrapper &chan, size_t num_threads /*=1*/)
    : PhasedPSIServer(chan, num_threads),
      prng_(PRNG::getTestPRNG()),
      prf_(prng_, 128),
      num_client_elements_(0) {}

void ECNRPSIServer::Base() {
  std::cerr << "Base Start\n";
  channel_.clearStats();
  std::chrono::duration<double> base_time; 
  auto time1 = std::chrono::high_resolution_clock::now();
  size_t num_client_elements;
  channel_.recv((uint8_t *)&num_client_elements, sizeof(num_client_elements));
  num_client_elements_ = be64toh(num_client_elements);
  size_t numBaseOTs = 128;
  std::vector<block> baseOTs;
  BitVector baseChoices(numBaseOTs);
  baseChoices.randomize(prng_);
  baseOTs.resize(numBaseOTs);
  span<block> baseOTsSpan(baseOTs.data(), baseOTs.size());

  VerifiedSimplestOT ot;
  ot.receive(baseChoices, baseOTsSpan, prng_, channel_);
  KosOtExtSender otExtSender;
  otExtSender.setBaseOts(baseOTsSpan, baseChoices);

  ots_.resize(num_client_elements_ * 128);
  span<std::array<block, 2>> otSpan(ots_.data(), ots_.size());
  otExtSender.send(otSpan, prng_, channel_);

  base_time = std::chrono::high_resolution_clock::now() - time1;
  std::string time = "Time:\n\t base:   " + std::to_string(base_time.count());
  Log::v("PSI", "%s", time.c_str());
  Log::v("PSI",
         "Base Comm: %fMiB sent, %fMiB recv\n",
         channel_.getBytesSent() / 1024.0 / 1024.0,
         channel_.getBytesRecv() / 1024.0 / 1024.0);
  std::cerr << "Base Done\n";
}

void ECNRPSIServer::Online() {
  std::cerr << "Online Start\n";
  channel_.clearStats();
  // std::vector<std::array<uint8_t, 32>> prfInOut;
  BitVector bv(128 * num_client_elements_);

  channel_.recv(bv.data(), num_client_elements_ * 128 / 8);
  for (auto i = 0; i < num_client_elements_; i++) {
    BitVector c;
    c.copy(bv, 128 * i, 128);
    span<std::array<block, 2>> otSpan(&ots_[i * 128], 128);
    prf_.oprf(c, otSpan, channel_);
  }
  Log::v("PSI",
         "Online Comm: %fMiB sent, %fMiB recv\n",
         channel_.getBytesSent() / 1024.0 / 1024.0,
         channel_.getBytesRecv() / 1024.0 / 1024.0);
  channel_.clearStats();
  std::cerr << "Online Done\n";
}

}  // namespace droidCrypto
