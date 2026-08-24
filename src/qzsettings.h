#ifndef QZSETTINGS_H
#define QZSETTINGS_H

#include <QString>

class QZSettings {
  private:
    QZSettings() {}

  public:
    //--------------------------------------------------------------------------------------------
    // These are not in settings.qml
    //--------------------------------------------------------------------------------------------

    static const QString cryptoKeySettingsProfiles;
    static constexpr int default_cryptoKeySettingsProfiles = 0;
    /**
     *@brief Disable (true) reconnection when the device disconnects from Bluetooth.
     */
    static const QString bluetooth_no_reconnection;
    static constexpr bool default_bluetooth_no_reconnection = false;

    /**
     *@brief Choose between wheel revolutions (true) and wheel and crank revolutions (false)
     *when configuring the CSC feature BLE characteristic.
     */
    static const QString bike_wheel_revs;
    static constexpr bool default_bike_wheel_revs = false;

    static const QString bluetooth_lastdevice_name;
    static const QString default_bluetooth_lastdevice_name;

    static const QString bluetooth_lastdevice_address;
    static const QString default_bluetooth_lastdevice_address;

    static const QString hrm_lastdevice_name;
    static const QString default_hrm_lastdevice_name;

    static const QString hrm_lastdevice_address;
    static const QString default_hrm_lastdevice_address;

    static const QString csc_sensor_address;
    static const QString default_csc_sensor_address;

    static const QString csc_sensor_lastdevice_name;
    static const QString default_csc_sensor_lastdevice_name;

    static const QString power_sensor_lastdevice_name;
    static const QString default_power_sensor_lastdevice_name;

    static const QString power_sensor_address;
    static const QString default_power_sensor_address;

    static const QString elite_rizer_lastdevice_name;
    static const QString default_elite_rizer_lastdevice_name;

    static const QString elite_rizer_address;
    static const QString default_elite_rizer_address;

    static const QString elite_sterzo_smart_lastdevice_name;
    static const QString default_elite_sterzo_smart_lastdevice_name;

    static const QString elite_sterzo_smart_address;
    static const QString default_elite_sterzo_smart_address;

    static const QString code;
    static const QString default_code;

    //--------------------------------------------------------------------------------------------

    /**
     *@brief Preferred application language. Use "auto" to follow system locale.
     */
    static const QString app_language;
    static const QString default_app_language;

    /**
     *@brief Disable (true) or use (false) the device's heart rate service.
     */
    static const QString bike_heartrate_service;
    static constexpr bool default_bike_heartrate_service = false;

    /**
     *@brief An offset that can be applied to the resistance from the device.
     * calculated_resistance = raw_resistance * bike_resistance_gain_f + bike_resistance_offset
     */
    static const QString bike_resistance_offset;
    static constexpr int default_bike_resistance_offset = 4;

    /**
     *@brief A gain that can be applied to the resistance from the device.
     * calculated_resistance = raw_resistance * bike_resistance_gain_f + bike_resistance_offset
     */
    static const QString bike_resistance_gain_f;
    static constexpr float default_bike_resistance_gain_f = 1.0;

    /**
     *@brief Used to specify of QZ is using Zwift in ERG (workout) Mode.
     * When supporting this, QZ should communicate the target resistance
     * (or automatically adjust the device's resistance if it has this capability) to match the target
     * watts based on the cadence (RPM). In ERG Mode, the changes in inclination should not affect target resistance,
     * as is the case in Simulation Mode. Default is false.
     *
     */
    static const QString zwift_erg;
    static constexpr bool default_zwift_erg = false;

    /**
     *@brief In ERG Mode, Zwift sends a “target output” request. If the output requested doesn’t match the current
     *output (calculated using cadence and resistance level), the target resistance should change to help the user get
     *closer to the target output. If the filter is set to higher values, there should be less adjustment of the target
     *resistance and cadence would need to be increased to match the target output. The zwift_erg_filter and
     *zwift_erg_filter_down settings are the upper and lower margin before the adjustment of resistance is communicated.
     *Example: if the zwift_erg_filter and zwift_erg_filter_down filters are set to 10 and the target output is 100
     *watts, a change of resistance will only be communicated if the device produces less than 90 Watts or more than 110
     *Watts.
     */
    static const QString zwift_erg_filter;
    static constexpr float default_zwift_erg_filter = 10.0;

