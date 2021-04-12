#include <application.h>

#define SERVICE_INTERVAL_INTERVAL (60 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)

#define TEMPERATURE_TAG_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define TEMPERATURE_TAG_PUB_VALUE_CHANGE 0.2f
#define TEMPERATURE_UPDATE_SERVICE_INTERVAL (5 * 1000)
#define TEMPERATURE_UPDATE_NORMAL_INTERVAL (10 * 1000)

#define HUMIDITY_TAG_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define HUMIDITY_TAG_PUB_VALUE_CHANGE 5.0f
#define HUMIDITY_TAG_UPDATE_SERVICE_INTERVAL (5 * 1000)
#define HUMIDITY_TAG_UPDATE_NORMAL_INTERVAL (10 * 1000)

#define BAROMETER_TAG_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define BAROMETER_TAG_PUB_VALUE_CHANGE 20.0f
#define BAROMETER_TAG_UPDATE_SERVICE_INTERVAL (1 * 60 * 1000)
#define BAROMETER_TAG_UPDATE_NORMAL_INTERVAL  (5 * 60 * 1000)

#define CO2_PUB_NO_CHANGE_INTERVAL (15 * 60 * 1000)
#define CO2_PUB_VALUE_CHANGE 50.0f
#define CO2_UPDATE_SERVICE_INTERVAL (1 * 60 * 1000)
#define CO2_UPDATE_NORMAL_INTERVAL  (5 * 60 * 1000)

#define CALIBRATION_DELAY (4 * 60 * 1000)
#define CALIBRATION_INTERVAL (1 * 60 * 1000)

// LED instance
twr_led_t led;

// Button instance
twr_button_t button;

// Thermometer instance
twr_tmp112_t tmp112;

// Temperature tag instance
twr_tag_temperature_t temperature;
event_param_t temperature_event_param = { .next_pub = 0 };

// Humidity tag instance
twr_tag_humidity_t humidity;
event_param_t humidity_event_param = { .next_pub = 0 };

// Barometer tag instance
twr_tag_barometer_t barometer;
event_param_t barometer_event_param = { .next_pub = 0 };

// CO2
event_param_t co2_event_param = { .next_pub = 0 };

twr_scheduler_task_id_t calibration_task_id = 0;
int calibration_counter;

void calibration_task(void *param);

void calibration_start()
{
    calibration_counter = 32;

    twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    calibration_task_id = twr_scheduler_register(calibration_task, NULL, twr_tick_get() + CALIBRATION_DELAY);
    twr_radio_pub_string("co2-meter/-/calibration", "start");
}

void calibration_stop()
{
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    twr_scheduler_unregister(calibration_task_id);
    calibration_task_id = 0;

    twr_module_co2_set_update_interval(CO2_UPDATE_NORMAL_INTERVAL);
    twr_radio_pub_string("co2-meter/-/calibration", "end");
}

void calibration_task(void *param)
{
    (void) param;

    twr_led_set_mode(&led, TWR_LED_MODE_BLINK_SLOW);

    twr_radio_pub_int("co2-meter/-/calibration", &calibration_counter);

    twr_module_co2_set_update_interval(CO2_UPDATE_SERVICE_INTERVAL);
    twr_module_co2_calibration(TWR_LP8_CALIBRATION_BACKGROUND_FILTERED);

    calibration_counter--;

    if (calibration_counter == 0)
    {
        calibration_stop();
    }

    twr_scheduler_plan_current_relative(CALIBRATION_INTERVAL);
}

void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == TWR_BUTTON_EVENT_CLICK)
    {
        static uint16_t button_count = 0;

        twr_led_pulse(&led, 100);

        twr_radio_pub_push_button(&button_count);
        button_count++;
    }
    else if (event == TWR_BUTTON_EVENT_HOLD)
    {
        if (!calibration_task_id)
        {
            calibration_start();
        }
        else
        {
            calibration_stop();
        }

    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event_param;

    float voltage;

    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (twr_module_battery_get_voltage(&voltage))
        {
            twr_radio_pub_battery(&voltage);
        }
    }
}

void temperature_tag_event_handler(twr_tag_temperature_t *self, twr_tag_temperature_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event == TWR_TAG_TEMPERATURE_EVENT_UPDATE)
    {
        if (twr_tag_temperature_get_temperature_celsius(self, &value))
        {
            if ((fabsf(value - param->value) >= TEMPERATURE_TAG_PUB_VALUE_CHANGE) || (param->next_pub < twr_scheduler_get_spin_tick()))
            {
                twr_radio_pub_temperature(param->channel, &value);
                param->value = value;
                param->next_pub = twr_scheduler_get_spin_tick() + TEMPERATURE_TAG_PUB_NO_CHANGE_INTEVAL;
            }
        }
    }
}

