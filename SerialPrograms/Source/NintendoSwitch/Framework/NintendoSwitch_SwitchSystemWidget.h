/*  Switch System
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_NintendoSwitch_SwitchSystemWidget_H
#define PokemonAutomation_NintendoSwitch_SwitchSystemWidget_H

#include "Integrations/ProgramTracker.h"
#include "NintendoSwitch_SwitchSetupWidget.h"
#include "NintendoSwitch_SwitchSystem.h"

namespace PokemonAutomation{
namespace NintendoSwitch{


class CommandRow;

class SwitchSystemWidget : public SwitchSetupWidget, public ConsoleSystem{
    Q_OBJECT

public:
    SwitchSystemWidget(
        QWidget& parent,
        SwitchSystemFactory& factory,
        Logger& logger,
        uint64_t program_id
    );
    virtual ~SwitchSystemWidget();

    ProgramState last_known_state() const;

    virtual bool serial_ok() const override;
    virtual void wait_for_all_requests() override;
    virtual void stop_serial() override;
    virtual void reset_serial() override;

public:
    BotBase* botbase();
    VideoFeed& camera();
    VideoOverlay& overlay();
    virtual void update_ui(ProgramState state) override;

    virtual VideoFeed& video() override;
    virtual BotBaseHandle& sender() override;

private:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void keyReleaseEvent(QKeyEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    uint64_t m_instance_id = 0;
    SwitchSystemFactory& m_factory;
    SerialLogger m_logger;

    SerialSelectorWidget* m_serial;
    CommandRow* m_command;
    CameraSelectorWidget* m_camera;
    VideoDisplayWidget* m_video_display;
};




}
}
#endif