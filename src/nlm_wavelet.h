#pragma once

#include "nlm_core.h"
#include <vector>

// 2-Level Haar DWT + NLM
void nlm_denoise_wavelet(const Image& src, Image& dst, const NlmParams& params);