void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != TWR_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (twr_tag_humidity_get_humidity_percentage(self, &value))
    {
        if ((fabsf(value - param->value) >= HUMIDITY_TAG_PUB_VALUE_CHANGE) || (param->next_pub < twr_scheduler_get_spin_tick()))
        {
            twr_radio_pub_humidity(param->channel, &value);
            param->value = value;
            param->next_pub = twr_scheduler_get_spin_tick() + HUMIDITY_TAG_PUB_NO_CHANGE_INTEVAL;
        }
    }
}

void barometer_tag_event_handler(twr_tag_barometer_t *self, twr_tag_barometer_event_t event, void *event_param)
{
    float pascal;
    float meter;
    event_param_t *param = (event_param_t *)event_param;

    if (event != TWR_TAG_BAROMETER_EVENT_UPDATE)
    {
        return;
    }

    if (!twr_tag_barometer_get_pressure_pascal(self, &pascal))
    {
        return;
    }

    if ((fabsf(pascal - param->value) >= BAROMETER_TAG_PUB_VALUE_CHANGE) || (param->next_pub < twr_scheduler_get_spin_tick()))
    {

        if (!twr_tag_barometer_get_altitude_meter(self, &meter))
        {
            return;
        }

        twr_radio_pub_barometer(param->channel, &pascal, &meter);
        param->value = pascal;
        param->next_pub = twr_scheduler_get_spin_tick() + BAROMETER_TAG_PUB_NO_CHANGE_INTEVAL;
    }
}

void co2_event_handler(twr_module_co2_event_t event, void *event_param)
{
    event_param_t *param = (event_param_t *) event_param;
    float value;

    if (event == TWR_MODULE_CO2_EVENT_ERROR)
    {
        twr_lp8_error_t error;
        twr_module_co2_get_error(&error);
        int error_int = (int)error;

        twr_radio_pub_int("co2-meter/-/error", &error_int);
    }

    if (event == TWR_MODULE_CO2_EVENT_UPDATE)
    {
        if (twr_module_co2_get_concentration_ppm(&value))
        {
            if ((fabsf(value - param->value) >= CO2_PUB_VALUE_CHANGE) || (param->next_pub < twr_scheduler_get_spin_tick()) || calibration_task_id)
            {
                twr_radio_pub_co2(&value);
                param->value = value;
                param->next_pub = twr_scheduler_get_spin_tick() + CO2_PUB_NO_CHANGE_INTERVAL;
            }
        }
    }
}

void switch_to_normal_mode_task(void *param)
{
    twr_tag_temperature_set_update_interval(&temperature, TEMPERATURE_UPDATE_NORMAL_INTERVAL);

    twr_tag_humidity_set_update_interval(&humidity, HUMIDITY_TAG_UPDATE_NORMAL_INTERVAL);

    twr_tag_barometer_set_update_interval(&barometer, BAROMETER_TAG_UPDATE_NORMAL_INTERVAL);

    twr_module_co2_set_update_interval(CO2_UPDATE_NORMAL_INTERVAL);

    twr_scheduler_unregister(twr_scheduler_get_current_task_id());
}

void application_init(void)
{
    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    twr_radio_init(TWR_RADIO_MODE_NODE_SLEEPING);

    // Initialize thermometer sensor on core module
    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_hold_time(&button, 10000);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize temperature
    temperature_event_param.channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    twr_tag_temperature_init(&temperature, TWR_I2C_I2C0, TWR_TAG_TEMPERATURE_I2C_ADDRESS_DEFAULT);
    twr_tag_temperature_set_update_interval(&temperature, TEMPERATURE_UPDATE_SERVICE_INTERVAL);
    twr_tag_temperature_set_event_handler(&temperature, temperature_tag_event_handler, &temperature_event_param);

    // Initialize humidity
    humidity_event_param.channel = TWR_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT;
    twr_tag_humidity_init(&humidity, TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C0, TWR_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);
    twr_tag_humidity_set_update_interval(&humidity, HUMIDITY_TAG_UPDATE_SERVICE_INTERVAL);
    twr_tag_humidity_set_event_handler(&humidity, humidity_tag_event_handler, &humidity_event_param);

    // Initialize barometer
    barometer_event_param.channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    twr_tag_barometer_init(&barometer, TWR_I2C_I2C0);
    twr_tag_barometer_set_update_interval(&barometer, BAROMETER_TAG_UPDATE_SERVICE_INTERVAL);
    twr_tag_barometer_set_event_handler(&barometer, barometer_tag_event_handler, &barometer_event_param);

    // Initialize CO2
    twr_module_co2_init();
    twr_module_co2_set_update_interval(CO2_UPDATE_SERVICE_INTERVAL);
    twr_module_co2_set_event_handler(co2_event_handler, &co2_event_param);

    twr_radio_pairing_request("co2-monitor", VERSION);

    twr_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);

    twr_led_pulse(&led, 2000);
}
