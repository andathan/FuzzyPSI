#pragma once
// This file and the associated implementation has been placed in the public domain, waiving all copyright. No restrictions are placed on its use. 
#include <droidCrypto/ot/TwoChooseOne/OTExtInterface.h>
#include <array>
#include <droidCrypto/PRNG.h>

namespace droidCrypto
{

    class IknpDotExtReceiver :
        public OtExtReceiver
    {
    public:
        IknpDotExtReceiver()
            :mHasBase(false)
        {}

        bool hasBaseOts() const override
        {
            return mHasBase;
        }

        //LinearCode mCode;
        bool mHasBase;
        std::vector<std::array<PRNG, 2>> mGens;

        void setBaseOts(
            span<std::array<block, 2>> baseSendOts)override;


        std::unique_ptr<OtExtReceiver> split() override;

        void receive(
            const BitVector& choices,
            span<block> messages,
            PRNG& prng,
            ChannelWrapper& chl/*,
            std::atomic<u64>& doneIdx*/)override;


    };

}