    /**
     *@brief In ERG Mode, Zwift sends a “target output” request. If the output requested doesn’t match the current
     *output (calculated using cadence and resistance level), the target resistance should change to help the user get
     *closer to the target output. If the filter is set to higher values, there should be less adjustment of the target
     *resistance and cadence would need to be increased to match the target output. The zwift_erg_filter and
     *zwift_erg_filter_down settings are the upper and lower margin before the adjustment of resistance is communicated.
     *Example: if the zwift_erg_filter and zwift_erg_filter_down filters are set to 10 and the target output is 100
     *watts, a change of resistance will only be communicated if the device produces less than 90 Watts or more than 110
     *Watts.
     */
    static const QString zwift_erg_filter_down;
    static constexpr float default_zwift_erg_filter_down = 10.0;

    /**
     *@brief Used to invoke a workaround whereby negative inclination is multiplied by 2.
     */
    static const QString zwift_negative_inclination_x2;
    static constexpr bool default_zwift_negative_inclination_x2 = false;

    /**
     *@brief An offset that will be applied to the inclination received from the client application.
     * calculated_inclination = raw_inclination * zwift_inclination_gain + zwift_inclination_offset
     */
    static const QString zwift_inclination_offset;
    static constexpr float default_zwift_inclination_offset = 0;

    /**
     *@brief A gain that will be applied to the inclination received from the client application.
     * calculated_inclination = raw_inclination * zwift_inclination_gain + zwift_inclination_offset
     */
    static const QString zwift_inclination_gain;
    static constexpr float default_zwift_inclination_gain = 1.0;

    static const QString echelon_resistance_offset;
    static constexpr float default_echelon_resistance_offset = 0;

    static const QString echelon_resistance_gain;
    static constexpr float default_echelon_resistance_gain = 1.0;

    /**
     *@brief Used for some devices to specify that speed should be calculated from power.
     */
    static const QString speed_power_based;
    static constexpr bool default_speed_power_based = false;

    /**
     *@brief The resistance to be set when a bike or elliptical trainer first connects.
     */
    static const QString bike_resistance_start;
    static constexpr int default_bike_resistance_start = 1;

    /**
     *@brief The age of the user in years.
     */
    static const QString age;
    static constexpr int default_age = 35.0;

    /**
     *@brief The mass of the user in kilograms. Used for power calculations.
     */
    static const QString weight;
    static constexpr float default_weight = 75.0;

    static const QString user_nickname;
    static const QString default_user_nickname;

    /**
     *@brief Specifies whether or not to use miles (false) or kilometers (true) as the unit of distance.
     */
    static const QString miles_unit;
    static constexpr bool default_miles_unit = false;

    /**
     *@brief Flag to indicate if it should be ignored (true) that the user has stopped doing work.
     */
    static const QString continuous_moving;
    static constexpr bool default_continuous_moving = false;

    static const QString bike_cadence_sensor;
    static constexpr bool default_bike_cadence_sensor = false;

    static const QString rogue_echo_bike;
    static constexpr bool default_rogue_echo_bike = false;

    static const QString bike_power_sensor;
    static constexpr bool default_bike_power_sensor = false;

    static const QString bike_power_offset;
    static constexpr int default_bike_power_offset = 0;

    static const QString heart_rate_belt_name;
    static const QString default_heart_rate_belt_name;

    /**
     *@brief Used to ignore the heart rate from some devices.
     */
    static const QString heart_ignore_builtin;
    static constexpr bool default_heart_ignore_builtin = false;

    static const QString ant_cadence;
    static constexpr bool default_ant_cadence = false;

