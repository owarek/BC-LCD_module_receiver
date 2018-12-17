#include <application.h>
#include <bc_eeprom.h>
#include <bc_spi.h>
#include <bc_dice.h>

#define SERVICE_INTERVAL_INTERVAL (60 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)

#define TEMPERATURE_TAG_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)
#define TEMPERATURE_TAG_PUB_VALUE_CHANGE 0.2f
#define TEMPERATURE_UPDATE_SERVICE_INTERVAL (5 * 1000)
#define TEMPERATURE_UPDATE_NORMAL_INTERVAL (10 * 1000)

event_param_t temperature_event_param = { .next_pub = 0, .value = NAN };

bc_scheduler_task_id_t appTaskID = 0;

// LED instance
bc_led_t led;
bc_led_t led_lcd_red;
bc_led_t led_lcd_blue;

// Thermometer instance
bc_tmp112_t tmp112;

float flt_temperature;

// Button instance
bc_button_t button;

#define COLOR_BLACK true
#define APPLICATION_TASK_ID 0

#if ROTATE_SUPPORT
bc_lis2dh12_t lis2dh12;
bc_dice_t dice;
bc_dice_face_t face = BC_DICE_FACE_UNKNOWN;
bc_module_lcd_rotation_t rotation = BC_MODULE_LCD_ROTATION_0;
#endif

#if CORE_R == 2
bc_module_lcd_rotation_t face_2_lcd_rotation_lut[7] =
{
    [BC_DICE_FACE_2] = BC_MODULE_LCD_ROTATION_270,
    [BC_DICE_FACE_3] = BC_MODULE_LCD_ROTATION_180,
    [BC_DICE_FACE_4] = BC_MODULE_LCD_ROTATION_0,
    [BC_DICE_FACE_5] = BC_MODULE_LCD_ROTATION_90
};
#else
bc_module_lcd_rotation_t face_2_lcd_rotation_lut[7] =
{
    [BC_DICE_FACE_2] = BC_MODULE_LCD_ROTATION_90,
    [BC_DICE_FACE_3] = BC_MODULE_LCD_ROTATION_0,
    [BC_DICE_FACE_4] = BC_MODULE_LCD_ROTATION_180,
    [BC_DICE_FACE_5] = BC_MODULE_LCD_ROTATION_270
};
#endif

void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != BC_TMP112_EVENT_UPDATE)
    {
        return;
    }

    if (bc_tmp112_get_temperature_celsius(self, &value))
    {
        if ((fabsf(value - param->value) >= TEMPERATURE_TAG_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
        {
            bc_radio_pub_temperature(BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_ALTERNATE, &value);
            param->value = value;
            param->next_pub = bc_scheduler_get_spin_tick() + TEMPERATURE_TAG_PUB_NO_CHANGE_INTERVAL;
            //bc_log_debug("Internal temperature: %f", value);
        }
    }
    else
    {
        param->value = NAN;
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event_param;

    float voltage;

    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (bc_module_battery_get_voltage(&voltage))
        {
            bc_radio_pub_battery(&voltage);
            //bc_log_debug("Battery voltage: %f", voltage);
        }
    }
}

void switch_to_normal_mode_task(void *param)
{
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_NORMAL_INTERVAL);

    bc_scheduler_unregister(bc_scheduler_get_current_task_id());
}

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_led_set_mode(&led, BC_LED_MODE_TOGGLE);
    }

    // Logging in action
    //bc_log_info("Button event handler - event: %i", event);
}

void lcd_message_set(uint64_t *id, const char *topic, void *value, void *param);

// subscribe table, format: topic, expect payload type, callback, user param
static const bc_radio_sub_t subs[] = {
    // state/set
    {"barometer/-/interval/set", BC_RADIO_SUB_PT_FLOAT  , lcd_message_set, NULL }
};

void lcd_message_set(uint64_t *id, const char *topic, void *value, void *param)
{
    //uint8_t angle = *(int*)value;
    flt_temperature = *(float*)value;
    //bc_log_debug("Recieved payload: %f", *(float*)value);
    bc_scheduler_plan_now (appTaskID);
}

void on_lcd_button_click(void)
{
    //radio_pub_set_temperature();

    // Save set temperature to eeprom
    //uint32_t neg_set_temperature;
    //float *set_temperature = (float *) &neg_set_temperature;

    //*set_temperature = temperature_set_point.value;

    //neg_set_temperature = ~neg_set_temperature;

    //bc_eeprom_write(EEPROM_SET_TEMPERATURE_ADDRESS, &temperature_set_point.value, sizeof(temperature_set_point.value));
    //bc_eeprom_write(EEPROM_SET_TEMPERATURE_ADDRESS + sizeof(temperature_set_point.value), &neg_set_temperature, sizeof(neg_set_temperature));

    bc_scheduler_plan_now(APPLICATION_TASK_ID);
}

