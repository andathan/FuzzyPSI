#include "IknpDotExtSender.h"
#include "droidCrypto/ot/NaorPinkas.h"

#include <droidCrypto/ChannelWrapper.h>
#include <droidCrypto/Matrix.h>
#include <droidCrypto/SecureRandom.h>
#include <droidCrypto/utils/Log.h>
#include <droidCrypto/utils/Utils.h>
#include <chrono>

namespace droidCrypto {
//#define KOS_DEBUG

std::unique_ptr<OtExtSender> IknpDotExtSender::split() {
  auto dot = new IknpDotExtSender();
  std::unique_ptr<OtExtSender> ret(dot);
  std::vector<block> baseRecvOts(mGens.size());
  for (uint64_t i = 0; i < mGens.size(); ++i)
    baseRecvOts[i] = mGens[i].get<block>();
  ret->setBaseOts(baseRecvOts, mBaseChoiceBits);
  return std::move(ret);
}

void IknpDotExtSender::setBaseOts(span<block> baseRecvOts,
                                  const BitVector &choices) {
  mBaseChoiceBits = choices;
  mGens.resize(choices.size());
  mBaseChoiceBits.resize(roundUpTo(mBaseChoiceBits.size(), 8));
  for (uint64_t i = mBaseChoiceBits.size() - 1; i >= choices.size(); --i)
    mBaseChoiceBits[i] = 0;

  mBaseChoiceBits.resize(choices.size());
  for (uint64_t i = 0; i < mGens.size(); i++) mGens[i].SetSeed(baseRecvOts[i]);
}

void IknpDotExtSender::setDelta(const block &delta) { mDelta = delta; }

void IknpDotExtSender::send(span<std::array<block, 2>> messages, PRNG &prng,
                            ChannelWrapper &chl) {
  // round up
  uint64_t numOtExt = roundUpTo(messages.size(), 128 * superBlkSize);
  uint64_t numSuperBlocks = numOtExt / 128 / superBlkSize;

  // a temp that will be used to transpose the sender's matrix
  Matrix<uint8_t> t(mGens.size(), superBlkSize * sizeof(block));
  std::vector<std::array<block, superBlkSize>> u(mGens.size() * commStepSize);

  std::vector<block> choiceMask(mBaseChoiceBits.size());
  std::array<block, 2> delta{ZeroBlock, ZeroBlock};

  memcpy(delta.data(), mBaseChoiceBits.data(), mBaseChoiceBits.sizeBytes());

  for (uint64_t i = 0; i < choiceMask.size(); ++i) {
    if (mBaseChoiceBits[i])
      choiceMask[i] = AllOneBlock;
    else
      choiceMask[i] = ZeroBlock;
  }

  std::array<std::array<block, 2>, 128> extraBlocks;
  std::array<block, 2> *xIter = extraBlocks.data();

  auto mIter = messages.begin();
  auto mIterPartial =
      messages.end() - std::min<uint64_t>(128 * superBlkSize, messages.size());

  // set uIter = to the end so that it gets loaded on the first loop.
  block *uIter = (block *)u.data() + superBlkSize * mGens.size() * commStepSize;
  block *uEnd = uIter;

  for (uint64_t superBlkIdx = 0; superBlkIdx < numSuperBlocks; ++superBlkIdx) {
    if (uIter == uEnd) {
      uint64_t step = std::min<uint64_t>(numSuperBlocks - superBlkIdx,
                                         (uint64_t)commStepSize);
      chl.recv((uint8_t *)u.data(),
               step * superBlkSize * mGens.size() * sizeof(block));
      uIter = (block *)u.data();
    }

    block *cIter = choiceMask.data();
    block *tIter = (block *)t.data();

    // transpose 128 columns at at time. Each column will be 128 * superBlkSize
    // = 1024 bits long.
    for (uint64_t colIdx = 0; colIdx < mGens.size(); ++colIdx) {
      // generate the columns using AES-NI in counter mode.
      mGens[colIdx].mAes.encryptCTR(mGens[colIdx].mBlockIdx, superBlkSize,
                                    tIter);
      mGens[colIdx].mBlockIdx += superBlkSize;

      uIter[0] = uIter[0] & *cIter;
      uIter[1] = uIter[1] & *cIter;
      uIter[2] = uIter[2] & *cIter;
      uIter[3] = uIter[3] & *cIter;
      uIter[4] = uIter[4] & *cIter;
      uIter[5] = uIter[5] & *cIter;
      uIter[6] = uIter[6] & *cIter;
      uIter[7] = uIter[7] & *cIter;

      tIter[0] = tIter[0] ^ uIter[0];
      tIter[1] = tIter[1] ^ uIter[1];
      tIter[2] = tIter[2] ^ uIter[2];
      tIter[3] = tIter[3] ^ uIter[3];
      tIter[4] = tIter[4] ^ uIter[4];
      tIter[5] = tIter[5] ^ uIter[5];
      tIter[6] = tIter[6] ^ uIter[6];
      tIter[7] = tIter[7] ^ uIter[7];

      ++cIter;
      uIter += 8;
      tIter += 8;
    }

    if (mIter >= mIterPartial) {
      Matrix<uint8_t> tOut(128 * superBlkSize, sizeof(block) * 2);

      // transpose our 128 columns of 1024 bits. We will have 1024 rows,
      // each 128 bits wide.
      Utils::transpose(t, tOut);

      auto mCount =
          std::min<uint64_t>(128 * superBlkSize, messages.end() - mIter);
      auto xCount =
          std::min<uint64_t>(128 * superBlkSize - mCount,
                             extraBlocks.data() + extraBlocks.size() - xIter);

      // std::copy(mIter, mIter + mCount, tOut.begin());
      if (mCount) memcpy(&*mIter, tOut.data(), mCount * sizeof(block) * 2);
      mIter += mCount;

      memcpy(xIter, tOut.data() + mCount * sizeof(block) * 2,
             xCount * sizeof(block) * 2);
      xIter += xCount;
    } else {
      MatrixView<uint8_t> tOut((uint8_t *)&*mIter, 128 * superBlkSize,
                               sizeof(block) * 2);

      mIter += std::min<uint64_t>(128 * superBlkSize, messages.end() - mIter);

      // transpose our 128 columns of 1024 bits. We will have 1024 rows,
      // each 128 bits wide.
      Utils::transpose(t, tOut);
    }
  }

  block seed = prng.get<block>();
  chl.send((uint8_t *)&seed, sizeof(block));

  PRNG codePrng(seed);
  LinearCode code;

  code.random(codePrng, mBaseChoiceBits.size(), 128);

  block curDelta;
  code.encode((uint8_t *)delta.data(), (uint8_t *)&curDelta);

  if (eq(mDelta, ZeroBlock)) mDelta = prng.get<block>();

  block offset = curDelta ^ mDelta;
  chl.send(offset);

  uint64_t doneIdx = 0;

  uint64_t bb = (messages.size() + 127) / 128;
  for (uint64_t blockIdx = 0; blockIdx < bb; ++blockIdx) {
    uint64_t stop0 = std::min<uint64_t>(messages.size(), doneIdx + 128);

    uint64_t i = 0, dd = doneIdx;
    for (; dd < stop0; ++dd, ++i) {
      code.encode((uint8_t *)messages[dd].data(), (uint8_t *)&messages[dd][0]);
      messages[dd][1] = messages[dd][0] ^ mDelta;
    }

    doneIdx = stop0;
  }

  static_assert(gOtExtBaseOtCount == 128, "expecting 128");
}

}  // namespace droidCrypto