    static const QString ant_heart;
    static constexpr bool default_ant_heart = false;

    static const QString peloton_heartrate_metric;
    static const QString default_peloton_heartrate_metric;

    static const QString tile_avg_pace_enabled;
    static constexpr bool default_tile_avg_pace_enabled = false;

    static const QString tile_avg_pace_order;
    static constexpr int default_tile_avg_pace_order = 76;

    static const QString peloton_gain;
    static constexpr float default_peloton_gain = 1.0;

    static const QString peloton_offset;
    static constexpr float default_peloton_offset = 0;

    /**
     *@brief 1 mile time goal, for a training program with the speed control.
     */
    /**
     *@brief 5 km time goal, for a training program with the speed control.
     */
    /**
     *@brief 10 km time goal, for a training program with the speed control.
     */
    /**
     *@brief  pacef_1mile, but for half-marathon distance, for a training program with the speed control.
     */

    static const QString hammer_racer_s;
    static constexpr bool default_hammer_racer_s = false;

    static const QString proform_treadmill_995i;
    static constexpr bool default_proform_treadmill_995i = false;
    static const QString nordictrack_series_7;
    static constexpr bool default_nordictrack_series_7 = false;
    static const QString nordictrack_se7i;
    static constexpr bool default_nordictrack_se7i = false;

    static const QString toorx_ftms;
    static constexpr bool default_toorx_ftms = false;

    static const QString flywheel_life_fitness_ic8;
    static constexpr bool default_flywheel_life_fitness_ic8 = false;

    static const QString schwinn_bike_resistance;
    static constexpr bool default_schwinn_bike_resistance = false;

    static const QString schwinn_bike_resistance_v2;
    static constexpr bool default_schwinn_bike_resistance_v2 = false;

    static const QString gym_mode;
    static constexpr bool default_gym_mode = false;

    /**
     * @brief Adjusts value in a metric object that's configured specifically for measuring WATTS.
     */
    static const QString watt_offset;
    static constexpr float default_watt_offset = 0;

    /**
     * @brief Adjusts value in a metric object that's configured specifically for measuring WATTS.
     */
    static const QString watt_gain;
    static constexpr float default_watt_gain = 1;

    static const QString power_avg_5s;
    static constexpr bool default_power_avg_5s = false;

    static const QString power_avg_3s;
    static constexpr bool default_power_avg_3s = false;

    static const QString instant_power_on_pause;
    static constexpr bool default_instant_power_on_pause = false;

    static const QString toputure_teb1;
    static constexpr bool default_toputure_teb1 = false;

    /**
     * @brief Adjusts value in a metric object that's configured specifically for measuring SPEED.
     */
    static const QString speed_offset;
    static constexpr float default_speed_offset = 0;

    /**
     * @brief Adjusts value in a metric object that's configured specifically for measuring SPEED.
     */
    static const QString speed_gain;
    static constexpr float default_speed_gain = 1;

    static const QString filter_device;
    static const QString default_filter_device;

    static const QString cadence_sensor_name;
    static const QString default_cadence_sensor_name;

    static const QString cadence_sensor_as_bike;
    static constexpr bool default_cadence_sensor_as_bike = false;

    static const QString cadence_sensor_speed_ratio;
    static constexpr float default_cadence_sensor_speed_ratio = 0.33;

    static const QString cscbike_custom_resistance_power_table;
    static constexpr bool default_cscbike_custom_resistance_power_table = false;

    static const QString cscbike_custom_resistance_level_1;
    static constexpr float default_cscbike_custom_resistance_level_1 = 1;

    static const QString cscbike_custom_watt_1;
    static constexpr float default_cscbike_custom_watt_1 = 100;

    static const QString cscbike_custom_resistance_level_2;
    static constexpr float default_cscbike_custom_resistance_level_2 = 15;

    static const QString cscbike_custom_watt_2;
    static constexpr float default_cscbike_custom_watt_2 = 300;

    static const QString power_hr_pwr1;
    static constexpr float default_power_hr_pwr1 = 200;