void lcd_button_left_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_CLICK)
    {

        //temperature_set_point.value -= SET_TEMPERATURE_ADD_ON_CLICK;

        static uint16_t left_event_count = 0;

        left_event_count++;

        bc_radio_pub_event_count(BC_RADIO_PUB_EVENT_LCD_BUTTON_LEFT, &left_event_count);

        bc_led_pulse(&led_lcd_blue, 30);

        on_lcd_button_click();
    }
}

void lcd_button_right_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) event_param;

    if (event == BC_BUTTON_EVENT_CLICK)
    {

        //temperature_set_point.value += SET_TEMPERATURE_ADD_ON_CLICK;

        static uint16_t right_event_count = 0;

        right_event_count++;

        bc_radio_pub_event_count(BC_RADIO_PUB_EVENT_LCD_BUTTON_RIGHT, &right_event_count);

        bc_led_pulse(&led_lcd_red, 30);

        on_lcd_button_click();
    }
}

#if ROTATE_SUPPORT
void lis2dh12_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    (void) event_param;

    if (event == BC_LIS2DH12_EVENT_UPDATE)
    {
        bc_lis2dh12_result_g_t result;

        bc_lis2dh12_get_result_g(self, &result);

        bc_dice_feed_vectors(&dice, result.x_axis, result.y_axis, result.z_axis);

        face = bc_dice_get_face(&dice);

        if (face > BC_DICE_FACE_1 && face < BC_DICE_FACE_6)
        {
            rotation = face_2_lcd_rotation_lut[face];

            bc_scheduler_plan_now(APPLICATION_TASK_ID);
        }
    }
}
#endif

void application_init(void)
{
    // Initialize logging
    //bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Radio init
    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);
    bc_radio_set_rx_timeout_for_sleeping_node(2000);
    bc_radio_set_subs((bc_radio_sub_t *) subs, sizeof(subs)/sizeof(bc_radio_sub_t));

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize thermometer sensor on core module
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, tmp112_event_handler, &temperature_event_param);
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_SERVICE_INTERVAL);

    // Initialize LCD
    bc_module_lcd_init();

    // Initialize LCD button left
    static bc_button_t lcd_left;
    bc_button_init_virtual(&lcd_left, BC_MODULE_LCD_BUTTON_LEFT, bc_module_lcd_get_button_driver(), false);
    bc_button_set_event_handler(&lcd_left, lcd_button_left_event_handler, NULL);

    // Initialize LCD button right
    static bc_button_t lcd_right;
    bc_button_init_virtual(&lcd_right, BC_MODULE_LCD_BUTTON_RIGHT, bc_module_lcd_get_button_driver(), false);
    bc_button_set_event_handler(&lcd_right, lcd_button_right_event_handler, NULL);

    // Initialize red and blue LED on LCD module
    bc_led_init_virtual(&led_lcd_red, BC_MODULE_LCD_LED_RED, bc_module_lcd_get_led_driver(), true);
    bc_led_init_virtual(&led_lcd_blue, BC_MODULE_LCD_LED_BLUE, bc_module_lcd_get_led_driver(), true);

    #if ROTATE_SUPPORT
    // Initialize Accelerometer
    bc_dice_init(&dice, BC_DICE_FACE_UNKNOWN);
    bc_lis2dh12_init(&lis2dh12, BC_I2C_I2C0, 0x19);
    bc_lis2dh12_set_update_interval(&lis2dh12, 5 * 1000);
    bc_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    #endif

    bc_radio_pairing_request("lcd",VERSION);
    bc_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);
}

void application_task(void)
{
    static char str_temperature[10];
    // Logging in action
    //bc_log_debug("application_task run");

    if (!bc_module_lcd_is_ready())
    {
        return;
    }
    bc_module_lcd_clear();

    bc_module_lcd_set_font(&bc_font_ubuntu_33);
    snprintf(str_temperature, sizeof(str_temperature), "%.1f   ", flt_temperature);

    int x = bc_module_lcd_draw_string(20, 20, str_temperature, COLOR_BLACK);

    bc_module_lcd_set_font(&bc_font_ubuntu_24);
    bc_module_lcd_draw_string(x - 20, 25, "\xb0" "C   ", COLOR_BLACK);

    bc_module_lcd_set_font(&bc_font_ubuntu_24);
    //bc_module_lcd_draw_string(x - 20, 25, "\xb0" "C   ", COLOR_BLACK);

    bc_module_lcd_set_font(&bc_font_ubuntu_15);
    bc_module_lcd_draw_string(10, 80, "Outside temp", COLOR_BLACK);

    bc_module_lcd_update();

    // Plan next run this function after 10 minutes
    bc_scheduler_plan_current_from_now(10 * 60000);
    appTaskID = bc_scheduler_get_current_task_id();
}
