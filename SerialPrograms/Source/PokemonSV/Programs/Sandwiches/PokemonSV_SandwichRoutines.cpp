/*  Sandwich Routines
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "Common/Cpp/Exceptions.h"
#include "Common/Cpp/Concurrency/AsyncDispatcher.h"
#include "CommonFramework/Exceptions/OperationFailedException.h"
#include "CommonFramework/InferenceInfra/InferenceRoutines.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlay.h"
#include "CommonFramework/Tools/ErrorDumper.h"
#include "CommonFramework/Tools/InterruptableCommands.h"
#include "CommonFramework/Tools/ProgramEnvironment.h"
#include "CommonFramework/ImageTools/ImageFilter.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_ScalarButtons.h"
#include "PokemonSV/Inference/Dialogs/PokemonSV_DialogDetector.h"
#include "PokemonSV/Inference/Dialogs/PokemonSV_GradientArrowDetector.h"
#include "PokemonSV/Inference/Picnics/PokemonSV_PicnicDetector.h"
#include "PokemonSV/Inference/Picnics/PokemonSV_SandwichHandDetector.h"
#include "PokemonSV/Inference/Picnics/PokemonSV_SandwichIngredientDetector.h"
#include "PokemonSV/Inference/Picnics/PokemonSV_SandwichRecipeDetector.h"
#include "PokemonSV/Resources/PokemonSV_FillingsCoordinates.h"
#include "PokemonSV/Resources/PokemonSV_Ingredients.h"
#include "PokemonSV_SandwichRoutines.h"
#include "PokemonSV/Programs/Sandwiches/PokemonSV_IngredientSession.h"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSV{

namespace {
    const ImageFloatBox HAND_INITIAL_BOX{0.440, 0.455, 0.112, 0.179};
    const ImageFloatBox INGREDIENT_BOX{0.455, 0.130, 0.090, 0.030};

void wait_for_initial_hand(const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context){
     SandwichHandWatcher free_hand(SandwichHandType::FREE, HAND_INITIAL_BOX);
    int ret = wait_until(console, context, std::chrono::seconds(30), {free_hand});
    if (ret < 0){
        dump_image_and_throw_recoverable_exception(info, console, "FreeHandNotDetected",
            "Cannot detect hand at start of making a sandwich.");
    }
}

} // anonymous namespace

bool enter_sandwich_recipe_list(const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context){
    context.wait_for_all_requests();
    console.log("Opening sandwich menu at picnic table.");

    // Firt, try pressing button A to bring up the menu to make sandwich
    pbf_press_button(context, BUTTON_A, 20, 80);

    WallClock start = current_time();
    bool opened_table_menu = false;
    while(true){
        context.wait_for_all_requests();
        if (current_time() - start > std::chrono::minutes(1)){
            dump_image_and_throw_recoverable_exception(
                info, console, "FailToSandwich",
                "enter_sandwich_recipe_list(): Failed to open sandwich menu after 1 minute."
            );
        }

        PicnicWatcher picnic_watcher;
        GradientArrowWatcher sandwich_arrow(COLOR_YELLOW, GradientArrowType::RIGHT, {0.551, 0.311, 0.310, 0.106});
        GradientArrowWatcher recipe_arrow(COLOR_YELLOW, GradientArrowType::DOWN, {0.103, 0.074, 0.068, 0.085});
        AdvanceDialogWatcher dialog_watcher(COLOR_RED);

        const int ret = wait_until(
            console, context,
            std::chrono::seconds(30),
            {picnic_watcher, sandwich_arrow, recipe_arrow, dialog_watcher}
        );
        switch (ret){
        case 0:
            console.log("Detected picnic. Maybe button A press dropped.");
            pbf_press_button(context, BUTTON_A, 20, 80);
            continue;
        case 1:
            console.log("Detected \"make a sandwich\" menu item selection arrrow.");
            console.overlay().add_log("Open sandwich recipes", COLOR_WHITE);
            opened_table_menu = true;
            pbf_press_button(context, BUTTON_A, 20, 100);
            continue;
        case 2:
            console.log("Detected recipe selection arrow.");
            context.wait_for(std::chrono::seconds(1)); // wait one second to make sure the menu is fully loaded.
            return true;
        case 3:
            console.log("Detected advance dialog.");
            if (opened_table_menu){
                console.log("Advance dialog after \"make a sandwich\" menu item. No ingredients.", COLOR_RED);
                console.overlay().add_log("No ingredient!", COLOR_RED);
                return false;
            }
            pbf_press_button(context, BUTTON_A, 20, 80);
            continue;
        default:
            dump_image_and_throw_recoverable_exception(info, console, "NotEnterSandwichList",
                "enter_sandwich_recipe_list(): No recognized state after 60 seconds.");
        }
    }
}


bool select_sandwich_recipe(const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context, size_t target_sandwich_ID){
    context.wait_for_all_requests();
    console.log("Choosing sandwich recipe: " + std::to_string(target_sandwich_ID));
    console.overlay().add_log("Search recipe " + std::to_string(target_sandwich_ID), COLOR_WHITE);

    SandwichRecipeNumberDetector recipe_detector(console.logger());
    SandwichRecipeSelectionWatcher selection_watcher;

    VideoOverlaySet overlay_set(console.overlay());
    recipe_detector.make_overlays(overlay_set);
    selection_watcher.make_overlays(overlay_set);

    bool found_recipe = false;
    int max_move_down_list_attempts = 100; // There are 151 total recipes, so 76 rows.
    for(int move_down_list_attempt = 0; move_down_list_attempt < max_move_down_list_attempts; move_down_list_attempt++) {
        context.wait_for_all_requests();

        auto snapshot = console.video().snapshot();
        size_t recipe_IDs[6] = {0, 0, 0, 0, 0, 0};
        recipe_detector.detect_recipes(snapshot, recipe_IDs);
        {
            std::ostringstream os;
            os << "Recipe IDs detected: ";
            for(int i = 0; i < 6; i++){
                os << recipe_IDs[i] << ", ";
            }
            console.log(os.str());
        }

        size_t min_ID = 300;
        for(int i = 0; i < 6; i++){
            if (recipe_IDs[i] > 0 && recipe_IDs[i] < min_ID){
                min_ID = recipe_IDs[i];
            }
        }
        if (min_ID == 300){
            min_ID = 0; // set case of no recipe ID detected to be 0 min_ID
        }
        size_t max_ID = *std::max_element(recipe_IDs, recipe_IDs+6);
        console.log("min, max IDs " + std::to_string(min_ID) + ", " + std::to_string(max_ID));

        if (0 < min_ID && min_ID <= target_sandwich_ID && target_sandwich_ID <= max_ID){
            // target is in this page!
            
            int target_cell = -1;
            for(int i = 0; i < 6; i++){
                if (recipe_IDs[i] == target_sandwich_ID){
                    target_cell = i;
                    break;
                }
            }
            if (target_cell == -1){ // not targe recipe found in this page, probably not enough ingredients
                console.log("Not enough ingredients for target recipe.", COLOR_RED);
                console.overlay().add_log("Not enough ingredients", COLOR_RED);
                return false;
            }

            console.log("found recipe in the current page, cell " + std::to_string(target_cell));
            
            int ret = wait_until(console, context, std::chrono::seconds(10), {selection_watcher});
            int selected_cell = selection_watcher.selected_recipe_cell();
            if (ret < 0 || selected_cell < 0){
                dump_image_and_throw_recoverable_exception(info, console, "RecipeSelectionArrowNotDetected",
                    "select_sandwich_recipe(): Cannot detect recipe selection arrow.");
            }

            console.log("Current selected cell " + std::to_string(selected_cell));

            if (target_cell == selected_cell){
                // Selected target recipe!
                console.log("Found recipe at cell " + std::to_string(selected_cell));
                console.overlay().add_log("Found recipe", COLOR_WHITE);
                found_recipe = true;
                break;
            }
            else if (target_cell == selected_cell + 1){
                console.log("Move to the right column.");
                // Target is in a different column
                // Move cursor right.
                pbf_press_dpad(context, DPAD_RIGHT, 10, 50);
                continue;
            }
            // else, continue moving down the list
        }

        // target sandwich recipe is still below the current displayed recipes.
        // Move down the list.
        pbf_press_dpad(context, DPAD_DOWN, 10, 50);
    } // end for moving down the recipe list

    overlay_set.clear();

    if (found_recipe){
        // Press A to enter the pick selection 
        pbf_press_button(context, BUTTON_A, 30, 100);
        context.wait_for_all_requests();

        SandwichIngredientArrowWatcher pick_selection(0, COLOR_YELLOW);
        while(true){
            int ret = wait_until(console, context, std::chrono::seconds(3),
                {selection_watcher, pick_selection});
            
            if (ret == 0){
                console.log("Detected recipe selection. Dropped Button A?");
                pbf_press_button(context, BUTTON_A, 30, 100);
                continue;
            } else if (ret == 1){
                console.log("Detected pick selection.");
                pbf_press_button(context, BUTTON_A, 30, 100);
                continue;
            } else{
                console.log("Entered sandwich minigame.");
                break;
            }
        }
        return true;
    }

    // we cannot find the receipt
    console.log("Max list travese attempt reached. Target recipe not found", COLOR_RED);
    console.overlay().add_log("Recipe not found", COLOR_RED);

    return false;
}

namespace {

// expand the hand bounding box so that the hand watcher can pick the hand in the next iteration
ImageFloatBox expand_box(const ImageFloatBox& box){
    const double x = std::max(0.0, box.x - box.width * 1.5);
    const double y = std::max(0.0, box.y - box.height * 1.5);
    const double width = std::min(box.width*4, 1.0 - x);
    const double height = std::min(box.height*4, 1.0 - y);
    return ImageFloatBox(x, y, width, height);
}

ImageFloatBox hand_location_to_box(const std::pair<double, double>& loc){
    const double hand_width = 0.071, hand_height = 0.106;
    return {loc.first - hand_width/2, loc.second - hand_height/2, hand_width, hand_height};
}

std::string box_to_string(const ImageFloatBox& box){
    std::ostringstream os;
    os << "(" << box.x << ", " << box.y << ", " << box.width << ", " << box.height << ")";
    return os.str();
}

ImageFloatBox move_sandwich_hand(
    const ProgramInfo& info,
    AsyncDispatcher& dispatcher,
    ConsoleHandle& console,
    BotBaseContext& context,
    SandwichHandType hand_type,
    bool pressing_A,
    const ImageFloatBox& start_box,
    const ImageFloatBox& end_box
){
    context.wait_for_all_requests();
    console.log("Start moving sandwich hand: " + SANDWICH_HAND_TYPE_NAMES(hand_type)
        + " start box " + box_to_string(start_box) + " end box " + box_to_string(end_box));

    uint8_t joystick_x = 128;
    uint8_t joystick_y = 128;

    SandwichHandWatcher hand_watcher(hand_type, start_box);

    // A session that creates a new thread to send button commands to controller
    AsyncCommandSession move_session(context, console.logger(), dispatcher, console.botbase());
    
    if (pressing_A){
        move_session.dispatch([](BotBaseContext& context){
            pbf_controller_state(context, BUTTON_A, DPAD_NONE, 128, 128, 128, 128, 3000);
        });
    }

    const std::pair<double, double> target_loc(end_box.x + end_box.width/2, end_box.y + end_box.height/2);

    std::pair<double, double> last_loc(-1, -1);
    std::pair<double, double> speed(-1, -1);
    WallClock cur_time, last_time;
    VideoOverlaySet overlay_set(console.overlay());

    while(true){
        int ret = wait_until(console, context, std::chrono::seconds(5), {hand_watcher});
        if (ret < 0){
            dump_image_and_throw_recoverable_exception(
                info, console,
                SANDWICH_HAND_TYPE_NAMES(hand_type) + "SandwichHandNotDetected",
                "move_sandwich_hand(): Cannot detect " + SANDWICH_HAND_TYPE_NAMES(hand_type) + " hand.",
                hand_watcher.last_snapshot()
            );
        }

        auto cur_loc = hand_watcher.location();
        console.log("Hand location: " + std::to_string(cur_loc.first) + ", " + std::to_string(cur_loc.second));
        cur_time = current_time();

        const ImageFloatBox hand_bb = hand_location_to_box(cur_loc); 
        const ImageFloatBox expanded_hand_bb = expand_box(hand_bb);
        hand_watcher.change_box(expanded_hand_bb);

        overlay_set.clear();
        overlay_set.add(COLOR_RED, hand_bb);
        overlay_set.add(COLOR_BLUE, expanded_hand_bb);

        std::pair<double, double> dif(target_loc.first - cur_loc.first, target_loc.second - cur_loc.second);
        // console.log("float diff to target: " + std::to_string(dif.first) + ", " + std::to_string(dif.second));
        if (std::fabs(dif.first) < end_box.width/2 && std::fabs(dif.second) < end_box.height/2){
            console.log(SANDWICH_HAND_TYPE_NAMES(hand_type) + " hand reached target.");
            move_session.stop_session_and_rethrow(); // Stop the commands
            if (hand_type == SandwichHandType::GRABBING){
                // wait for some time to let hand release ingredient
                context.wait_for(std::chrono::milliseconds(100));
            }
            return hand_bb;
        }

        // Assume screen width is 16.0, then the screen height is 9.0
        std::pair<double, double> real_dif(dif.first * 16, dif.second * 9);
        double distance = std::sqrt(real_dif.first * real_dif.first + real_dif.second * real_dif.second);
        // console.log("scaled diff to target: " + std::to_string(real_dif.first) + ", " + std::to_string(real_dif.second)
        //     + " distance " + std::to_string(distance));

        // Build a P-D controller!

        // We assume for a screen distance of 4 (1/4 of the width), we can use max joystick push, 128.
        // So for distance of value 1.0, we multiply by 32 to get joystick push
        double target_joystick_push = std::min(distance * 32, 128.0);

        std::pair<double, double> push(real_dif.first * target_joystick_push / distance, real_dif.second * target_joystick_push / distance);
        // console.log("push force " + std::to_string(push.first) + ", " + std::to_string(push.second));

        if (last_loc.first < 0){
            speed = std::make_pair(0.0, 0.0);
        } else {
            std::chrono::microseconds time = std::chrono::duration_cast<std::chrono::microseconds>(cur_time - last_time);
            double time_s = time.count() / 1000000.0;
            std::pair<double, double> moved((cur_loc.first - last_loc.first) * 16, (cur_loc.second - last_loc.second) * 9);

            // Currently set to zero damping as it seems we don't need them for now
            double damping_factor = 0.0;
            double damping_multiplier = (-1.0) * damping_factor / time_s;
            std::pair<double, double> damped_push_offset(moved.first * damping_multiplier, moved.second * damping_multiplier);

            push.first += damped_push_offset.first;
            push.second += damped_push_offset.second;
        }

        joystick_x = (uint8_t) std::max(std::min(int(push.first + 0.5) + 128, 255), 0);
        joystick_y = (uint8_t) std::max(std::min(int(push.second + 0.5) + 128, 255), 0);
        // console.log("joystick push " + std::to_string(joystick_x) + ", " + std::to_string(joystick_y));

        // Dispatch a new series of commands that overwrites the last ones
        move_session.dispatch([&](BotBaseContext& context){
            if (pressing_A){
                // Note: joystick_x and joystick_y must be defined to outlive `move_session`.
//                pbf_controller_state(context, BUTTON_A, DPAD_NONE, joystick_x, joystick_y, 128, 128, 20);
                ssf_press_button(context, BUTTON_A, 0, 1000, 0);
            }
            pbf_move_left_joystick(context, joystick_x, joystick_y, 20, 0);
        });
        
        console.log("Moved joystick");

        last_loc = cur_loc;
        last_time = cur_time;
        context.wait_for(std::chrono::milliseconds(80));
    }
}

} // end anonymous namesapce

void build_great_peanut_butter_sandwich(const ProgramInfo& info, AsyncDispatcher& dispatcher, ConsoleHandle& console, BotBaseContext& context){
    const ImageFloatBox sandwich_target_box_left  {0.386, 0.507, 0.060, 0.055};
    const ImageFloatBox sandwich_target_box_middle{0.470, 0.507, 0.060, 0.055};
    const ImageFloatBox sandwich_target_box_right {0.554, 0.507, 0.060, 0.055};
    const ImageFloatBox upper_bread_drop_box{0.482, 0.400, 0.036, 0.030};

    wait_for_initial_hand(info, console, context);
    console.overlay().add_log("Start making sandwich", COLOR_WHITE);

    // console.overlay().add_log("Pick first banana", COLOR_WHITE);
    auto end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::FREE, false, HAND_INITIAL_BOX, INGREDIENT_BOX);

    // console.overlay().add_log("Drop first banana", COLOR_WHITE);
    // visual feedback grabbing is not reliable. Switch to blind grabbing:
    end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::GRABBING, true, expand_box(end_box), sandwich_target_box_left);
    
    // pbf_controller_state(context, BUTTON_A, DPAD_NONE, 100, 200, 128, 128, 120);
    // context.wait_for(std::chrono::milliseconds(100));
    // context.wait_for_all_requests();

    // console.overlay().add_log("Pick second banana", COLOR_WHITE);
    end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::FREE, false, {0, 0, 1.0, 1.0}, INGREDIENT_BOX);

    // console.overlay().add_log("Drop second banana", COLOR_WHITE);
    end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::GRABBING, true, expand_box(end_box), sandwich_target_box_middle);
    // pbf_controller_state(context, BUTTON_A, DPAD_NONE, 128, 200, 128, 128, 120);
    // context.wait_for(std::chrono::milliseconds(100));
    // context.wait_for_all_requests();

    // console.overlay().add_log("Pick third banana", COLOR_WHITE);
    end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::FREE, false, {0, 0, 1.0, 1.0}, INGREDIENT_BOX);
    
    // console.overlay().add_log("Drop third banana", COLOR_WHITE);
    end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::GRABBING, true, expand_box(end_box), sandwich_target_box_right);
    // pbf_controller_state(context, BUTTON_A, DPAD_NONE, 156, 200, 128, 128, 120);
    // context.wait_for(std::chrono::milliseconds(100));
    // context.wait_for_all_requests();

    // Drop upper bread and pick
    // console.overlay().add_log("Drop upper bread and pick", COLOR_WHITE);
    SandwichHandWatcher grabbing_hand(SandwichHandType::FREE, {0, 0, 1.0, 1.0});
    int ret = wait_until(console, context, std::chrono::seconds(30), {grabbing_hand});
    if (ret < 0){
        throw OperationFailedException(
            ErrorReport::SEND_ERROR_REPORT, console,
            "make_great_peanut_butter_sandwich(): Cannot detect grabing hand when waiting for upper bread.",
            grabbing_hand.last_snapshot()
        );
    }

    auto hand_box = hand_location_to_box(grabbing_hand.location());

    end_box = move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::GRABBING, false, expand_box(hand_box), upper_bread_drop_box);
    pbf_mash_button(context, BUTTON_A, 125 * 5);

    console.log("Hand end box " + box_to_string(end_box));
    console.overlay().add_log("Built sandwich", COLOR_WHITE);

    context.wait_for_all_requests();
}

void finish_sandwich_eating(const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context){
    console.overlay().add_log("Eating", COLOR_WHITE);
    PicnicWatcher picnic_watcher;
    int ret = run_until(
        console, context,
        [](BotBaseContext& context){
            for(int i = 0; i < 20; i++){
                pbf_press_button(context, BUTTON_A, 20, 3*TICKS_PER_SECOND - 20);
            }
        },
        {picnic_watcher}
    );
    if (ret < 0){
        dump_image_and_throw_recoverable_exception(info, console, "PicnicNotDetected",
            "finish_sandwich_eating(): cannot detect picnic after 60 seconds.");
    }
    console.overlay().add_log("Finish eating", COLOR_WHITE);
    context.wait_for(std::chrono::seconds(1));
}

namespace{

void repeat_press_until(
    const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context,
    std::function<void()> button_press,
    const std::vector<PeriodicInferenceCallback>& callbacks,
    const std::string &error_name, const std::string &error_message,
    std::chrono::milliseconds detection_timeout = std::chrono::seconds(5),
    size_t max_presses = 10,
    std::chrono::milliseconds default_video_period = std::chrono::milliseconds(50),
    std::chrono::milliseconds default_audio_period = std::chrono::milliseconds(20)
){
    button_press();
    for(size_t i_try = 0; i_try < max_presses; i_try++){
        context.wait_for_all_requests();
        const int ret = wait_until(console, context, detection_timeout, callbacks);
        if (ret >= 0){
            return;
        }
        button_press();
    }

    dump_image_and_throw_recoverable_exception(info, console, "IngredientListNotDetected",
        "enter_custom_sandwich_mode(): cannot detect ingredient list after 50 seconds.");
}

void repeat_button_press_until(
    const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context,
    uint16_t button, uint16_t hold_ticks, uint16_t release_ticks,
    const std::vector<PeriodicInferenceCallback>& callbacks,
    const std::string &error_name, const std::string &error_message,
    std::chrono::milliseconds iteration_length = std::chrono::seconds(5),
    size_t max_presses = 10,
    std::chrono::milliseconds default_video_period = std::chrono::milliseconds(50),
    std::chrono::milliseconds default_audio_period = std::chrono::milliseconds(20)
){
    const std::chrono::milliseconds button_time = std::chrono::milliseconds((hold_ticks + release_ticks) * (1000 / TICKS_PER_SECOND));
    repeat_press_until(info, console, context,
        [&](){ pbf_press_button(context, button, hold_ticks, release_ticks); },
        callbacks, error_name, error_message, iteration_length - button_time, max_presses,
        default_video_period, default_audio_period
    );
}

void repeat_dpad_press_until(
    const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context,
    uint8_t dpad_position, uint16_t hold_ticks, uint16_t release_ticks,
    const std::vector<PeriodicInferenceCallback>& callbacks,
    const std::string &error_name, const std::string &error_message,
    std::chrono::milliseconds iteration_length = std::chrono::seconds(5),
    size_t max_presses = 10,
    std::chrono::milliseconds default_video_period = std::chrono::milliseconds(50),
    std::chrono::milliseconds default_audio_period = std::chrono::milliseconds(20)
){
    const std::chrono::milliseconds button_time = std::chrono::milliseconds((hold_ticks + release_ticks) * (1000 / TICKS_PER_SECOND));
    repeat_press_until(info, console, context,
        [&](){ pbf_press_dpad(context, dpad_position, hold_ticks, release_ticks); },
        callbacks, error_name, error_message, iteration_length - button_time, max_presses, 
        default_video_period, default_audio_period
    );
}


} // anonymous namespace


void enter_custom_sandwich_mode(const ProgramInfo& info, ConsoleHandle& console, BotBaseContext& context){
    context.wait_for_all_requests();
    console.log("Entering custom sandwich mode.");
    console.overlay().add_log("Custom sandwich", COLOR_WHITE);

    SandwichIngredientArrowWatcher ingredient_selection_arrow(0, COLOR_YELLOW);
    repeat_button_press_until(
        info, console, context, BUTTON_X, 40, 80, {ingredient_selection_arrow},
        "IngredientListNotDetected", "enter_custom_sandwich_mode(): cannot detect ingredient list after 50 seconds."
    );
}

namespace {

void finish_two_herbs_sandwich(
    const ProgramInfo& info, AsyncDispatcher& dispatcher, ConsoleHandle& console, BotBaseContext& context
){
    console.log("Finish determining ingredients for two-sweet-herb sandwich.");
    console.overlay().add_log("Finish picking ingredients", COLOR_WHITE);

    wait_for_initial_hand(info, console, context);

    console.overlay().add_log("Start making sandwich", COLOR_WHITE);
    move_sandwich_hand(info, dispatcher, console, context, SandwichHandType::FREE, false, HAND_INITIAL_BOX, INGREDIENT_BOX);
    // Mash button A to pick and drop ingredients, upper bread and pick.
    // Egg Power 3 is applied with only two sweet herb condiments!
    pbf_mash_button(context, BUTTON_A, 8 * TICKS_PER_SECOND);
    context.wait_for_all_requests();
    console.overlay().add_log("Built sandwich", COLOR_WHITE);
}

} // anonymous namesapce

void make_two_herbs_sandwich(
    const ProgramInfo& info, AsyncDispatcher& dispatcher, ConsoleHandle& console, BotBaseContext& context,
    EggSandwichType sandwich_type, size_t sweet_herb_index_last, size_t salty_herb_index_last, size_t bitter_herb_index_last
){
    // The game has at most 5 herbs, in the order of sweet, salty, sour, bitter, spicy:
    if (sweet_herb_index_last >= 5){ // sweet index can only be: 0, 1, 2, 3, 4
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid sweet herb index: " + std::to_string(sweet_herb_index_last));
    }
    if (salty_herb_index_last >= 4){ // 0, 1, 2, 3
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid salty herb index: " + std::to_string(salty_herb_index_last));
    }
    if (bitter_herb_index_last >= 2){ // 0, 1
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid bitter herb index: " + std::to_string(bitter_herb_index_last));
    }

    if (sandwich_type == EggSandwichType::SALTY_SWEET_HERBS && salty_herb_index_last >= sweet_herb_index_last){
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid salty and sweet herb indices: " + std::to_string(salty_herb_index_last) + ", " + std::to_string(sweet_herb_index_last));
    }
    if (sandwich_type == EggSandwichType::BITTER_SWEET_HERBS && bitter_herb_index_last >= sweet_herb_index_last){
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid bitter and sweet herb indices: " + std::to_string(bitter_herb_index_last) + ", " + std::to_string(sweet_herb_index_last));
    }

    {
        // Press button A to add first filling, assumed to be lettuce
        DeterminedSandwichIngredientWatcher filling_watcher(SandwichIngredientType::FILLING, 0);
        repeat_button_press_until(
            info, console, context, BUTTON_A, 40, 50, {filling_watcher},
            "DeterminedIngredientNotDetected", "make_two_herbs_sandwich(): cannot detect determined lettuce after 50 seconds."
        );
    }

    {
        // Press button + to go to condiments page
        SandwichCondimentsPageWatcher condiments_page_watcher;
        repeat_button_press_until(
            info, console, context, BUTTON_PLUS, 40, 60, {condiments_page_watcher},
            "CondimentsPageNotDetected", "make_two_herbs_sandwich(): cannot detect condiments page after 50 seconds."
        );
    }

    size_t first_herb_index_last = 0;
    switch(sandwich_type){
    case EggSandwichType::TWO_SWEET_HERBS:
        first_herb_index_last = sweet_herb_index_last;
        break;
    case EggSandwichType::SALTY_SWEET_HERBS:
        first_herb_index_last = salty_herb_index_last;
        break;
    case EggSandwichType::BITTER_SWEET_HERBS:
        first_herb_index_last = bitter_herb_index_last;
        break;
    default:
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid EggSandwichType for make_two_herbs_sandwich()");
    }

    auto move_one_up_to_row = [&](size_t row){
        console.log("Move arrow to row " + std::to_string(row));
        SandwichIngredientArrowWatcher arrow(row);
        repeat_dpad_press_until(info, console, context, DPAD_UP, 10, 30, {arrow}, "IngredientArrowNotDetected",
            "make_two_herbs_sandwich(): cannot detect ingredient selection arrow at row " + std::to_string(row) + " after 50 seconds."
        );
    };

    auto press_a_to_determine_herb = [&](size_t herb_index){
        DeterminedSandwichIngredientWatcher herb_watcher(SandwichIngredientType::CONDIMENT, herb_index);
        repeat_button_press_until(
            info, console, context, BUTTON_A, 40, 60, {herb_watcher}, "CondimentsPageNotDetected",
            "make_two_herbs_sandwich(): cannot detect detemined herb at cell " + std::to_string(herb_index) + " after 50 seconds."
        );
    };

    // Press DPAD_UP multiple times to move to the first herb row
    for(size_t i = 0; i < first_herb_index_last+1; i++){
        move_one_up_to_row(9 - i);
    }
    press_a_to_determine_herb(0); // Press A to detemine one herb
    // Press DPAD_UP againt to move to the second herb row
    for(size_t i = first_herb_index_last+1; i < sweet_herb_index_last+1; i++){
        move_one_up_to_row(9 - i);
    }
    press_a_to_determine_herb(1); // Press A to detemine the second herb

    {
        // Press button + to go to picks page
        SandwichPicksPageWatcher picks_page_watcher;
        repeat_button_press_until(
            info, console, context, BUTTON_PLUS, 40, 60, {picks_page_watcher},
            "CondimentsPageNotDetected", "make_two_herbs_sandwich(): cannot detect picks page after 50 seconds."
        );
    }

    // Mesh button A to select the first pick
    pbf_mash_button(context, BUTTON_A, 80);
    context.wait_for_all_requests();

    finish_two_herbs_sandwich(info, dispatcher, console, context);
}

void make_two_herbs_sandwich(
    const ProgramInfo& info, AsyncDispatcher& dispatcher, ConsoleHandle& console, BotBaseContext& context,
    EggSandwichType sandwich_type, Language language
){
    std::map<std::string, uint8_t> fillings = {{"lettuce", (uint8_t)1}};
    std::map<std::string, uint8_t> condiments = {{"sweet-herba-mystica", (uint8_t)1}};
    switch(sandwich_type){
    case EggSandwichType::TWO_SWEET_HERBS:
        condiments["sweet-herba-mystica"] = 2;
        break;
    case EggSandwichType::SALTY_SWEET_HERBS:
        condiments["salty-herba-mystica"] = 1;
        break;
    case EggSandwichType::BITTER_SWEET_HERBS:
        condiments["bitter-herba-mystica"] = 1;
        break;
    default:
        throw InternalProgramError(&console.logger(), PA_CURRENT_FUNCTION,
            "Invalid EggSandwichType for make_two_herbs_sandwich()");
    }
    add_sandwich_ingredients(dispatcher, console, context, language, std::move(fillings), std::move(condiments));

    finish_two_herbs_sandwich(info, dispatcher, console, context);
}

void run_sandwich_maker(SingleSwitchProgramEnvironment& env, BotBaseContext& context, SandwichMakerOption& SANDWICH_OPTIONS) {
    if (SANDWICH_OPTIONS.LANGUAGE == Language::None) {
        throw UserSetupError(env.console.logger(), "Must set game langauge option to read ingredient lists.");
    }

    int num_fillings = 0;
    int num_condiments = 0;
    std::map<std::string, uint8_t> fillings;
    std::map<std::string, uint8_t> condiments;

    //Add the selected ingredients to the maps if set to custom
    if (SANDWICH_OPTIONS.BASE_RECIPE == SandwichMakerOption::BaseRecipe::custom) {
        env.log("Custom sandwich selected. Validating ingredients.", COLOR_BLACK);
        env.console.overlay().add_log("Custom sandwich selected. Validating ingredients.", COLOR_WHITE);

        std::vector<std::unique_ptr<SandwichIngredientsTableRow>> table = SANDWICH_OPTIONS.SANDWICH_INGREDIENTS.copy_snapshot();

        for (const std::unique_ptr<SandwichIngredientsTableRow>& row : table) {
            const std::string& table_item = row->item.slug();
            if (!(table_item == "baguette")) { //ignore baguette
                if (std::find(ALL_SANDWICH_FILLINGS_SLUGS().begin(), ALL_SANDWICH_FILLINGS_SLUGS().end(), table_item) != ALL_SANDWICH_FILLINGS_SLUGS().end()) {
                    fillings[table_item]++;
                    num_fillings++;
                }
                else {
                    condiments[table_item]++;
                    num_condiments++;
                }
            }
            else {
                env.log("Skipping baguette as it is unobtainable.");
                env.console.overlay().add_log("Skipping baguette as it is unobtainable.", COLOR_WHITE);
            }
        }

        if (num_fillings == 0 || num_condiments == 0) {
            throw UserSetupError(env.console.logger(), "Must have at least one filling and at least one condiment.");
        }

        if (num_fillings > 6 || num_condiments > 4) {
            throw UserSetupError(env.console.logger(), "Number of fillings exceed 6 and/or number of condiments exceed 4.");
        }
        env.log("Ingredients validated.", COLOR_BLACK);
        env.console.overlay().add_log("Ingredients validated.", COLOR_WHITE);
    }
    //Otherwise get the preset ingredients
    else {
        env.log("Preset sandwich selected.", COLOR_BLACK);
        env.console.overlay().add_log("Preset sandwich selected.", COLOR_WHITE);

        std::vector<std::string> table = SANDWICH_OPTIONS.get_premade_ingredients(SANDWICH_OPTIONS.get_premade_sandwich_recipe(SANDWICH_OPTIONS.BASE_RECIPE, SANDWICH_OPTIONS.TYPE, SANDWICH_OPTIONS.PARADOX));

        for (auto&& s : table) {
            if (std::find(ALL_SANDWICH_FILLINGS_SLUGS().begin(), ALL_SANDWICH_FILLINGS_SLUGS().end(), s) != ALL_SANDWICH_FILLINGS_SLUGS().end()) {
                fillings[s]++;
                num_fillings++;
            }
            else {
                condiments[s]++;
                num_condiments++;
            }
        }
        //Insert Herba Mystica if required
        if (SandwichMakerOption::two_herba_required(SANDWICH_OPTIONS.BASE_RECIPE)) {
            if (SANDWICH_OPTIONS.HERBA_ONE == SANDWICH_OPTIONS.HERBA_TWO) {
                condiments.insert(std::make_pair(SANDWICH_OPTIONS.herba_to_string(SANDWICH_OPTIONS.HERBA_ONE), (uint8_t)2));
            }
            else {
                condiments.insert(std::make_pair(SANDWICH_OPTIONS.herba_to_string(SANDWICH_OPTIONS.HERBA_ONE), (uint8_t)1));
                condiments.insert(std::make_pair(SANDWICH_OPTIONS.herba_to_string(SANDWICH_OPTIONS.HERBA_TWO), (uint8_t)1));
            }
            num_condiments++;
            num_condiments++;
        }
    }

    /*
    //Print ingredients
    cout << "Fillings:" << endl;
    for (const auto& [key, value] : fillings) {
        std::cout << key << ": " << (int)value << endl;
    }
    cout << "Condiments:" << endl;
    for (const auto& [key, value] : condiments) {
        std::cout << key << ": " << (int)value << endl;
    }
    */

    //Sort the fillings by priority for building (ex. large items on bottom, cherry tomatoes on top)
    //std::vector<std::string> fillings_game_order = {"lettuce", "tomato", "cherry-tomatoes", "cucumber", "pickle", "onion", "red-onion", "green-bell-pepper", "red-bell-pepper",
    //    "yellow-bell-pepper", "avocado", "bacon", "ham", "prosciutto", "chorizo", "herbed-sausage", "hamburger", "klawf-stick", "smoked-fillet", "fried-fillet", "egg", "potato-tortilla",
    //    "tofu", "rice", "noodles", "potato-salad", "cheese", "banana", "strawberry", "apple", "kiwi", "pineapple", "jalapeno", "watercress", "basil"};
    std::vector<std::string> fillings_game_order = { "hamburger", "rice", "noodles", "smoked-fillet", "fried-fillet", "cucumber", "pickle", "tofu",
        "chorizo", "herbed-sausage", "potato-tortilla", "klawf-stick", "lettuce", "tomato", "onion", "red-onion", "green-bell-pepper", "red-bell-pepper", "yellow-bell-pepper", "avocado",
        "bacon", "ham", "prosciutto", "cheese", "banana", "strawberry", "apple", "kiwi", "pineapple", "jalape\xc3\xb1o", "watercress", "potato-salad", "egg", "basil", "cherry-tomatoes" };

    //Add keys to new vector and sort
    std::vector<std::string> fillings_sorted;
    for (auto i = fillings.begin(); i != fillings.end(); i++) {
        fillings_sorted.push_back(i->first);
    }
    std::unordered_map<std::string, int> temp_map;
    for (auto i = 0; i < (int)fillings_game_order.size(); i++) {
        temp_map[fillings_game_order[i]] = i;
    }
    auto compare = [&temp_map](const std::string& s, const std::string& s1) {
        return temp_map[s] < temp_map[s1];
    };
    std::sort(fillings_sorted.begin(), fillings_sorted.end(), compare);

    /*
    //Print sorted fillings
    cout << "Sorted fillings:" << endl;
    for (auto i : fillings_sorted) {
        cout << i << endl;
    }
    */

    //Calculate number of bowls there will be on the build screen
    //Get each ingredient in list order, then get the number of times it appears from the map
    //Also store how many of each ingredient is in each bowl (ex. 6 onion in first bowl and then 3 onion in the next)
    int bowls = 0;
    std::vector<int> bowl_amounts;

    for (auto i : fillings_sorted) {

        //Add "full" bowls
        int bowl_calcs = (int)(fillings[i] / FillingsCoordinates::instance().get_filling_information(i).servingsPerBowl);
        if (bowl_calcs != 0) {
            bowls += bowl_calcs;
            for (int j = 0; j < bowl_calcs; j++) {
                bowl_amounts.push_back((int)(FillingsCoordinates::instance().get_filling_information(i).servingsPerBowl) * (int)(FillingsCoordinates::instance().get_filling_information(i).piecesPerServing));
            }
        }

        //Add bowls for remaining servings
        int bowl_remaining = ((int)(fillings[i] % FillingsCoordinates::instance().get_filling_information(i).servingsPerBowl));
        if (bowl_remaining != 0) {
            bowls++;
            bowl_amounts.push_back(bowl_remaining * (int)(FillingsCoordinates::instance().get_filling_information(i).piecesPerServing));
        }
    }
    //cout << "Number of bowls: " << bowls << endl;
    env.log("Number of bowls: " + std::to_string(bowls), COLOR_BLACK);
    env.console.overlay().add_log("Number of bowls: " + std::to_string(bowls), COLOR_WHITE);

    //Player must be on default sandwich menu
    std::map<std::string, uint8_t> fillings_copy(fillings); //Making a copy as we need the map for later
    enter_custom_sandwich_mode(env.program_info(), env.console, context);
    add_sandwich_ingredients(env.realtime_dispatcher(), env.console, context, SANDWICH_OPTIONS.LANGUAGE, std::move(fillings_copy), std::move(condiments));
    wait_for_initial_hand(env.program_info(), env.console, context);

    //Wait for labels to appear
    env.log("Waiting for labels to appear.", COLOR_BLACK);
    env.console.overlay().add_log("Waiting for labels to appear.", COLOR_WHITE);
    pbf_wait(context, 300);
    context.wait_for_all_requests();

    //Now read in bowl labels and store which bowl has what
    //TODO: Clean this up - this is a mess
    env.log("Reading bowl labels.", COLOR_BLACK);
    env.console.overlay().add_log("Reading bowl labels.", COLOR_WHITE);

    VideoSnapshot screen = env.console.video().snapshot();
    ImageFloatBox left_bowl_label(0.099, 0.270, 0.205, 0.041);
    ImageFloatBox center_bowl_label(0.397, 0.268, 0.203, 0.044);
    ImageFloatBox right_bowl_label(0.699, 0.269, 0.201, 0.044);

    //VideoOverlaySet label_set(env.console);
    //label_set.add(COLOR_BLUE, center_bowl_label);
    std::vector<std::string> bowl_order;

    //Get 3 default labels
    ImageRGB32 image_center_label = to_blackwhite_rgb32_range(
        extract_box_reference(screen, center_bowl_label),
        combine_rgb(215, 215, 215), combine_rgb(255, 255, 255), true
    );
    ImageRGB32 image_left_label = to_blackwhite_rgb32_range(
        extract_box_reference(screen, left_bowl_label),
        combine_rgb(215, 215, 215), combine_rgb(255, 255, 255), true
    );
    ImageRGB32 image_right_label = to_blackwhite_rgb32_range(
        extract_box_reference(screen, right_bowl_label),
        combine_rgb(215, 215, 215), combine_rgb(255, 255, 255), true
    );

    OCR::StringMatchResult result = PokemonSV::SandwichFillingOCR::instance().read_substring(
        env.console, SANDWICH_OPTIONS.LANGUAGE, image_center_label,
        OCR::BLACK_TEXT_FILTERS()
    );
    result.clear_beyond_log10p(SandwichFillingOCR::MAX_LOG10P);
    result.clear_beyond_spread(SandwichFillingOCR::MAX_LOG10P_SPREAD);
    if (result.results.empty()) {
        throw OperationFailedException(
            ErrorReport::SEND_ERROR_REPORT, env.console,
            "No ingredient found on center label.",
            true
        );
    }
    for (auto& r : result.results) {
        env.console.log("Ingredient found on center label: " + r.second.token);
        env.console.overlay().add_log("Ingredient found on center label : " + r.second.token, COLOR_WHITE);
        bowl_order.push_back(r.second.token);
    }
    //Get left (2nd) ingredient
    result = PokemonSV::SandwichFillingOCR::instance().read_substring(
        env.console, SANDWICH_OPTIONS.LANGUAGE, image_left_label,
        OCR::BLACK_TEXT_FILTERS()
    );
    result.clear_beyond_log10p(SandwichFillingOCR::MAX_LOG10P);
    result.clear_beyond_spread(SandwichFillingOCR::MAX_LOG10P_SPREAD);
    if (result.results.empty()) {
        env.log("No ingredient found on left label.", COLOR_BLACK);
        env.console.overlay().add_log("No ingredient found on left label.", COLOR_WHITE);
    }
    for (auto& r : result.results) {
        env.console.log("Ingredient found on left label: " + r.second.token);
        env.console.overlay().add_log("Ingredient found on left label: " + r.second.token, COLOR_WHITE);
        bowl_order.push_back(r.second.token);
    }
    //Get right (3rd) ingredient
    result = PokemonSV::SandwichFillingOCR::instance().read_substring(
        env.console, SANDWICH_OPTIONS.LANGUAGE, image_right_label,
        OCR::BLACK_TEXT_FILTERS()
    );
    result.clear_beyond_log10p(SandwichFillingOCR::MAX_LOG10P);
    result.clear_beyond_spread(SandwichFillingOCR::MAX_LOG10P_SPREAD);
    if (result.results.empty()) {
        env.log("No ingredient found on right label.", COLOR_BLACK);
        env.console.overlay().add_log("No ingredient found on right label.", COLOR_WHITE);
    }
    for (auto& r : result.results) {
        env.console.log("Ingredient found on right label: " + r.second.token);
        env.console.overlay().add_log("Ingredient found on right label: " + r.second.token, COLOR_WHITE);
        bowl_order.push_back(r.second.token);
    }
    //Get remaining ingredients if any
    //center 1, left 2, right 3, far left 4, far far left/right 5, right 6
    //this differs from the game layout: far right is 5 and far far left/right is 6 in game
    //however as long as we stay internally consistent with this numbering it will work
    for (int i = 0; i < (bowls - 3); i++) {
        pbf_press_button(context, BUTTON_R, 20, 80);
        pbf_wait(context, 100);
        context.wait_for_all_requests();

        VideoSnapshot screen2 = env.console.video().snapshot();
        ImageRGB32 image_side_label = to_blackwhite_rgb32_range(
            extract_box_reference(screen2, left_bowl_label),
            combine_rgb(215, 215, 215), combine_rgb(255, 255, 255), true
        );
        //image_side_label.save("./image_side_label.png");

        result = PokemonSV::SandwichFillingOCR::instance().read_substring(
            env.console, SANDWICH_OPTIONS.LANGUAGE, image_side_label,
            OCR::BLACK_TEXT_FILTERS()
        );
        result.clear_beyond_log10p(SandwichFillingOCR::MAX_LOG10P);
        result.clear_beyond_spread(SandwichFillingOCR::MAX_LOG10P_SPREAD);
        if (result.results.empty()) {
            env.log("No ingredient found on side label.", COLOR_BLACK);
            env.console.overlay().add_log("No ingredient found on side label.", COLOR_WHITE);
        }
        for (auto& r : result.results) {
            env.console.log("Ingredient found on side label: " + r.second.token);
            env.console.overlay().add_log("Ingredient found on side label: " + r.second.token, COLOR_WHITE);
            bowl_order.push_back(r.second.token);
        }
    }

    //Now re-center bowls
    env.log("Re-centering bowls if needed.", COLOR_BLACK);
    env.console.overlay().add_log("Re-centering bowls if needed.", COLOR_WHITE);
    for (int i = 0; i < (bowls - 3); i++) {
        pbf_press_button(context, BUTTON_L, 20, 80);
    }

    /*
    cout << "Labels:" << endl;
    for (auto i : bowl_order) {
        cout << i << endl;
    }
    */

    //If a label fails to read it'll cause issues down the line
    if ((int)bowl_order.size() != bowls) {
        throw OperationFailedException(
            ErrorReport::SEND_ERROR_REPORT, env.console,
            "Number of bowl labels did not match number of bowls.",
            true
        );
    }

    //Finally.
    env.log("Start making sandwich", COLOR_BLACK);
    env.console.overlay().add_log("Start making sandwich.", COLOR_WHITE);

    const ImageFloatBox center_bowl{ 0.455, 0.130, 0.090, 0.030 };
    const ImageFloatBox left_bowl{ 0.190, 0.136, 0.096, 0.031 };
    const ImageFloatBox right_bowl{ 0.715, 0.140, 0.108, 0.033 };

    ImageFloatBox target_bowl = center_bowl;
    //Initial position handling
    auto end_box = move_sandwich_hand(env.program_info(), env.realtime_dispatcher(), env.console, context, SandwichHandType::FREE, false, HAND_INITIAL_BOX, HAND_INITIAL_BOX);
    move_sandwich_hand(env.program_info(), env.realtime_dispatcher(), env.console, context, SandwichHandType::GRABBING, true, { 0, 0, 1.0, 1.0 }, HAND_INITIAL_BOX);
    context.wait_for_all_requests();

    //Find fillings and add them in order
    for (auto i : fillings_sorted) {
        //cout << "Placing " << i << endl;
        env.console.overlay().add_log("Placing " + i, COLOR_WHITE);

        int times_to_place = (int)(FillingsCoordinates::instance().get_filling_information(i).piecesPerServing) * (fillings.find(i)->second);
        int placement_number = 0;

        //cout << "Times to place: " << times_to_place << endl;
        env.console.overlay().add_log("Times to place: " + std::to_string(times_to_place), COLOR_WHITE);

        std::vector<int> bowl_index;
        //Get the bowls we want to go to
        for (int j = 0; j < (int)bowl_order.size(); j++) {
            if (i == bowl_order.at(j)) {
                bowl_index.push_back(j);
            }
        }

        //Target the correct filling bowl and place until it is empty
        for (int j = 0; j < (int)bowl_index.size(); j++) {
            //Navigate to bowl and set target bowl
            //cout << "Target bowl: " << bowl_index.at(j) << endl;
            env.console.overlay().add_log("Target bowl: " + std::to_string(bowl_index.at(j)), COLOR_WHITE);
            switch (bowl_index.at(j)) {
            case 0:
                target_bowl = center_bowl;
                break;
            case 1:
                target_bowl = left_bowl;
                break;
            case 2:
                target_bowl = right_bowl;
                break;
            case 3: case 4: case 5: case 6:
                //Press R the appropriate number of times
                for (int k = 2; k < bowl_index.at(j); k++) {
                    pbf_press_button(context, BUTTON_R, 20, 80);
                }
                target_bowl = left_bowl;
                break;
            default:
                break;
            }

            //Place the fillings until label does not light up yellow on grab/the piece count is not hit
            while (true) {
                //Break out after placing all pieces of the filling
                if (placement_number == times_to_place) {
                    break;
                }

                end_box = move_sandwich_hand(env.program_info(), env.realtime_dispatcher(), env.console, context, SandwichHandType::FREE, false, { 0, 0, 1.0, 1.0 }, target_bowl);
                context.wait_for_all_requests();

                //Get placement location
                ImageFloatBox placement_target = FillingsCoordinates::instance().get_filling_information(i).placementCoordinates.at((int)fillings.find(i)->second).at(placement_number);

                end_box = move_sandwich_hand(env.program_info(), env.realtime_dispatcher(), env.console, context, SandwichHandType::GRABBING, true, expand_box(end_box), placement_target);
                context.wait_for_all_requests();

                //If any of the labels are yellow continue. Otherwise assume bowl is empty move on to the next.
                VideoSnapshot label_color_check = env.console.video().snapshot();
                ImageRGB32 left_check = filter_rgb32_range(
                    extract_box_reference(label_color_check, left_bowl_label),
                    combine_rgb(180, 161, 0), combine_rgb(255, 255, 100), Color(0), false
                );
                ImageRGB32 right_check = filter_rgb32_range(
                    extract_box_reference(label_color_check, right_bowl_label),
                    combine_rgb(180, 161, 0), combine_rgb(255, 255, 100), Color(0), false
                );
                ImageRGB32 center_check = filter_rgb32_range(
                    extract_box_reference(label_color_check, center_bowl_label),
                    combine_rgb(180, 161, 0), combine_rgb(255, 255, 100), Color(0), false
                );
                ImageStats left_stats = image_stats(left_check);
                ImageStats right_stats = image_stats(right_check);
                ImageStats center_stats = image_stats(center_check);

                //cout << "Left stats: " << left_stats.count << endl;
                //cout << "Right stats: " << right_stats.count << endl;
                //cout << "Center stats: " << center_stats.count << endl;

                //The label check is needed for ingredients with multiple bowls as we don't know which bowl has what amount
                if (left_stats.count < 100 && right_stats.count < 100 && center_stats.count < 100) {
                    context.wait_for_all_requests();
                    break;
                }

                //If the bowl is empty the increment is skipped using the above break
                placement_number++;
            }

            //Reset bowl positions
            for (int k = 2; k < bowl_index.at(j); k++) {
                pbf_press_button(context, BUTTON_L, 20, 80);
            }
        }
    }
    // Handle top slice by tossing it away
    SandwichHandWatcher grabbing_hand(SandwichHandType::FREE, { 0, 0, 1.0, 1.0 });
    int ret = wait_until(env.console, context, std::chrono::seconds(30), { grabbing_hand });
    if (ret < 0) {
        throw OperationFailedException(
            ErrorReport::SEND_ERROR_REPORT, env.console,
            "SandwichMaker: Cannot detect grabing hand when waiting for upper bread.",
            grabbing_hand.last_snapshot()
        );
    }

    auto hand_box = hand_location_to_box(grabbing_hand.location());

    end_box = move_sandwich_hand(env.program_info(), env.realtime_dispatcher(), env.console, context, SandwichHandType::GRABBING, false, expand_box(hand_box), center_bowl);
    pbf_mash_button(context, BUTTON_A, 125 * 5);

    env.log("Hand end box " + box_to_string(end_box));
    env.log("Built sandwich", COLOR_BLACK);
    env.console.overlay().add_log("Hand end box " + box_to_string(end_box), COLOR_WHITE);
    env.console.overlay().add_log("Built sandwich.", COLOR_WHITE);
    context.wait_for_all_requests();

    finish_sandwich_eating(env.program_info(), env.console, context);
}

}
}
}