    static const QString power_hr_hr1;
    static constexpr float default_power_hr_hr1 = 150;

    static const QString power_hr_pwr2;
    static constexpr float default_power_hr_pwr2 = 230;

    static const QString power_hr_hr2;
    static constexpr float default_power_hr_hr2 = 170;

    static const QString power_sensor_name;
    static const QString default_power_sensor_name;

    static const QString powr_sensor_running_cadence_double;
    static constexpr bool default_powr_sensor_running_cadence_double = false;

    static const QString elite_rizer_name;
    static const QString default_elite_rizer_name;

    static const QString elite_sterzo_smart_name;
    static const QString default_elite_sterzo_smart_name;

    static const QString fitmetria_fanfit_enable;
    static constexpr bool default_fitmetria_fanfit_enable = false;

    static const QString fitmetria_fanfit_mode;
    static const QString default_fitmetria_fanfit_mode;

    static const QString fitmetria_fanfit_min;
    static constexpr float default_fitmetria_fanfit_min = 0;

    static const QString fitmetria_fanfit_max;
    static constexpr float default_fitmetria_fanfit_max = 100;
    /**
     *@brief Indicates if the virtual device should send resistance requests to the bike.
     */
    static const QString virtualbike_forceresistance;
    static constexpr bool default_virtualbike_forceresistance = true;
    /**
     *@brief Troubleshooting setting. Should be false unless advised by QZ tech support.
     */
    static const QString bluetooth_relaxed;
    static constexpr bool default_bluetooth_relaxed = false;
    /**
     *@brief Troubleshooting setting. Should be false unless advised by QZ tech support.
     */
    static const QString bluetooth_30m_hangs;
    static constexpr bool default_bluetooth_30m_hangs = false;

    static const QString battery_service;
    static constexpr bool default_battery_service = false;

    /**
     *@brief Experimental feature. Not recommended to use.
     */
    static const QString service_changed;
    static constexpr bool default_service_changed = false;

    /**
     *@brief Enable/disable the virtual device that connects QZ to the client app.
     */
    static const QString virtual_device_enabled;
    static constexpr bool default_virtual_device_enabled = true;
    /**
     *@brief Enable/disable the Bluetooth connectivity of the virtual device that connects QZ to the client app.
     */
    static const QString virtual_device_bluetooth;
    static constexpr bool default_virtual_device_bluetooth = true;

    static const QString ios_peloton_workaround;
    static constexpr bool default_ios_peloton_workaround = true;

    static const QString android_wakelock;
    static constexpr bool default_android_wakelock = true;
    /**
     *@brief Specifies if the debug log file will be written.
     */
    static const QString log_debug;
    static constexpr bool default_log_debug = false;
    /**
     *@brief Force QZ to communicate ONLY the Heart Rate metric to third-party apps.
     */
    static const QString virtual_device_onlyheart;
    static constexpr bool default_virtual_device_onlyheart = false;
    /**
     *@brief Enables QZ to communicate with the Echelon app.
     *This setting can only be used with iOS running QZ and iOS running the Echelon app.
     */
    static const QString virtual_device_echelon;
    static constexpr bool default_virtual_device_echelon = false;
    /**
     *@brief Enables a virtual bluetooth bridge to the iFit App.
     */
    static const QString virtual_device_ifit;
    static constexpr bool default_virtual_device_ifit = false;
    /**
     *@brief Instructs QZ to send a rower Bluetooth profile instead of a bike profile to third party apps that support
     *rowing (examples: Kinomap and BitGym). This should be off for Zwift.
     */
    /**
     *@brief When virtual_device_rower is enabled, use the Concept2 PM5 protocol instead of FTMS.
     * This enables compatibility with apps like Mywhoosh that only support PM5 rowers.
     */
    /**
     *@brief Used to force a non-bike device to be presented to client apps as a bike.
     */
    static const QString volume_change_gears;
    static constexpr bool default_volume_change_gears = false;

