/*  Pokemon Home Panels
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "CommonFramework/GlobalSettingsPanel.h"
#include "PokemonHome_Panels.h"

#include "PokemonHome_Settings.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonHome{



std::vector<PanelEntry> make_panels(){
    std::vector<PanelEntry> ret;

    ret.emplace_back("---- Settings ----");
//    ret.emplace_back(make_settings<GameSettings_Descriptor, GameSettingsPanel>());

//    ret.emplace_back("---- General ----");

//    ret.emplace_back("---- Trading ----");

//    ret.emplace_back("---- Farming ----");

//    ret.emplace_back("---- Shiny Hunting ----");

    if (PreloadSettings::instance().DEVELOPER_MODE){
//        ret.emplace_back("---- Developer Tools ----");
    }

    return ret;
}




}
}
}