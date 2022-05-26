/*  Map Location
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *  
 *  Function to detect player character location and orientation on the in-game map.
 */

#ifndef PokemonAutomation_PokemonLA_MapLocation_H
#define PokemonAutomation_PokemonLA_MapLocation_H

#include <QImage>

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLA{

enum class MapRegion;

// Get which direction the player is facing by detecting the direction of the red arrow on the map.
// The orientation is measured by the angle to the positive x-axis direction on the screen image.
// The image x-axis points to the right, and y-axis points downward.
// e.g south-east direction is measured as 45 degree.
// Range of the orientation: [0, 360), or FLT_MAX if sth. wrong happens.
//
// Notes:
// - The red arrow won't be occluded by lables on the map.
// - Due to the lava being red on the map too, this function does not work if the player is inside
//   the volcano area in the coastlands when viewing the map in median zoom scale. It also does not
//   work if the player is near the volcano area when viewing the map in detailed zoom scale.
//   If `avoid_lava_area` is true, skip the red pixels that are in the upper right area of the map,
//   which is well the lava area is on the coastlands map in the median zoom scale.
float get_orientation_on_map(const QImage& screen, bool avoid_lava_area = false);


}
}
}
#endif