    /**
     *@brief Minimum target resistance for ERG mode.
     */
    static const QString zwift_erg_resistance_down;
    static constexpr float default_zwift_erg_resistance_down = 0.0;

    /**
     *@brief Maximum targe resistance for ERG mode.
     */
    static const QString zwift_erg_resistance_up;
    static constexpr float default_zwift_erg_resistance_up = 999.0;

    // from version 2.10.23
    // not used anymore because it's an elliptical not a treadmill. Don't remove this
    // it will cause corruption in the settings
    // static const QString nordictrack_fs5i_treadmill;
    // static constexpr bool default_nordictrack_fs5i_treadmill = false;

    static const QString elite_rizer_gain;
    static constexpr float default_elite_rizer_gain = 1.0;

    /**
     *@brief Enable the Wahoo Dircon device.
     */
    static const QString dircon_yes;
    static constexpr bool default_dircon_yes = true;

    static const QString dircon_server_base_port;
    static constexpr int default_dircon_server_base_port = 36866;

    static const QString ios_cache_heart_device;
    static constexpr bool default_ios_cache_heart_device = true;

    /**
     *@brief Count of the number of times the app has been opened.
     */
    static const QString app_opening;
    static constexpr int default_app_opening = 0;

    /**
     *@brief The mass of the bike in kilograms.
     */
    static const QString bike_weight;
    static constexpr float default_bike_weight = 0;

    /**
     *@brief The gender of the user.
     */
    static const QString sex;
    static const QString default_sex;

    /**
     *@brief The IP address for the Proform Treadmill.
     */
    // from version 2.11.22
    /**
     *@brief The number of seconds to add to the video timestamp.
     */

    static const bool default_nordictrack_gx_2_7 = false;

    static const QString rolling_resistance;
    static constexpr double default_rolling_resistance = 0.005;

    static const QString wahoo_rgt_dircon;
    static constexpr bool default_wahoo_rgt_dircon = false;

    static const QString wahoo_without_wheel_diameter;
    static constexpr bool default_wahoo_without_wheel_diameter = false;

    static const QString CRRGain;
    static constexpr double default_CRRGain = 0;

    static const QString CWGain;
    static constexpr double default_CWGain = 0;

    static const QString android_notification;
    static constexpr bool default_android_notification = false;

    static const QString gears_restore_value;
    static constexpr bool default_gears_restore_value = false;

    static const QString gears_current_value;
    static constexpr double default_gears_current_value = 0;

    static const QString peloton_workout_ocr;
    static constexpr bool default_peloton_workout_ocr = false;

    static const QString peloton_bike_ocr;
    static constexpr bool default_peloton_bike_ocr = false;

    static const QString zwift_ocr;
    static constexpr bool default_zwift_ocr = false;

    static const QString garmin_companion;
    static constexpr bool default_garmin_companion = false;

    static constexpr bool default_companion_peloton_workout_ocr = false;

    static const QString gears_gain;
    static constexpr double default_gears_gain = 1.0;

    static const QString gears_custom_table_enabled;
    static constexpr bool default_gears_custom_table_enabled = false;

    static const QString gears_custom_table;
    static const QString default_gears_custom_table;

    static const QString poll_device_time;
    static constexpr int default_poll_device_time = 200;

    static const QString watt_ignore_builtin;
    static constexpr bool default_watt_ignore_builtin = true;

    static const QString ftms_bike;
    static const QString default_ftms_bike;

    static const QString race_mode;
    static constexpr bool default_race_mode = false;

    static const QString saris_trainer;
    static constexpr bool default_saris_trainer = false;

    static const QString garmin_bluetooth_compatibility;
    static constexpr bool default_garmin_bluetooth_compatibility = false;

    static const QString android_documents_folder;
    static constexpr bool default_android_documents_folder = false;

    static const QString zwift_click;
    static constexpr bool default_zwift_click = false;

    static const QString thinkrider_controller;
    static constexpr bool default_thinkrider_controller = false;

