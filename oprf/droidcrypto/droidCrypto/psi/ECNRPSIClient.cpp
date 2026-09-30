#include <assert.h>
#include <droidCrypto/ChannelWrapper.h>
#include <droidCrypto/PRNG.h>
#include <droidCrypto/RCurve.h>
#include <droidCrypto/SHA1.h>
#include <droidCrypto/SHAKE128.h>
#include <droidCrypto/ot/TwoChooseOne/KosOtExtReceiver.h>
#include <droidCrypto/ot/VerifiedSimplestOT.h>
#include <droidCrypto/endian_compat.h>
#include <droidCrypto/psi/ECNRPSIClient.h>
#include <droidCrypto/utils/Log.h>
#include <chrono>
#include <iostream>

namespace droidCrypto {

ECNRPSIClient::ECNRPSIClient(ChannelWrapper &chan)
    : PhasedPSIClient(chan) {}

void ECNRPSIClient::Base(size_t num_elements) {
  std::cerr << "Base Start\n";
  channel_.clearStats();
  std::chrono::duration<double> base_time; 
  auto time1 = std::chrono::high_resolution_clock::now();
  size_t num_client_elements = htobe64(num_elements);
  channel_.send((uint8_t *)&num_client_elements, sizeof(num_client_elements));
  VerifiedSimplestOT ot;

  size_t numBaseOTs = 128;
  std::vector<std::array<block, 2>> baseOTs;
  baseOTs.resize(numBaseOTs);
  PRNG p = PRNG::getTestPRNG();
  span<std::array<block, 2>> baseOTsSpan(baseOTs.data(), baseOTs.size());
  ot.send(baseOTsSpan, p, channel_);
  KosOtExtReceiver OTeRecv;
  OTeRecv.setBaseOts(baseOTsSpan);
  ot_choices_.resize(num_elements * 128);
  ot_choices_.randomize(p);
  ots_.resize(num_elements * 128);
  span<block> otSpan(ots_.data(), ots_.size());
  OTeRecv.receive(ot_choices_, otSpan, p, channel_);

  base_time = std::chrono::high_resolution_clock::now() - time1;
  std::string time = "Time:\t base:   " + std::to_string(base_time.count());
  Log::v("PSI", "%s", time.c_str());
  Log::v("PSI",
         "Base Comm: %fMiB sent, %fMiB recv\n",
         channel_.getBytesSent() / 1024.0 / 1024.0,
         channel_.getBytesRecv() / 1024.0 / 1024.0);
  std::cerr << "Base Done\n";
}

void ECNRPSIClient::OnlineOPRF(std::vector<block> &elements, uint8_t * ptr) {
  // do OPRF evaluation
  channel_.clearStats();
  PRNG p = PRNG::getTestPRNG();
  auto time4 = std::chrono::high_resolution_clock::now();
  block *choices = (block *)ot_choices_.data();
  for (auto i = 0; i < elements.size(); i++) {
    choices[i] ^= elements[i];
  }
  auto time5 = std::chrono::high_resolution_clock::now();
  channel_.send(ot_choices_.data(), elements.size() * 128 / 8);
  REllipticCurve curve;
  auto time6 = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> recv2; 
  for (auto i = 0; i < elements.size(); i++) {
    auto time9 = std::chrono::high_resolution_clock::now();
    BitVector bv;
    bv.assign(elements[i]);
    REccNumber r(curve, 1);
    REccNumber rj(curve, 0);
    std::array<uint8_t, 33> buf{};
    std::array<uint8_t, 128 * 32 + 33> buf1{};
    channel_.recv(buf1.data(), buf1.size());

    for (auto j = 0; j < 128; j++) {
      PRNG p_rj(ots_[i * 128 + j], 2);
      p_rj.get(buf.data(), 32);
      rj.fromBytes(buf.data());
      rj.toBytes(buf.data());
      if (bv[j]) {
        for (auto k = 0; k < 32; k++) {
          buf[k] ^= buf1[j * 32 + k];
        }
      }
      rj.fromBytes(buf.data());
      r *= rj;
    }
    REccPoint gT(curve);
    gT.fromBytes(buf1.data() + 128 * 32);
    gT = gT * r;

    recv2 += std::chrono::high_resolution_clock::now() - time9;
    // std::cout << gT << "\n";
    gT.toBytes(buf.data());
    memcpy(ptr, buf.data(), 33);
    ptr = ptr+33;
  }
  //auto time6 = std::chrono::high_resolution_clock::now();

  std::chrono::duration<double> send1 = time5 - time4;
  std::chrono::duration<double> recv = time6 - time5;
  //std::chrono::duration<double> recv = time6 - time5;

  std::string time = "Time:\n\t prep:   " + std::to_string(send1.count());
  time += ",\n\t prf:  " + std::to_string((recv+recv2).count());
  Log::v("PSI", "%s", time.c_str());
  droidCrypto::Log::v("ECNR", "Sent: %zu, Recv: %zu", channel_.getBytesSent(),
                      channel_.getBytesRecv());

  Log::v("PSI", "Online Time Total: %fsec", (send1+recv+recv2).count());
}

}  // namespace droidCrypto
