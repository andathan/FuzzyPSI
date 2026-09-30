#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif



void xor_rows(const uint8_t* db, unsigned int db_len, 
    const uint64_t* elems, unsigned int num_elems,
    unsigned int block_len, uint8_t* out);

void xor_hashes_by_bit_vector(const uint8_t* db, unsigned int db_len, 
    const uint8_t* indexing, 
    uint8_t* out);

#ifdef __cplusplus
}
#endif