    static const QString cycplus_bc2_controller;
    static constexpr bool default_cycplus_bc2_controller = false;

    static const QString zwift_play;
    static constexpr bool default_zwift_play = false;

    static const QString zwift_play_vibration;
    static constexpr bool default_zwift_play_vibration = true;

    static const QString ergDataPoints;
    static const QString default_ergDataPoints;

    static const QString dircon_id;
    static constexpr int default_dircon_id = 0;

    static const QString rouvy_compatibility;
    static constexpr bool default_rouvy_compatibility = false;

    static const QString domyosbike_notfmts;
    static constexpr bool default_domyosbike_notfmts = false;

    static const QString gears_volume_debouncing;
    static constexpr bool default_gears_volume_debouncing = false;

    static const QString zwiftplay_swap;
    static constexpr bool default_zwiftplay_swap = false;

    static const QString gears_zwift_ratio;
    static constexpr bool default_gears_zwift_ratio = false;

    static const QString gears_offset;
    static constexpr double default_gears_offset = 0.0;

    static const QString force_resistance_instead_inclination;
    static constexpr bool default_force_resistance_instead_inclination = false;

    static const QString zwift_play_emulator;
    static constexpr bool default_zwift_play_emulator = false;

    static const QString gear_crankset_size;
    static constexpr int default_gear_crankset_size = 42;

    static const QString gear_cog_size;
    static constexpr int default_gear_cog_size = 14;

    static const QString gear_circumference;
    static constexpr double default_gear_circumference = 2070.0;

    static const QString watt_bike_emulator;
    static constexpr bool default_watt_bike_emulator = false;

    static const QString restore_specific_gear;
    static constexpr bool default_restore_specific_gear = false;

    static const QString min_inclination;
    static constexpr double default_min_inclination = -999.0;

    static const QString sram_axs_controller;
    static constexpr bool default_sram_axs_controller = false;

    static const QString zwift_gear_ui_aligned;
    static constexpr bool default_zwift_gear_ui_aligned = false;

    static const QString inclinationResistancePoints;
    static const QString default_inclinationResistancePoints;

    // Climb Profile Settings
    // Sprint Profile Settings

    /**
     * @brief Metric shown on the leading side of the iOS Dynamic Island compact Live Activity.
     */
    static const QString ios_live_activity_compact_leading_metric;
    static const QString default_ios_live_activity_compact_leading_metric;

    /**
     * @brief Metric shown on the trailing side of the iOS Dynamic Island compact Live Activity.
     */
    static const QString ios_live_activity_compact_trailing_metric;
    static const QString default_ios_live_activity_compact_trailing_metric;

   /**
     * @brief Calculate only active calories (exclude basal metabolic rate)
     */
    static const QString calories_active_only;
    static constexpr bool default_calories_active_only = false;

    /**
     * @brief Calculate calories from heart rate instead of power
     */
    static const QString calories_from_hr;
    static constexpr bool default_calories_from_hr = false;

    /**
     * @brief User height in centimeters for BMR calculation
     */
    static const QString height;
    static constexpr double default_height = 175.0;

    static const QString nordictrack_vr21;
    static constexpr bool default_nordictrack_vr21 = false;

    // MyWhoosh Link Options
    /**
     * @brief Enable MyWhoosh Link server for sending control commands to MyWhoosh app
     */
    static const QString mywhoosh_link_enabled;
    static constexpr bool default_mywhoosh_link_enabled = false;

    /**
     * @brief Override local gear changes when MyWhoosh Link is enabled (true = only send to MyWhoosh, false = both)
     */
    static const QString mywhoosh_link_override_gears;
    static constexpr bool default_mywhoosh_link_override_gears = false;

    // Left Controller Button Mappings (0=Disabled, 1=GearUp, 2=GearDown, 3=SteerLeft, 4=SteerRight, 5=UTurn, 6=CameraAngle, 7=Emote, 8=Tuck)
    static const QString mywhoosh_link_left_up;
    static constexpr int default_mywhoosh_link_left_up = 1; // GearUp

