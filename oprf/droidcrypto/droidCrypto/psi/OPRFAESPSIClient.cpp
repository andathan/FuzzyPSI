#include <droidCrypto/psi/OPRFAESPSIClient.h>
#include <droidCrypto/ChannelWrapper.h>
#include <droidCrypto/BitVector.h>
#include <droidCrypto/endian_compat.h>
#include <droidCrypto/utils/Log.h>
#include <assert.h>
#include <iostream>


namespace droidCrypto {

    OPRFAESPSIClient::OPRFAESPSIClient(ChannelWrapper& chan) : PhasedPSIClient(chan), circ_(chan) {}

    void OPRFAESPSIClient::Base(size_t num_elements) {
        size_t num_client_elements = htobe64(num_elements);
        channel_.send((uint8_t*)&num_client_elements, sizeof(num_client_elements));

        circ_.evaluateBase(num_elements);
    }

    void OPRFAESPSIClient::OnlineOPRF(std::vector<block> &elements, uint8_t* ptr) {
        size_t num_client_elements = elements.size();
        //do GC evaluation

        std::vector<BitVector> bit_elements;
        bit_elements.reserve(elements.size());
        for(size_t i = 0; i < elements.size(); i++) {
            BitVector bitinput((uint8_t*)(&elements[i]), 128);
            bit_elements.push_back(bitinput);
        }
        channel_.clearStats();
        std::vector<BitVector> result = circ_.evaluateOnline(bit_elements);

        std::cout << "Size Bitvector: " << result.size() << "\n";

        std::string time = "Time:\n\t OT:   " + std::to_string(circ_.timeBaseOT.count());
        time += ",\n\t OTe:  " + std::to_string(circ_.timeOT.count());
        time += ",\n\t Send: " + std::to_string(circ_.timeSendGC.count());
        time += ",\n\t Eval: " + std::to_string(circ_.timeEval.count());
        time += ";\n\t Total:" + std::to_string((circ_.timeBaseOT+circ_.timeOT+circ_.timeEval+circ_.timeSendGC).count());
        droidCrypto::Log::v("GC", "%s", time.c_str());
        droidCrypto::Log::v("GC", "Sent: %zu, Recv: %zu", channel_.getBytesSent(), channel_.getBytesRecv());

        // //result[i].size() = 128 bit
        for (size_t i = 0; i < num_client_elements; i++) {
            memcpy(ptr, result[i].data(), 16);
            ptr = ptr+16;
        }
    }

}
