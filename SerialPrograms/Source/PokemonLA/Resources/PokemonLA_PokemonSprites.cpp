/*  Pokemon LA Icons
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "PokemonLA_PokemonSprites.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLA{


const SpriteDatabase& ALL_POKEMON_SPRITES(){
    static const SpriteDatabase database("PokemonLA/PokemonSprites.png", "PokemonLA/PokemonSprites.json");
    return database;
}



}
}
}