    static const QString mywhoosh_link_left_down;
    static constexpr int default_mywhoosh_link_left_down = 2; // GearDown

    static const QString mywhoosh_link_left_left;
    static constexpr int default_mywhoosh_link_left_left = 0; // Disabled

    static const QString mywhoosh_link_left_right;
    static constexpr int default_mywhoosh_link_left_right = 0; // Disabled

    static const QString mywhoosh_link_left_shoulder;
    static constexpr int default_mywhoosh_link_left_shoulder = 5; // UTurn

    static const QString mywhoosh_link_left_power;
    static constexpr int default_mywhoosh_link_left_power = 0; // Disabled

    // Right Controller Button Mappings
    static const QString mywhoosh_link_right_y;
    static constexpr int default_mywhoosh_link_right_y = 6; // CameraAngle

    static const QString mywhoosh_link_right_a;
    static constexpr int default_mywhoosh_link_right_a = 0; // Disabled

    static const QString mywhoosh_link_right_b;
    static constexpr int default_mywhoosh_link_right_b = 7; // Emote

    static const QString mywhoosh_link_right_z;
    static constexpr int default_mywhoosh_link_right_z = 0; // Disabled

    static const QString mywhoosh_link_right_shoulder;
    static constexpr int default_mywhoosh_link_right_shoulder = 0; // Disabled

    static const QString mywhoosh_link_right_power;
    static constexpr int default_mywhoosh_link_right_power = 0; // Disabled

    // Cycling values for Camera Angle and Emote actions
    static const QString mywhoosh_link_camera_value;
    static constexpr int default_mywhoosh_link_camera_value = 1;

    static const QString mywhoosh_link_emote_value;
    static constexpr int default_mywhoosh_link_emote_value = 1;

    /**
     * @brief Automatically trigger a lap when completing each workout segment/row in TrainProgram
     */

    /*
     * @brief Gain multiplier applied to step count calculated from cadence for calibration purposes
     */

    /**
     * @brief Per-button gear mapping for Zwift Play/Ride controllers.
     * Values follow MyWhoosh::Action: 0 = Disabled, 1 = Gear Up, 2 = Gear Down.
     */
    static const QString zwiftplay_gear_ls1; // Left Shift Up
    static constexpr int default_zwiftplay_gear_ls1 = 2; // Gear Down
    static const QString zwiftplay_gear_ls2; // Left Shift Down
    static constexpr int default_zwiftplay_gear_ls2 = 2; // Gear Down
    static const QString zwiftplay_gear_rs1; // Right Shift Up
    static constexpr int default_zwiftplay_gear_rs1 = 1; // Gear Up
    static const QString zwiftplay_gear_rs2; // Right Shift Down
    static constexpr int default_zwiftplay_gear_rs2 = 1; // Gear Up
    static const QString zwiftplay_gear_paddle_left; // Left Paddle (ZL)
    static constexpr int default_zwiftplay_gear_paddle_left = 2; // Gear Down
    static const QString zwiftplay_gear_paddle_right; // Right Paddle (ZR)
    static constexpr int default_zwiftplay_gear_paddle_right = 1; // Gear Up
    static const QString zwiftplay_gear_lb; // Power Up (LB)
    static constexpr int default_zwiftplay_gear_lb = 0; // Disabled
    static const QString zwiftplay_gear_rb; // Ride On (RB)
    static constexpr int default_zwiftplay_gear_rb = 0; // Disabled

    /**
     *@brief resistance_slew_up Maximum rate, in resistance levels per second, at which the resistance
     *command is allowed to increase. On bikes whose console reports the commanded target instead of the
     *magnet position, a target that climbs faster than the actuator makes the bike publish an effort the
     *rider never made. Limiting the command to the actuator's own rate keeps the published value honest,
     *at the cost of responsiveness on rolling terrain. 0 disables the limiter and keeps the previous
     *behaviour exactly. Measured on a YPOO-based console: 2-3 levels/s.
     */
    static const QString resistance_slew_up;
    static constexpr double default_resistance_slew_up = 0.0;

