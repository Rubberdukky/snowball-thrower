#include "Joystick.h"
#include <stdint.h>

void new_loopforever(state_t* state, const command* coms, size_t n_coms) {
    state->commands = coms;
    state->n_commands = n_coms;
    state->current = 0;
    state->repeated = 0;
    state->loop_type = LOOP_FOREVER;
}

void new_n(state_t* state, const command* coms, size_t n_coms, uint32_t times, finish_callback_f* f, void* arg) {
    state->commands = coms;
    state->n_commands = n_coms;
    state->current = 0;
    state->repeated = 0;
    state->loop_type = LOOP_N;
    state->times = times;
    state->callback_f = f;
    state->cb_arg = arg;
}

// Process and deliver data from IN and OUT endpoints.
void HID_Task(state_t* state) {
    // If the device isn't connected and properly configured, we can't do anything here.
    if (USB_DeviceState != DEVICE_STATE_Configured) {
        return;
    }

    // We'll start with the OUT endpoint.
    Endpoint_SelectEndpoint(JOYSTICK_OUT_EPADDR);
    // We'll check to see if we received something on the OUT endpoint.
    if (Endpoint_IsOUTReceived()) {
        // If we did, and the packet has data, we'll react to it.
        if (Endpoint_IsReadWriteAllowed()) {
            // We'll create a place to store our data received from the host.
            USB_JoystickReport_Output_t JoystickOutputData;
            // We'll then take in that data, setting it up in our storage.
            while(Endpoint_Read_Stream_LE(&JoystickOutputData, sizeof(JoystickOutputData), NULL) != ENDPOINT_RWSTREAM_NoError);
            // At this point, we can react to this data.

            // However, since we're not doing anything with this data, we abandon it.
        }
        // Regardless of whether we reacted to the data, we acknowledge an OUT packet on this endpoint.
        Endpoint_ClearOUT();
    }

    // We'll then move on to the IN endpoint.
    Endpoint_SelectEndpoint(JOYSTICK_IN_EPADDR);
    // We first check to see if the host is ready to accept data.
    if (Endpoint_IsINReady()) {
        // We'll create an empty report.
        USB_JoystickReport_Input_t JoystickInputData;
        // We'll then populate this report with what we want to send to the host.
        GetNextReport(state, &JoystickInputData);
        // Once populated, we can output this data to the host. We do this by first writing the data to the control stream.
        while(Endpoint_Write_Stream_LE(&JoystickInputData, sizeof(JoystickInputData), NULL) != ENDPOINT_RWSTREAM_NoError);
        // We then send an IN packet on this endpoint.
        Endpoint_ClearIN();
    }
}

void GetNextReport(state_t* state, USB_JoystickReport_Input_t* ReportData) {
    // Prepare an empty report
    memset(ReportData, 0, sizeof(USB_JoystickReport_Input_t));
    ReportData->LX = STICK_CENTER;
    ReportData->LY = STICK_CENTER;
    ReportData->RX = STICK_CENTER;
    ReportData->RY = STICK_CENTER;
    ReportData->HAT = HAT_CENTER;

    const command* com = &state->commands[state->current];
    Buttons_t but = com->button;

    // TODO - Decide whether or not to ECHOES here.

    if (but == GENERIC) but = com->cb(state, com->cb_arg);

    switch (com->button) {
        case UP:
            ReportData->LY = STICK_MIN;
            break;

        case LEFT:
            ReportData->LX = STICK_MIN;
            break;

        case DOWN:
            ReportData->LY = STICK_MAX;
            break;

        case RIGHT:
            ReportData->LX = STICK_MAX;
            break;

        case A:
            ReportData->Button |= SWITCH_A;
            break;

        case B:
            ReportData->Button |= SWITCH_B;
            break;

        case R:
            ReportData->Button |= SWITCH_R;
            break;

        case THROW:
            ReportData->LY = STICK_MIN;
            ReportData->Button |= SWITCH_R;
            break;

        case TRIGGERS:
            ReportData->Button |= SWITCH_L | SWITCH_R;
            break;

        case BUMPERS:
            ReportData->Button |= SWITCH_ZL | SWITCH_ZR;
            break;

        default:
            ReportData->LX = STICK_CENTER;
            ReportData->LY = STICK_CENTER;
            ReportData->RX = STICK_CENTER;
            ReportData->RY = STICK_CENTER;
            ReportData->HAT = HAT_CENTER;
            break;
    }

    if (++(state->repeated) == com->duration) {
        state->repeated = 0;
        state->current++;
        if (state->current == state->n_commands) {
            state->current = 0;
            switch (state->loop_type) {
                case LOOP_FOREVER:
                    break;
                case LOOP_N:
                    if (--(state->times) == 0) {
                        state->callback_f(state, state->cb_arg);
                    }
                    break;
            }
        }
    }
}

static const command command_setup[] = {
    // Setup controller
    PRESS(NOTHING,  500),
    PRESS(TRIGGERS,   2),
    PRESS(NOTHING,   98),
    PRESS(TRIGGERS,   2),
    PRESS(NOTHING,   48),
    PRESS(TRIGGERS,   2),
    PRESS(NOTHING,   48),
    PRESS(A,          2),
    PRESS(NOTHING,   48),
    PRESS(A,          2),
    PRESS(NOTHING,   48),
    PRESS(A,          2),
    PRESS(NOTHING,  298),
};

static const command randomDI_airdodge[] = {
    PRESS(LEFT,          1),
    PRESS(RIGHT,         1),
    PRESS(BUMPERS,       1),
};

static const command rightDI_airdodge[] = {
    PRESS(RIGHT,         1),
    PRESS(BUMPERS,       1),
};

