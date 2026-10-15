#include "flipper_stub.h"
typedef struct { int dummy; } DMA_TypeDef; extern DMA_TypeDef* DMA1;
#define LL_DMA_CHANNEL_1 1
#define LL_DMA_DIRECTION_MEMORY_TO_PERIPH 1
#define LL_DMA_PRIORITY_VERYHIGH 1
#define LL_DMA_MODE_CIRCULAR 1
#define LL_DMA_PERIPH_NOINCREMENT 1
#define LL_DMA_MEMORY_INCREMENT 1
#define LL_DMA_PDATAALIGN_HALFWORD 1
#define LL_DMA_MDATAALIGN_HALFWORD 1
void LL_DMA_DisableChannel(DMA_TypeDef*, uint32_t); void LL_DMA_EnableChannel(DMA_TypeDef*, uint32_t);
void LL_DMA_ConfigAddresses(DMA_TypeDef*, uint32_t, uint32_t, uint32_t, uint32_t);
void LL_DMA_SetDataLength(DMA_TypeDef*, uint32_t, uint32_t); void LL_DMA_SetPeriphRequest(DMA_TypeDef*, uint32_t, uint32_t);
void LL_DMA_SetChannelPriorityLevel(DMA_TypeDef*, uint32_t, uint32_t); void LL_DMA_SetMode(DMA_TypeDef*, uint32_t, uint32_t);
void LL_DMA_SetPeriphIncMode(DMA_TypeDef*, uint32_t, uint32_t); void LL_DMA_SetMemoryIncMode(DMA_TypeDef*, uint32_t, uint32_t);
void LL_DMA_SetPeriphSize(DMA_TypeDef*, uint32_t, uint32_t); void LL_DMA_SetMemorySize(DMA_TypeDef*, uint32_t, uint32_t);
void LL_DMA_EnableIT_HT(DMA_TypeDef*, uint32_t); void LL_DMA_EnableIT_TC(DMA_TypeDef*, uint32_t);
void LL_DMA_DisableIT_HT(DMA_TypeDef*, uint32_t); void LL_DMA_DisableIT_TC(DMA_TypeDef*, uint32_t);
uint32_t LL_DMA_IsActiveFlag_HT1(DMA_TypeDef*); uint32_t LL_DMA_IsActiveFlag_TC1(DMA_TypeDef*);
void LL_DMA_ClearFlag_HT1(DMA_TypeDef*); void LL_DMA_ClearFlag_TC1(DMA_TypeDef*);
void LL_DMA_SetDataTransferDirection(DMA_TypeDef*, uint32_t, uint32_t);