    /**
     *@brief resistance_slew_down Same as resistance_slew_up, for decreasing resistance. The actuator is
     *usually faster coming down than going up, which is exactly why the error is one-sided when it is not
     *limited. Measured on a YPOO-based console: 4-5 levels/s. 0 disables it.
     */
    static const QString resistance_slew_down;
    static constexpr double default_resistance_slew_down = 0.0;

    /**
     *@brief gamepad_enabled Read an XInput gamepad (wired or Bluetooth Xbox pad, or any pad in its
     *X-input mode) and let it shift gears and toggle ERG. Unlike the keyboard shortcuts, which are
     *Qt.WindowShortcut and so need QZ in front, this works while the training app owns the screen.
     *Windows only.
     */
    static const QString gamepad_enabled;
    static constexpr bool default_gamepad_enabled = false;

    /**
     *@brief gamepad_gear_up Buttons that shift up, comma separated, from: a, b, x, y, lb, rb, lt,
     *rt, start, back, l3, r3, dpad_up, dpad_down, dpad_left, dpad_right. Both triggers by default,
     *so the shifter is complete under either hand whichever way the pad is mounted on the bars.
     */
    static const QString gamepad_gear_up;
    static const QString default_gamepad_gear_up;

    /**
     *@brief gamepad_gear_down Buttons that shift down. Same names as gamepad_gear_up; both bumpers
     *by default, pairing with the triggers above.
     */
    static const QString gamepad_gear_down;
    static const QString default_gamepad_gear_down;

    /**
     *@brief gamepad_erg_mode Buttons that toggle ERG mode, the same action as the ERG tile. Never
     *repeats while held, whatever the repeat settings say.
     */
    static const QString gamepad_erg_mode;
    static const QString default_gamepad_erg_mode;

    /**
     *@brief gamepad_repeat_delay Milliseconds a shift button must be held before it starts
     *repeating. 0 disables repeating entirely, giving exactly one shift per press.
     */
    static const QString gamepad_repeat_delay;
    static constexpr int default_gamepad_repeat_delay = 400;

    /**
     *@brief gamepad_repeat_rate Milliseconds between repeats once repeating has started. Values
     *below the 50 ms poll interval are clamped to it.
     */
    static const QString gamepad_repeat_rate;
    static constexpr int default_gamepad_repeat_rate = 150;

    /**
     *@brief gears_neutral_gear The gear that rides exactly what the training app asked for,
     *neither harder nor easier - the flat-road gear. Setting it to anything but 0 also
     *changes what gears_custom_table means: the rows stop being offsets added to the gear
     *and become the resistance level each gear should reach on its own, with the row count
     *deciding how many gears the bike has. 0 keeps the historic offset behaviour.
     */
    static const QString gears_neutral_gear;
    static constexpr int default_gears_neutral_gear = 0;

    /**
     *@brief simulated_bike Run against a bike that is not there. QZ skips discovery entirely
     *and plays a ride scenario into the app, so the tiles, gears, ERG, the DIRCON output and
     *the FIT file can all be exercised with no trainer in the room. Proves nothing about
     *parsing a real frame or about the bytes QZ writes back - see docs/fork/VIRTUAL-BIKE.md.
     */
    static const QString simulated_bike;
    static constexpr bool default_simulated_bike = false;

    /**
     *@brief simulated_bike_ride Path to the .ride scenario the simulated bike plays. Empty, or
     *a file that will not load, falls back to a built-in steady ride rather than leaving the
     *app without a device.
     */
    static const QString simulated_bike_ride;
    static const QString default_simulated_bike_ride;

    /**
     * @brief Write the QSettings values using the constants from this namespace.
     * @param showDefaults Optionally indicates if the default should be shown with the key.
     */
    static void qDebugAllSettings(bool showDefaults = false);

    /**
     * @brief Restore the default value to all the settings
     */
    static void restoreAll();
};

#endif