static const command leftDI_airdodge[] = {
    PRESS(LEFT,          1),
    PRESS(BUMPERS,       1),
};

static const command* program_order[] = {
    randomDI_airdodge,
    leftDI_airdodge,
    rightDI_airdodge,
};

static const size_t program_sizes[] = {
    countof(randomDI_airdodge),
    countof(leftDI_airdodge),
    countof(rightDI_airdodge),
};

typedef enum {
    SYNC_CONTROLLER,
    SYNC_POSITION,
    BREATHE,
    PROCESS,
    CLEANUP,
    DONE
} State_t;
State_t state = SYNC_CONTROLLER;

COM_FOREVER_F(randomDI_airdodge)

/*** Debounce ****
The following is some -really bad- debounce code. I have a more robust library
that I've used in other personal projects that would be a much better use
here, especially considering that this is a stick indented for use with arcade
fighters.
This code exists solely to actually test on. This will eventually be replaced.
**** Debounce ***/
// Quick debounce hackery!
// We're going to capture each port separately and store the contents into a 32-bit value.
volatile uint32_t pb_debounce = 0;
volatile uint32_t pd_debounce = 0;

// We also need a port state capture. We'll use a 16-bit value for this.
uint16_t bd_state = 0;

// We'll also give us some useful macros here.
#define PINB_DEBOUNCED ((bd_state >> 0) & 0xFF)

// So let's do some debounce! Lazily, and really poorly.
void debounce_ports(void) {
    // We'll shift the current value of the debounce down one set of 8 bits. We'll also read in the state of the pins.
    pb_debounce = (pb_debounce << 8) + PINB;
    pd_debounce = (pd_debounce << 8) + PIND;

    // We'll then iterate through a simple for loop.
    for (int i = 0; i < 8; i++) {
        if ((pb_debounce & (0x1010101 << i)) == (0x1010101 << i)) // wat
            bd_state |= (1 << i);
        else if ((pb_debounce & (0x1010101 << i)) == (0))
            bd_state &= ~(uint16_t)(1 << i);

        if ((pd_debounce & (0x1010101 << i)) == (0x1010101 << i))
            bd_state |= (1 << (8 + i));
        else if ((pd_debounce & (0x1010101 << i)) == (0))
            bd_state &= ~(uint16_t)(1 << (8 + i));
    }
}

int main(void) {
    // We'll start by performing hardware and peripheral setup.
    SetupHardware();
    // We'll then enable global interrupts for our use.
    GlobalInterruptEnable();

    int edge = 0;
    uint16_t new_button = (~PINB_DEBOUNCED & (1 << 3));
    uint16_t previous_button = new_button;

    uint16_t program_index = 0;
    state_t state;
    new_n(&state, command_setup, countof(command_setup), 1, &randomDI_airdodge_forever, NULL);
    for (;;) {
        // We need to run our task to process and deliver data for our IN and OUT endpoints.
        HID_Task(&state);
        // We also need to run the main USB management task.
        USB_USBTask();

        // As part of this loop, we'll also run our bad debounce code.
        // Optimally, we should replace this with something that fires on a timer.
        debounce_ports();

        previous_button = new_button;
        new_button = (~PINB_DEBOUNCED & (1 << 3));

        if ((previous_button ^ new_button) && (edge = ~edge)) {
            // User button input detected. Increasing mode counter.
            program_index++;
            const command* next_coms = program_order[program_index % countof(program_order)];
            const size_t next_coms_size = program_sizes[program_index % countof(program_sizes)];
            new_loopforever(&state, next_coms, next_coms_size);
        }
    }
}

// Configures hardware and peripherals, such as the USB peripherals.
void SetupHardware(void) {
    // We need to disable watchdog if enabled by bootloader/fuses.
    MCUSR &= ~(1 << WDRF);
    wdt_disable();

    // We need to disable clock division before initializing the USB hardware.
    clock_prescale_set(clock_div_1);
    // We can then initialize our hardware and peripherals, including the USB stack.

    #ifdef ALERT_WHEN_DONE
    // Both PORTD and PORTB will be used for the optional LED flashing and buzzer.
    #warning LED and Buzzer functionality enabled. All pins on both PORTB and \
PORTD will toggle when printing is done.
    DDRD  = 0xFF; //Teensy uses PORTD
    PORTD =  0x0;
    //We'll just flash all pins on both ports since the UNO R3
    DDRB  = 0xFF; //uses PORTB. Micro can use either or, but both give us 2 LEDs
    PORTB =  0x0; //The ATmega328P on the UNO will be resetting, so unplug it?
    #endif

    // The USB stack should be initialized last.
    USB_Init();
}

// Fired to indicate that the device is enumerating.
void EVENT_USB_Device_Connect(void) {
    // We can indicate that we're enumerating here (via status LEDs, sound, etc.).
}

// Fired to indicate that the device is no longer connected to a host.
void EVENT_USB_Device_Disconnect(void) {
    // We can indicate that our device is not ready (via status LEDs, sound, etc.).
}

// Fired when the host set the current configuration of the USB device after enumeration.
void EVENT_USB_Device_ConfigurationChanged(void) {
    bool ConfigSuccess = true;

    // We setup the HID report endpoints.
    ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_OUT_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);
    ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_IN_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);

    // We can read ConfigSuccess to indicate a success or failure at this point.
}

// Process control requests sent to the device from the USB host.
void EVENT_USB_Device_ControlRequest(void) {
    // We can handle two control requests: a GetReport and a SetReport.

    // Not used here, it looks like we don't receive control request from the Switch.
}
