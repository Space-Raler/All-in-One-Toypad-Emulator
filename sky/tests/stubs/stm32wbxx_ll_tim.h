#include "flipper_stub.h"
typedef struct { uint32_t CCR1; } TIM_TypeDef; extern TIM_TypeDef* TIM16;
#define LL_TIM_COUNTERMODE_UP 0
#define LL_TIM_CLOCKDIVISION_DIV1 0
#define LL_TIM_OCMODE_PWM1 0
#define LL_TIM_OCSTATE_ENABLE 1
#define LL_TIM_OCPOLARITY_HIGH 0
#define LL_TIM_CHANNEL_CH1 1
typedef struct { uint32_t Prescaler, CounterMode, Autoreload, ClockDivision, RepetitionCounter; } LL_TIM_InitTypeDef;
typedef struct { uint32_t OCMode, OCState, OCNState, CompareValue, OCPolarity, OCNPolarity, OCIdleState, OCNIdleState; } LL_TIM_OC_InitTypeDef;
int LL_TIM_Init(TIM_TypeDef*, LL_TIM_InitTypeDef*); int LL_TIM_OC_Init(TIM_TypeDef*, uint32_t, LL_TIM_OC_InitTypeDef*);
void LL_TIM_EnableARRPreload(TIM_TypeDef*); void LL_TIM_OC_EnablePreload(TIM_TypeDef*, uint32_t);
void LL_TIM_EnableAllOutputs(TIM_TypeDef*); void LL_TIM_DisableAllOutputs(TIM_TypeDef*);
void LL_TIM_GenerateEvent_UPDATE(TIM_TypeDef*); void LL_TIM_ClearFlag_UPDATE(TIM_TypeDef*);
void LL_TIM_EnableDMAReq_UPDATE(TIM_TypeDef*); void LL_TIM_DisableDMAReq_UPDATE(TIM_TypeDef*);
void LL_TIM_EnableCounter(TIM_TypeDef*); void LL_TIM_DisableCounter(TIM_TypeDef*);
