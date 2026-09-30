#include <stdint.h>
#include "Utils/intrinsics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct generator generator;



unsigned int workspace_size(unsigned int univ_size, unsigned int set_size);
generator* pset_ggm_init(unsigned int univ_size, unsigned int set_size, uint8_t* workspace);
void pset_ggm_eval(generator* gen, const uint8_t* seed, uint64_t* elems);

unsigned int pset_buffer_size(const generator* gen);
void pset_ggm_punc(generator* gen, const uint8_t* seed, unsigned int pos, uint8_t* pset);
void pset_ggm_eval_punc(generator* gen, const uint8_t* pset, unsigned int pos, uint64_t* elems);

int distinct(generator* gen, const uint64_t* elems, unsigned int num_elems);

#ifdef __cplusplus
}
#endif
