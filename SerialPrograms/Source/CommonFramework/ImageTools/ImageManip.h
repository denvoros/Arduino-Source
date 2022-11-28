/*  Image Manipulation
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_CommonFramework_ImageManip_H
#define PokemonAutomation_CommonFramework_ImageManip_H

#include "CommonFramework/ImageTypes/ImageRGB32.h"

namespace PokemonAutomation{


ImageRGB32 pad_image(const ImageViewRGB32& image, size_t pixels_on_each_side, uint32_t pixel);


}
#endif
