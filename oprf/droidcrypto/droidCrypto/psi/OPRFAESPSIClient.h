#pragma once

#include <droidCrypto/psi/PhasedPSIClient.h>
#include <droidCrypto/gc/circuits/AESCircuit.h>

namespace droidCrypto {
    class OPRFAESPSIClient : public PhasedPSIClient {
    public:
        OPRFAESPSIClient(ChannelWrapper& chan);

        void Base(size_t num_elements) override;
        void OnlineOPRF(std::vector<block> &elements, uint8_t * ptr) override; 

    private:
        SIMDAESCircuitPhases circ_;
    };
}

