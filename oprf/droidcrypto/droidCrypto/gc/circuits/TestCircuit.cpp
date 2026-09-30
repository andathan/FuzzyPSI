#include <droidCrypto/gc/circuits/TestCircuit.h>
#include <droidCrypto/ChannelWrapper.h>
#include <droidCrypto/utils/Log.h>
#include <droidCrypto/BitVector.h>
#include <droidCrypto/gc/HalfGate.h>
#include <droidCrypto/gc/WireLabel.h>
#include <assert.h>


namespace droidCrypto {

    std::vector<WireLabel> TestCircuit::computeFunction(const std::vector<WireLabel>& inputA, const std::vector<WireLabel>& inputB, GCEnv& env) {
       std::vector<WireLabel> outputs;
       outputs.push_back(env.AND(inputA[0], inputB[0]));
       return outputs;
    }

    std::vector<SIMDWireLabel>
    SIMDTestCircuit::computeFunction(const std::vector<SIMDWireLabel> &inputA,
                                     const std::vector<SIMDWireLabel> &inputB, SIMDGCEnv &env) {

        assert(inputA.size() == BIT_NUMBER);
        assert(inputB.size() == BIT_NUMBER);

        for(const SIMDWireLabel& wl : inputA) {
            for(size_t i = 0; i < wl.bytes.size(); i++) {
                BitVector a;
                a.assign(wl.bytes[i]);
                Log::v("GC", "A%d: %s", i, a.hex().c_str());
            }
        }

        for(const SIMDWireLabel& wl : inputB) {
            for(size_t i = 0; i < wl.bytes.size(); i++) {
                BitVector a;
                a.assign(wl.bytes[i]);
                Log::v("GC", "B%d: %s", i, a.hex().c_str());
            }
        }

        SIMDWireLabel a = env.AND(inputA[0], inputB[0]);
//        for(int i = 1; i < BIT_NUMBER; i++) {
//            SIMDWireLabel b = env.XOR(inputA[i], inputB[i]);
//            a = env.AND(a, b);
//        }
        std::vector<SIMDWireLabel> outputs;
        outputs.push_back(a);
        return outputs;
    }
}
