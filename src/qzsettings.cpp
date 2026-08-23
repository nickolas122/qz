#include "qzsettings.h"
#include <QDebug>
#include <QSettings>
const QString QZSettings::cryptoKeySettingsProfiles = QStringLiteral("cryptoKeySettingsProfiles");
const QString QZSettings::bluetooth_no_reconnection = QStringLiteral("bluetooth_no_reconnection");
const QString QZSettings::bike_wheel_revs = QStringLiteral("bike_wheel_revs");
const QString QZSettings::bluetooth_lastdevice_name = QStringLiteral("bluetooth_lastdevice_name");
const QString QZSettings::default_bluetooth_lastdevice_name = QStringLiteral("");
const QString QZSettings::bluetooth_lastdevice_address = QStringLiteral("bluetooth_lastdevice_address");
const QString QZSettings::default_bluetooth_lastdevice_address = QStringLiteral("");
const QString QZSettings::hrm_lastdevice_name = QStringLiteral("hrm_lastdevice_name");
const QString QZSettings::default_hrm_lastdevice_name = QStringLiteral("");
const QString QZSettings::hrm_lastdevice_address = QStringLiteral("hrm_lastdevice_address");
const QString QZSettings::default_hrm_lastdevice_address = QStringLiteral("");
const QString QZSettings::csc_sensor_address = QStringLiteral("csc_sensor_address");
const QString QZSettings::default_csc_sensor_address = QStringLiteral("");
const QString QZSettings::csc_sensor_lastdevice_name = QStringLiteral("csc_sensor_lastdevice_name");
const QString QZSettings::default_csc_sensor_lastdevice_name = QStringLiteral("");
const QString QZSettings::power_sensor_lastdevice_name = QStringLiteral("power_sensor_lastdevice_name");
const QString QZSettings::default_power_sensor_lastdevice_name = QStringLiteral("");
const QString QZSettings::power_sensor_address = QStringLiteral("power_sensor_address");
const QString QZSettings::default_power_sensor_address = QStringLiteral("");
const QString QZSettings::elite_rizer_lastdevice_name = QStringLiteral("elite_rizer_lastdevice_name");
const QString QZSettings::default_elite_rizer_lastdevice_name = QStringLiteral("");
const QString QZSettings::elite_rizer_address = QStringLiteral("elite_rizer_address");
const QString QZSettings::default_elite_rizer_address = QStringLiteral("");
const QString QZSettings::elite_sterzo_smart_lastdevice_name = QStringLiteral("elite_sterzo_smart_lastdevice_name");
const QString QZSettings::default_elite_sterzo_smart_lastdevice_name = QStringLiteral("");
const QString QZSettings::elite_sterzo_smart_address = QStringLiteral("elite_sterzo_smart_address");
const QString QZSettings::default_elite_sterzo_smart_address = QStringLiteral("");
const QString QZSettings::code = QStringLiteral("code");
const QString QZSettings::default_code = QStringLiteral("");
//--------------------------------------------------------------------------------------------
const QString QZSettings::app_language = QStringLiteral("app_language");
const QString QZSettings::default_app_language = QStringLiteral("auto");
const QString QZSettings::bike_heartrate_service = QStringLiteral("bike_heartrate_service");
const QString QZSettings::bike_resistance_offset = QStringLiteral("bike_resistance_offset");
const QString QZSettings::bike_resistance_gain_f = QStringLiteral("bike_resistance_gain_f");
const QString QZSettings::zwift_erg = QStringLiteral("zwift_erg");
const QString QZSettings::zwift_erg_filter = QStringLiteral("zwift_erg_filter");
const QString QZSettings::zwift_erg_filter_down = QStringLiteral("zwift_erg_filter_down");
const QString QZSettings::zwift_negative_inclination_x2 = QStringLiteral("zwift_negative_inclination_x2");
const QString QZSettings::zwift_inclination_offset = QStringLiteral("zwift_inclination_offset");
const QString QZSettings::zwift_inclination_gain = QStringLiteral("zwift_inclination_gain");
const QString QZSettings::echelon_resistance_offset = QStringLiteral("echelon_resistance_offset");
const QString QZSettings::echelon_resistance_gain = QStringLiteral("echelon_resistance_gain");
const QString QZSettings::speed_power_based = QStringLiteral("speed_power_based");
const QString QZSettings::bike_resistance_start = QStringLiteral("bike_resistance_start");
const QString QZSettings::age = QStringLiteral("age");
const QString QZSettings::weight = QStringLiteral("weight");
const QString QZSettings::user_nickname = QStringLiteral("user_nickname");
const QString QZSettings::default_user_nickname = QStringLiteral("");
const QString QZSettings::miles_unit = QStringLiteral("miles_unit");
const QString QZSettings::continuous_moving = QStringLiteral("continuous_moving");
const QString QZSettings::bike_cadence_sensor = QStringLiteral("bike_cadence_sensor");
const QString QZSettings::run_cadence_sensor = QStringLiteral("run_cadence_sensor");
const QString QZSettings::rogue_echo_bike = QStringLiteral("rogue_echo_bike");
const QString QZSettings::bike_power_sensor = QStringLiteral("bike_power_sensor");
const QString QZSettings::bike_power_offset = QStringLiteral("bike_power_offset");
const QString QZSettings::heart_rate_belt_name = QStringLiteral("heart_rate_belt_name");
const QString QZSettings::default_heart_rate_belt_name = QStringLiteral("Disabled");
const QString QZSettings::heart_ignore_builtin = QStringLiteral("heart_ignore_builtin");
const QString QZSettings::ant_cadence = QStringLiteral("ant_cadence");
const QString QZSettings::ant_heart = QStringLiteral("ant_heart");
const QString QZSettings::peloton_heartrate_metric = QStringLiteral("peloton_heartrate_metric");
const QString QZSettings::default_peloton_heartrate_metric = QStringLiteral("Heart Rate");
const QString QZSettings::tile_avg_pace_enabled = QStringLiteral("tile_avg_pace_enabled");
const QString QZSettings::tile_avg_pace_order = QStringLiteral("tile_avg_pace_order");
const QString QZSettings::peloton_gain = QStringLiteral("peloton_gain");
const QString QZSettings::peloton_offset = QStringLiteral("peloton_offset");
const QString QZSettings::gymstick_gx6_0_elliptical = QStringLiteral("gymstick_gx6_0_elliptical");
const QString QZSettings::hammer_racer_s = QStringLiteral("hammer_racer_s");
const QString QZSettings::pafers_treadmill = QStringLiteral("pafers_treadmill");
const QString QZSettings::nordictrack_10_treadmill = QStringLiteral("nordictrack_10_treadmill");
const QString QZSettings::proform_treadmill_995i = QStringLiteral("proform_treadmill_995i");
const QString QZSettings::nordictrack_series_7 = QStringLiteral("nordictrack_series_7");
const QString QZSettings::nordictrack_se7i = QStringLiteral("nordictrack_se7i");
const QString QZSettings::trx_route_key = QStringLiteral("trx_route_key");
const QString QZSettings::bh_spada_2 = QStringLiteral("bh_spada_2");
const QString QZSettings::toorx_bike = QStringLiteral("toorx_bike");
const QString QZSettings::toorx_ftms = QStringLiteral("toorx_ftms");
const QString QZSettings::snode_bike = QStringLiteral("snode_bike");
const QString QZSettings::fitplus_bike = QStringLiteral("fitplus_bike");
const QString QZSettings::virtufit_etappe = QStringLiteral("virtufit_etappe");
const QString QZSettings::flywheel_life_fitness_ic8 = QStringLiteral("flywheel_life_fitness_ic8");
const QString QZSettings::sole_treadmill_inclination = QStringLiteral("sole_treadmill_inclination");
const QString QZSettings::schwinn_bike_resistance = QStringLiteral("schwinn_bike_resistance");
const QString QZSettings::schwinn_bike_resistance_v2 = QStringLiteral("schwinn_bike_resistance_v2");
const QString QZSettings::technogym_myrun_treadmill_experimental =
    QStringLiteral("technogym_myrun_treadmill_experimental");
const QString QZSettings::gym_mode = QStringLiteral("gym_mode");
const QString QZSettings::watt_offset = QStringLiteral("watt_offset");
const QString QZSettings::watt_gain = QStringLiteral("watt_gain");
const QString QZSettings::power_avg_5s = QStringLiteral("power_avg_5s");
const QString QZSettings::power_avg_3s = QStringLiteral("power_avg_3s");
const QString QZSettings::instant_power_on_pause = QStringLiteral("instant_power_on_pause");
const QString QZSettings::toputure_teb1 = QStringLiteral("toputure_teb1");
const QString QZSettings::speed_offset = QStringLiteral("speed_offset");
const QString QZSettings::speed_gain = QStringLiteral("speed_gain");
const QString QZSettings::filter_device = QStringLiteral("filter_device");
const QString QZSettings::default_filter_device = QStringLiteral("Disabled");
const QString QZSettings::cadence_sensor_name = QStringLiteral("cadence_sensor_name");
const QString QZSettings::default_cadence_sensor_name = QStringLiteral("Disabled");
const QString QZSettings::cadence_sensor_as_bike = QStringLiteral("cadence_sensor_as_bike");
const QString QZSettings::cadence_sensor_as_treadmill = QStringLiteral("cadence_sensor_as_treadmill");
const QString QZSettings::cadence_sensor_speed_ratio = QStringLiteral("cadence_sensor_speed_ratio");
const QString QZSettings::cscbike_custom_resistance_power_table = QStringLiteral("cscbike_custom_resistance_power_table");
const QString QZSettings::cscbike_custom_resistance_level_1 = QStringLiteral("cscbike_custom_resistance_level_1");
const QString QZSettings::cscbike_custom_watt_1 = QStringLiteral("cscbike_custom_watt_1");
const QString QZSettings::cscbike_custom_resistance_level_2 = QStringLiteral("cscbike_custom_resistance_level_2");
const QString QZSettings::cscbike_custom_watt_2 = QStringLiteral("cscbike_custom_watt_2");
const QString QZSettings::power_hr_pwr1 = QStringLiteral("power_hr_pwr1");
const QString QZSettings::power_hr_hr1 = QStringLiteral("power_hr_hr1");
const QString QZSettings::power_hr_pwr2 = QStringLiteral("power_hr_pwr2");
const QString QZSettings::power_hr_hr2 = QStringLiteral("power_hr_hr2");
const QString QZSettings::power_sensor_name = QStringLiteral("power_sensor_name");
const QString QZSettings::default_power_sensor_name = QStringLiteral("Disabled");
const QString QZSettings::power_sensor_speed_inclination_coeff_a = QStringLiteral("power_sensor_speed_inclination_coeff_a");
const QString QZSettings::power_sensor_speed_inclination_coeff_b = QStringLiteral("power_sensor_speed_inclination_coeff_b");
const QString QZSettings::powr_sensor_running_cadence_double = QStringLiteral("powr_sensor_running_cadence_double");
const QString QZSettings::elite_rizer_name = QStringLiteral("elite_rizer_name");
const QString QZSettings::default_elite_rizer_name = QStringLiteral("Disabled");
const QString QZSettings::elite_sterzo_smart_name = QStringLiteral("elite_sterzo_smart_name");
const QString QZSettings::default_elite_sterzo_smart_name = QStringLiteral("Disabled");
const QString QZSettings::fitmetria_fanfit_enable = QStringLiteral("fitmetria_fanfit_enable");
const QString QZSettings::fitmetria_fanfit_mode = QStringLiteral("fitmetria_fanfit_mode");
const QString QZSettings::default_fitmetria_fanfit_mode = QStringLiteral("Heart");
const QString QZSettings::fitmetria_fanfit_min = QStringLiteral("fitmetria_fanfit_min");
const QString QZSettings::fitmetria_fanfit_max = QStringLiteral("fitmetria_fanfit_max");
const QString QZSettings::virtualbike_forceresistance = QStringLiteral("virtualbike_forceresistance");
const QString QZSettings::bluetooth_relaxed = QStringLiteral("bluetooth_relaxed");
const QString QZSettings::bluetooth_30m_hangs = QStringLiteral("bluetooth_30m_hangs");
const QString QZSettings::battery_service = QStringLiteral("battery_service");
const QString QZSettings::service_changed = QStringLiteral("service_changed");
const QString QZSettings::virtual_device_enabled = QStringLiteral("virtual_device_enabled");
const QString QZSettings::virtual_device_bluetooth = QStringLiteral("virtual_device_bluetooth");
const QString QZSettings::ios_peloton_workaround = QStringLiteral("ios_peloton_workaround");
const QString QZSettings::android_wakelock = QStringLiteral("android_wakelock");
const QString QZSettings::log_debug = QStringLiteral("log_debug");
const QString QZSettings::virtual_device_onlyheart = QStringLiteral("virtual_device_onlyheart");
const QString QZSettings::virtual_device_echelon = QStringLiteral("virtual_device_echelon");
const QString QZSettings::virtual_device_ifit = QStringLiteral("virtual_device_ifit");
const QString QZSettings::virtual_device_rower = QStringLiteral("virtual_device_rower");
const QString QZSettings::virtual_device_rower_pm5 = QStringLiteral("virtual_device_rower_pm5");
const QString QZSettings::volume_change_gears = QStringLiteral("volume_change_gears");
const QString QZSettings::applewatch_fakedevice = QStringLiteral("applewatch_fakedevice");
const QString QZSettings::zwift_erg_resistance_down = QStringLiteral("zwift_erg_resistance_down");
const QString QZSettings::zwift_erg_resistance_up = QStringLiteral("zwift_erg_resistance_up");
const QString QZSettings::treadmill_step_incline = QStringLiteral("treadmill_step_incline");
// from version 2.10.23
// not used anymore because it's an elliptical not a treadmill. Don't remove this
// it will cause corruption in the settings
// const QString QZSettings:: nordictrack_fs5i_treadmill = QStringLiteral("nordictrack_fs5i_treadmill");
//
const QString QZSettings::elite_rizer_gain = QStringLiteral("elite_rizer_gain");
const QString QZSettings::reebok_fr30_treadmill = QStringLiteral("reebok_fr30_treadmill");
const QString QZSettings::horizon_treadmill_7_8 = QStringLiteral("horizon_treadmill_7_8");
const QString QZSettings::dircon_yes = QStringLiteral("dircon_yes");
const QString QZSettings::dircon_server_base_port = QStringLiteral("dircon_server_base_port");
const QString QZSettings::ios_cache_heart_device = QStringLiteral("ios_cache_heart_device");
const QString QZSettings::app_opening = QStringLiteral("app_opening");
const QString QZSettings::proformtdf4ip = QStringLiteral("proformtdf4ip");
const QString QZSettings::default_proformtdf4ip = QStringLiteral("");
const QString QZSettings::bike_weight = QStringLiteral("bike_weight");
const QString QZSettings::cadence_offset = QStringLiteral("cadence_offset");
const QString QZSettings::cadence_gain = QStringLiteral("cadence_gain");
const QString QZSettings::fakedevice_elliptical = QStringLiteral("fakedevice_elliptical");
const QString QZSettings::nordictrack_2950_ip = QStringLiteral("nordictrack_2950_ip");
const QString QZSettings::default_nordictrack_2950_ip = QStringLiteral("");
const QString QZSettings::sex = QStringLiteral("sex");
const QString QZSettings::default_sex = QStringLiteral("Male");
const QString QZSettings::proformtreadmillip = QStringLiteral("proformtreadmillip");
const QString QZSettings::default_proformtreadmillip = QStringLiteral("");
// from version 2.11.22
const QString QZSettings::tdf_10_ip = QStringLiteral("tdf_10_ip");
const QString QZSettings::default_tdf_10_ip = QStringLiteral("");
const QString QZSettings::fakedevice_treadmill = QStringLiteral("fakedevice_treadmill");
const QString QZSettings::rolling_resistance = QStringLiteral("rolling_resistance");
const QString QZSettings::wahoo_rgt_dircon = QStringLiteral("wahoo_rgt_dircon");
const QString QZSettings::wahoo_without_wheel_diameter = QStringLiteral("wahoo_without_wheel_diameter");
const QString QZSettings::CRRGain = QStringLiteral("crrGain");
const QString QZSettings::CWGain = QStringLiteral("cwGain");
const QString QZSettings::android_notification = QStringLiteral("android_notification");
const QString QZSettings::computrainer_serialport = QStringLiteral("computrainer_serialport");
const QString QZSettings::default_computrainer_serialport = QStringLiteral("");
const QString QZSettings::toorx_ftms_treadmill = QStringLiteral("toorx_ftms_treadmill");
const QString QZSettings::gears_restore_value = QStringLiteral("gears_restore_value");
const QString QZSettings::gears_current_value = QStringLiteral("gears_current_value_f");
const QString QZSettings::pafers_treadmill_bh_iboxster_plus = QStringLiteral("pafers_treadmill_bh_iboxster_plus");
const QString QZSettings::peloton_workout_ocr = QStringLiteral("peloton_workout_ocr");
const QString QZSettings::peloton_bike_ocr = QStringLiteral("peloton_bike_ocr");
const QString QZSettings::treadmill_inclination_override_0 = QStringLiteral("treadmill_inclination_override_0");
const QString QZSettings::treadmill_inclination_override_05 = QStringLiteral("treadmill_inclination_override_05");
const QString QZSettings::treadmill_inclination_override_10 = QStringLiteral("treadmill_inclination_override_10");
const QString QZSettings::treadmill_inclination_override_15 = QStringLiteral("treadmill_inclination_override_15");
const QString QZSettings::treadmill_inclination_override_20 = QStringLiteral("treadmill_inclination_override_20");
const QString QZSettings::treadmill_inclination_override_25 = QStringLiteral("treadmill_inclination_override_25");
const QString QZSettings::treadmill_inclination_override_30 = QStringLiteral("treadmill_inclination_override_30");
const QString QZSettings::treadmill_inclination_override_35 = QStringLiteral("treadmill_inclination_override_35");
const QString QZSettings::treadmill_inclination_override_40 = QStringLiteral("treadmill_inclination_override_40");
const QString QZSettings::treadmill_inclination_override_45 = QStringLiteral("treadmill_inclination_override_45");
const QString QZSettings::treadmill_inclination_override_50 = QStringLiteral("treadmill_inclination_override_50");
const QString QZSettings::treadmill_inclination_override_55 = QStringLiteral("treadmill_inclination_override_55");
const QString QZSettings::treadmill_inclination_override_60 = QStringLiteral("treadmill_inclination_override_60");
const QString QZSettings::treadmill_inclination_override_65 = QStringLiteral("treadmill_inclination_override_65");
const QString QZSettings::treadmill_inclination_override_70 = QStringLiteral("treadmill_inclination_override_70");
const QString QZSettings::treadmill_inclination_override_75 = QStringLiteral("treadmill_inclination_override_75");
const QString QZSettings::treadmill_inclination_override_80 = QStringLiteral("treadmill_inclination_override_80");
const QString QZSettings::treadmill_inclination_override_85 = QStringLiteral("treadmill_inclination_override_85");
const QString QZSettings::treadmill_inclination_override_90 = QStringLiteral("treadmill_inclination_override_90");
const QString QZSettings::treadmill_inclination_override_95 = QStringLiteral("treadmill_inclination_override_95");
const QString QZSettings::treadmill_inclination_override_100 = QStringLiteral("treadmill_inclination_override_100");
const QString QZSettings::treadmill_inclination_override_105 = QStringLiteral("treadmill_inclination_override_105");
const QString QZSettings::treadmill_inclination_override_110 = QStringLiteral("treadmill_inclination_override_110");
const QString QZSettings::treadmill_inclination_override_115 = QStringLiteral("treadmill_inclination_override_115");
const QString QZSettings::treadmill_inclination_override_120 = QStringLiteral("treadmill_inclination_override_120");
const QString QZSettings::treadmill_inclination_override_125 = QStringLiteral("treadmill_inclination_override_125");
const QString QZSettings::treadmill_inclination_override_130 = QStringLiteral("treadmill_inclination_override_130");
const QString QZSettings::treadmill_inclination_override_135 = QStringLiteral("treadmill_inclination_override_135");
const QString QZSettings::treadmill_inclination_override_140 = QStringLiteral("treadmill_inclination_override_140");
const QString QZSettings::treadmill_inclination_override_145 = QStringLiteral("treadmill_inclination_override_145");
const QString QZSettings::treadmill_inclination_override_150 = QStringLiteral("treadmill_inclination_override_150");
const QString QZSettings::horizon_treadmill_force_ftms = QStringLiteral("horizon_treadmill_force_ftms");
const QString QZSettings::treadmill_inclination_ovveride_gain = QStringLiteral("treadmill_inclination_ovveride_gain");
const QString QZSettings::treadmill_inclination_ovveride_offset =
    QStringLiteral("treadmill_inclination_ovveride_offset");
const QString QZSettings::zwift_ocr = QStringLiteral("zwift_ocr");
const QString QZSettings::gem_module_inclination = QStringLiteral("gem_module_inclination");
const QString QZSettings::treadmill_simulate_inclination_with_speed =
    QStringLiteral("treadmill_simulate_inclination_with_speed");
const QString QZSettings::garmin_companion = QStringLiteral("garmin_companion");
const QString QZSettings::iconcept_elliptical = QStringLiteral("iconcept_elliptical");
const QString QZSettings::gears_gain = QStringLiteral("gears_gain");
const QString QZSettings::gears_custom_table_enabled = QStringLiteral("gears_custom_table_enabled");
const QString QZSettings::gears_custom_table = QStringLiteral("gears_custom_table");
const QString QZSettings::default_gears_custom_table = QStringLiteral(
    "1|1\n2|2\n3|3\n4|4\n5|5\n6|6\n7|7\n8|8\n9|9\n10|10\n11|11\n12|12\n"
    "13|13\n14|14\n15|15\n16|16\n17|17\n18|18\n19|19\n20|20\n21|21\n22|22\n23|23\n24|24");
const QString QZSettings::csafe_rower = QStringLiteral("csafe_rower");
const QString QZSettings::default_csafe_rower = QStringLiteral("");
const QString QZSettings::csafe_elliptical_port = QStringLiteral("csafe_elliptical_port");
const QString QZSettings::default_csafe_elliptical_port = QStringLiteral("");
const QString QZSettings::ftms_rower = QStringLiteral("ftms_rower");
const QString QZSettings::default_ftms_rower = QStringLiteral("Disabled");
const QString QZSettings::fakedevice_rower = QStringLiteral("fakedevice_rower");
const QString QZSettings::zwift_ocr_climb_portal = QStringLiteral("zwift_ocr_climb_portal");
const QString QZSettings::poll_device_time = QStringLiteral("poll_device_time");
const QString QZSettings::watt_ignore_builtin = QStringLiteral("watt_ignore_builtin");
const QString QZSettings::ftms_bike = QStringLiteral("ftms_bike");
const QString QZSettings::default_ftms_bike = QStringLiteral("Disabled");
const QString QZSettings::ftms_treadmill = QStringLiteral("ftms_treadmill");
const QString QZSettings::default_ftms_treadmill = QStringLiteral("Disabled");
const QString QZSettings::race_mode = QStringLiteral("race_mode");
const QString QZSettings::saris_trainer = QStringLiteral("saris_trainer");
const QString QZSettings::iconsole_elliptical = QStringLiteral("iconsole_elliptical");
const QString QZSettings::proformtdf1ip = QStringLiteral("proformtdf1ip");
const QString QZSettings::default_proformtdf1ip = QStringLiteral("");
const QString QZSettings::garmin_bluetooth_compatibility = QStringLiteral("garmin_bluetooth_compatibility");
const QString QZSettings::treadmill_incline_min = QStringLiteral("treadmill_incline_min");
const QString QZSettings::treadmill_incline_max = QStringLiteral("treadmill_incline_max");
const QString QZSettings::treadmill_speed_max = QStringLiteral("treadmill_speed_max");
const QString QZSettings::treadmill_speed_min = QStringLiteral("treadmill_speed_min");
const QString QZSettings::android_documents_folder = QStringLiteral("android_documents_folder");
const QString QZSettings::zwift_click = QStringLiteral("zwift_click");
const QString QZSettings::thinkrider_controller = QStringLiteral("thinkrider_controller");
const QString QZSettings::cycplus_bc2_controller = QStringLiteral("cycplus_bc2_controller");
const QString QZSettings::zwift_play = QStringLiteral("zwift_play");
const QString QZSettings::zwift_play_vibration = QStringLiteral("zwift_play_vibration");
const QString QZSettings::stryd_speed_instead_treadmill = QStringLiteral("stryd_speed_instead_treadmill");
const QString QZSettings::ergDataPoints = QStringLiteral("ergDataPoints");
const QString QZSettings::default_ergDataPoints = QStringLiteral("");
const QString QZSettings::ftp_run = QStringLiteral("ftp_run");
const QString QZSettings::treadmillDataPoints = QStringLiteral("treadmillDataPoints");
const QString QZSettings::default_treadmillDataPoints = QStringLiteral("");
const QString QZSettings::stryd_add_inclination_gain = QStringLiteral("stryd_add_inclination_gain");
const QString QZSettings::dircon_id = QStringLiteral("dircon_id");
const QString QZSettings::rouvy_compatibility = QStringLiteral("rouvy_compatibility");
const QString QZSettings::proform_elliptical_ip = QStringLiteral("proform_elliptical_ip");
const QString QZSettings::default_proform_elliptical_ip = QStringLiteral("");
const QString QZSettings::antbike = QStringLiteral("antbike");
const QString QZSettings::domyosbike_notfmts = QStringLiteral("domyosbike_notfmts");
const QString QZSettings::gears_volume_debouncing = QStringLiteral("gears_volume_debouncing");
const QString QZSettings::domyostreadmill_notfmts = QStringLiteral("domyostreadmill_notfmts");
const QString QZSettings::zwiftplay_swap = QStringLiteral("zwiftplay_swap");
const QString QZSettings::gears_zwift_ratio = QStringLiteral("gears_zwift_ratio");
const QString QZSettings::gears_offset = QStringLiteral("gears_offset");
const QString QZSettings::force_resistance_instead_inclination = QStringLiteral("force_resistance_instead_inclination");
const QString QZSettings::zwift_play_emulator = QStringLiteral("zwift_play_emulator");
const QString QZSettings::gear_crankset_size = QStringLiteral("gear_crankset_size");
const QString QZSettings::gear_cog_size = QStringLiteral("gear_cog_size");
const QString QZSettings::gear_circumference = QStringLiteral("gear_circumference");
const QString QZSettings::watt_bike_emulator = QStringLiteral("watt_bike_emulator");
const QString QZSettings::restore_specific_gear = QStringLiteral("restore_specific_gear");
const QString QZSettings::min_inclination = QStringLiteral("min_inclination");
const QString QZSettings::proform_performance_300i = QStringLiteral("proform_performance_300i");
const QString QZSettings::sram_axs_controller = QStringLiteral("sram_axs_controller");
const QString QZSettings::treadmill_follow_wattage = QStringLiteral("treadmill_follow_wattage");









const QString QZSettings::real_inclination_to_virtual_treamill_bridge = QStringLiteral("real_inclination_to_virtual_treamill_bridge");
const QString QZSettings::stryd_inclination_instead_treadmill = QStringLiteral("stryd_inclination_instead_treadmill");
const QString QZSettings::power_sensor_cadence_instead_treadmill = QStringLiteral("power_sensor_cadence_instead_treadmill");










const QString QZSettings::zwift_gear_ui_aligned = QStringLiteral("zwift_gear_ui_aligned");





const QString QZSettings::inclinationResistancePoints = QStringLiteral("inclinationResistancePoints");
const QString QZSettings::default_inclinationResistancePoints = QStringLiteral("");



const QString QZSettings::ios_live_activity_compact_leading_metric =
    QStringLiteral("ios_live_activity_compact_leading_metric");
const QString QZSettings::default_ios_live_activity_compact_leading_metric = QStringLiteral("Heart Rate");
const QString QZSettings::ios_live_activity_compact_trailing_metric =
    QStringLiteral("ios_live_activity_compact_trailing_metric");
const QString QZSettings::default_ios_live_activity_compact_trailing_metric = QStringLiteral("Watt");
const QString QZSettings::calories_active_only = QStringLiteral("calories_active_only");
const QString QZSettings::calories_from_hr = QStringLiteral("calories_from_hr");
const QString QZSettings::height = QStringLiteral("height");
const QString QZSettings::nordictrack_vr21 = QStringLiteral("nordictrack_vr21");
const QString QZSettings::step_gain = QStringLiteral("step_gain");
const QString QZSettings::applewatch_as_treadmill_speed = QStringLiteral("applewatch_as_treadmill_speed");

// Zwift Play/Ride per-button gear mapping
const QString QZSettings::zwiftplay_gear_ls1 = QStringLiteral("zwiftplay_gear_ls1");
const QString QZSettings::zwiftplay_gear_ls2 = QStringLiteral("zwiftplay_gear_ls2");
const QString QZSettings::zwiftplay_gear_rs1 = QStringLiteral("zwiftplay_gear_rs1");
const QString QZSettings::zwiftplay_gear_rs2 = QStringLiteral("zwiftplay_gear_rs2");
const QString QZSettings::zwiftplay_gear_paddle_left = QStringLiteral("zwiftplay_gear_paddle_left");
const QString QZSettings::zwiftplay_gear_paddle_right = QStringLiteral("zwiftplay_gear_paddle_right");
const QString QZSettings::zwiftplay_gear_lb = QStringLiteral("zwiftplay_gear_lb");
const QString QZSettings::zwiftplay_gear_rb = QStringLiteral("zwiftplay_gear_rb");

const QString QZSettings::resistance_slew_up = QStringLiteral("resistance_slew_up");
const QString QZSettings::resistance_slew_down = QStringLiteral("resistance_slew_down");

// MyWhoosh Link settings
const QString QZSettings::mywhoosh_link_enabled = QStringLiteral("mywhoosh_link_enabled");
const QString QZSettings::mywhoosh_link_override_gears = QStringLiteral("mywhoosh_link_override_gears");
const QString QZSettings::mywhoosh_link_left_up = QStringLiteral("mywhoosh_link_left_up");
const QString QZSettings::mywhoosh_link_left_down = QStringLiteral("mywhoosh_link_left_down");
const QString QZSettings::mywhoosh_link_left_left = QStringLiteral("mywhoosh_link_left_left");
const QString QZSettings::mywhoosh_link_left_right = QStringLiteral("mywhoosh_link_left_right");
const QString QZSettings::mywhoosh_link_left_shoulder = QStringLiteral("mywhoosh_link_left_shoulder");
const QString QZSettings::mywhoosh_link_left_power = QStringLiteral("mywhoosh_link_left_power");
const QString QZSettings::mywhoosh_link_right_y = QStringLiteral("mywhoosh_link_right_y");
const QString QZSettings::mywhoosh_link_right_a = QStringLiteral("mywhoosh_link_right_a");
const QString QZSettings::mywhoosh_link_right_b = QStringLiteral("mywhoosh_link_right_b");
const QString QZSettings::mywhoosh_link_right_z = QStringLiteral("mywhoosh_link_right_z");
const QString QZSettings::mywhoosh_link_right_shoulder = QStringLiteral("mywhoosh_link_right_shoulder");
const QString QZSettings::mywhoosh_link_right_power = QStringLiteral("mywhoosh_link_right_power");
const QString QZSettings::mywhoosh_link_camera_value = QStringLiteral("mywhoosh_link_camera_value");
const QString QZSettings::mywhoosh_link_emote_value = QStringLiteral("mywhoosh_link_emote_value");

const QString QZSettings::gamepad_enabled = QStringLiteral("gamepad_enabled");
const QString QZSettings::gamepad_gear_up = QStringLiteral("gamepad_gear_up");
const QString QZSettings::default_gamepad_gear_up = QStringLiteral("rt,lt");
const QString QZSettings::gamepad_gear_down = QStringLiteral("gamepad_gear_down");
const QString QZSettings::default_gamepad_gear_down = QStringLiteral("rb,lb");
const QString QZSettings::gamepad_erg_mode = QStringLiteral("gamepad_erg_mode");
const QString QZSettings::default_gamepad_erg_mode = QStringLiteral("y");
const QString QZSettings::gamepad_repeat_delay = QStringLiteral("gamepad_repeat_delay");
const QString QZSettings::gamepad_repeat_rate = QStringLiteral("gamepad_repeat_rate");
const QString QZSettings::gears_neutral_gear = QStringLiteral("gears_neutral_gear");
const QString QZSettings::simulated_bike = QStringLiteral("simulated_bike");
const QString QZSettings::simulated_bike_ride = QStringLiteral("simulated_bike_ride");
const QString QZSettings::default_simulated_bike_ride = QLatin1String("");

const uint32_t allSettingsCount = 283;

QVariant allSettings[allSettingsCount][2] = {
    {QZSettings::cryptoKeySettingsProfiles, QZSettings::default_cryptoKeySettingsProfiles},
    {QZSettings::bluetooth_no_reconnection, QZSettings::default_bluetooth_no_reconnection},
    {QZSettings::bike_wheel_revs, QZSettings::default_bike_wheel_revs},
    {QZSettings::bluetooth_lastdevice_name, QZSettings::default_bluetooth_lastdevice_name},
    {QZSettings::bluetooth_lastdevice_address, QZSettings::default_bluetooth_lastdevice_address},
    {QZSettings::hrm_lastdevice_name, QZSettings::default_hrm_lastdevice_name},
    {QZSettings::hrm_lastdevice_address, QZSettings::default_hrm_lastdevice_address},
    {QZSettings::csc_sensor_address, QZSettings::default_csc_sensor_address},
    {QZSettings::csc_sensor_lastdevice_name, QZSettings::default_csc_sensor_lastdevice_name},
    {QZSettings::power_sensor_lastdevice_name, QZSettings::default_power_sensor_lastdevice_name},
    {QZSettings::power_sensor_address, QZSettings::default_power_sensor_address},
    {QZSettings::elite_rizer_lastdevice_name, QZSettings::default_elite_rizer_lastdevice_name},
    {QZSettings::elite_rizer_address, QZSettings::default_elite_rizer_address},
    {QZSettings::elite_sterzo_smart_lastdevice_name, QZSettings::default_elite_sterzo_smart_lastdevice_name},
    {QZSettings::elite_sterzo_smart_address, QZSettings::default_elite_sterzo_smart_address},
    {QZSettings::app_language, QZSettings::default_app_language},
    {QZSettings::bike_heartrate_service, QZSettings::default_bike_heartrate_service},
    {QZSettings::bike_resistance_offset, QZSettings::default_bike_resistance_offset},
    {QZSettings::bike_resistance_gain_f, QZSettings::default_bike_resistance_gain_f},
    {QZSettings::zwift_erg, QZSettings::default_zwift_erg},
    {QZSettings::zwift_erg_filter, QZSettings::default_zwift_erg_filter},
    {QZSettings::zwift_erg_filter_down, QZSettings::default_zwift_erg_filter_down},
    {QZSettings::zwift_negative_inclination_x2, QZSettings::default_zwift_negative_inclination_x2},
    {QZSettings::zwift_inclination_offset, QZSettings::default_zwift_inclination_offset},
    {QZSettings::zwift_inclination_gain, QZSettings::default_zwift_inclination_gain},
    {QZSettings::echelon_resistance_offset, QZSettings::default_echelon_resistance_offset},
    {QZSettings::echelon_resistance_gain, QZSettings::default_echelon_resistance_gain},
    {QZSettings::speed_power_based, QZSettings::default_speed_power_based},
    {QZSettings::bike_resistance_start, QZSettings::default_bike_resistance_start},
    {QZSettings::age, QZSettings::default_age},
    {QZSettings::weight, QZSettings::default_weight},
    {QZSettings::user_nickname, QZSettings::default_user_nickname},
    {QZSettings::miles_unit, QZSettings::default_miles_unit},
    {QZSettings::continuous_moving, QZSettings::default_continuous_moving},
    {QZSettings::bike_cadence_sensor, QZSettings::default_bike_cadence_sensor},
    {QZSettings::run_cadence_sensor, QZSettings::default_run_cadence_sensor},
    {QZSettings::bike_power_sensor, QZSettings::default_bike_power_sensor},
    {QZSettings::bike_power_offset, QZSettings::default_bike_power_offset},
    {QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name},
    {QZSettings::heart_ignore_builtin, QZSettings::default_heart_ignore_builtin},
    {QZSettings::ant_cadence, QZSettings::default_ant_cadence},
    {QZSettings::ant_heart, QZSettings::default_ant_heart},
    {QZSettings::peloton_heartrate_metric, QZSettings::default_peloton_heartrate_metric},
    {QZSettings::peloton_gain, QZSettings::default_peloton_gain},
    {QZSettings::peloton_offset, QZSettings::default_peloton_offset},
    {QZSettings::gymstick_gx6_0_elliptical, QZSettings::default_gymstick_gx6_0_elliptical},
    {QZSettings::hammer_racer_s, QZSettings::default_hammer_racer_s},
    {QZSettings::pafers_treadmill, QZSettings::default_pafers_treadmill},
    {QZSettings::nordictrack_10_treadmill, QZSettings::default_nordictrack_10_treadmill},
    {QZSettings::trx_route_key, QZSettings::default_trx_route_key},
    {QZSettings::bh_spada_2, QZSettings::default_bh_spada_2},
    {QZSettings::toorx_bike, QZSettings::default_toorx_bike},
    {QZSettings::toorx_ftms, QZSettings::default_toorx_ftms},
    {QZSettings::snode_bike, QZSettings::default_snode_bike},
    {QZSettings::fitplus_bike, QZSettings::default_fitplus_bike},
    {QZSettings::virtufit_etappe, QZSettings::default_virtufit_etappe},
    {QZSettings::flywheel_life_fitness_ic8, QZSettings::default_flywheel_life_fitness_ic8},
    {QZSettings::sole_treadmill_inclination, QZSettings::default_sole_treadmill_inclination},
    {QZSettings::schwinn_bike_resistance, QZSettings::default_schwinn_bike_resistance},
    {QZSettings::schwinn_bike_resistance_v2, QZSettings::default_schwinn_bike_resistance_v2},
    {QZSettings::technogym_myrun_treadmill_experimental, QZSettings::default_technogym_myrun_treadmill_experimental},
    {QZSettings::gym_mode, QZSettings::default_gym_mode},
    {QZSettings::watt_offset, QZSettings::default_watt_offset},
    {QZSettings::watt_gain, QZSettings::default_watt_gain},
    {QZSettings::power_avg_5s, QZSettings::default_power_avg_5s},
    {QZSettings::power_avg_3s, QZSettings::default_power_avg_3s},
    {QZSettings::instant_power_on_pause, QZSettings::default_instant_power_on_pause},
    {QZSettings::toputure_teb1, QZSettings::default_toputure_teb1},
    {QZSettings::speed_offset, QZSettings::default_speed_offset},
    {QZSettings::speed_gain, QZSettings::default_speed_gain},
    {QZSettings::filter_device, QZSettings::default_filter_device},
    {QZSettings::cadence_sensor_name, QZSettings::default_cadence_sensor_name},
    {QZSettings::cadence_sensor_as_bike, QZSettings::default_cadence_sensor_as_bike},
    {QZSettings::cadence_sensor_as_treadmill, QZSettings::default_cadence_sensor_as_treadmill},
    {QZSettings::cadence_sensor_speed_ratio, QZSettings::default_cadence_sensor_speed_ratio},
    {QZSettings::cscbike_custom_resistance_power_table, QZSettings::default_cscbike_custom_resistance_power_table},
    {QZSettings::cscbike_custom_resistance_level_1, QZSettings::default_cscbike_custom_resistance_level_1},
    {QZSettings::cscbike_custom_watt_1, QZSettings::default_cscbike_custom_watt_1},
    {QZSettings::cscbike_custom_resistance_level_2, QZSettings::default_cscbike_custom_resistance_level_2},
    {QZSettings::cscbike_custom_watt_2, QZSettings::default_cscbike_custom_watt_2},
    {QZSettings::power_hr_pwr1, QZSettings::default_power_hr_pwr1},
    {QZSettings::power_hr_hr1, QZSettings::default_power_hr_hr1},
    {QZSettings::power_hr_pwr2, QZSettings::default_power_hr_pwr2},
    {QZSettings::power_hr_hr2, QZSettings::default_power_hr_hr2},
    {QZSettings::power_sensor_name, QZSettings::default_power_sensor_name},
    {QZSettings::power_sensor_speed_inclination_coeff_a, QZSettings::default_power_sensor_speed_inclination_coeff_a},
    {QZSettings::power_sensor_speed_inclination_coeff_b, QZSettings::default_power_sensor_speed_inclination_coeff_b},
    {QZSettings::powr_sensor_running_cadence_double, QZSettings::default_powr_sensor_running_cadence_double},
    {QZSettings::elite_rizer_name, QZSettings::default_elite_rizer_name},
    {QZSettings::elite_sterzo_smart_name, QZSettings::default_elite_sterzo_smart_name},
    {QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable},
    {QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode},
    {QZSettings::fitmetria_fanfit_min, QZSettings::default_fitmetria_fanfit_min},
    {QZSettings::fitmetria_fanfit_max, QZSettings::default_fitmetria_fanfit_max},
    {QZSettings::virtualbike_forceresistance, QZSettings::default_virtualbike_forceresistance},
    {QZSettings::bluetooth_relaxed, QZSettings::default_bluetooth_relaxed},
    {QZSettings::bluetooth_30m_hangs, QZSettings::default_bluetooth_30m_hangs},
    {QZSettings::battery_service, QZSettings::default_battery_service},
    {QZSettings::service_changed, QZSettings::default_service_changed},
    {QZSettings::virtual_device_enabled, QZSettings::default_virtual_device_enabled},
    {QZSettings::virtual_device_bluetooth, QZSettings::default_virtual_device_bluetooth},
    {QZSettings::ios_peloton_workaround, QZSettings::default_ios_peloton_workaround},
    {QZSettings::android_wakelock, QZSettings::default_android_wakelock},
    {QZSettings::log_debug, QZSettings::default_log_debug},
    {QZSettings::virtual_device_onlyheart, QZSettings::default_virtual_device_onlyheart},
    {QZSettings::virtual_device_echelon, QZSettings::default_virtual_device_echelon},
    {QZSettings::virtual_device_ifit, QZSettings::default_virtual_device_ifit},
    {QZSettings::virtual_device_rower, QZSettings::default_virtual_device_rower},
    {QZSettings::virtual_device_rower_pm5, QZSettings::default_virtual_device_rower_pm5},
    {QZSettings::volume_change_gears, QZSettings::default_volume_change_gears},
    {QZSettings::applewatch_fakedevice, QZSettings::default_applewatch_fakedevice},
    {QZSettings::zwift_erg_resistance_down, QZSettings::default_zwift_erg_resistance_down},
    {QZSettings::zwift_erg_resistance_up, QZSettings::default_zwift_erg_resistance_up},
    {QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline},
    {QZSettings::elite_rizer_gain, QZSettings::default_elite_rizer_gain},
    {QZSettings::reebok_fr30_treadmill, QZSettings::default_reebok_fr30_treadmill},
    {QZSettings::horizon_treadmill_7_8, QZSettings::default_horizon_treadmill_7_8},
    {QZSettings::dircon_yes, QZSettings::default_dircon_yes},
    {QZSettings::dircon_server_base_port, QZSettings::default_dircon_server_base_port},
    {QZSettings::ios_cache_heart_device, QZSettings::default_ios_cache_heart_device},
    {QZSettings::app_opening, QZSettings::default_app_opening},
    {QZSettings::proformtdf4ip, QZSettings::default_proformtdf4ip},
    {QZSettings::bike_weight, QZSettings::default_bike_weight},
    {QZSettings::cadence_offset, QZSettings::default_cadence_offset},
    {QZSettings::cadence_gain, QZSettings::default_cadence_gain},
    {QZSettings::fakedevice_elliptical, QZSettings::default_fakedevice_elliptical},
    {QZSettings::nordictrack_2950_ip, QZSettings::default_nordictrack_2950_ip},
    {QZSettings::sex, QZSettings::default_sex},
    {QZSettings::proformtreadmillip, QZSettings::default_proformtreadmillip},
    {QZSettings::tdf_10_ip, QZSettings::default_tdf_10_ip},
    {QZSettings::fakedevice_treadmill, QZSettings::default_fakedevice_treadmill},
    {QZSettings::rolling_resistance, QZSettings::default_rolling_resistance},
    {QZSettings::wahoo_rgt_dircon, QZSettings::default_wahoo_rgt_dircon},
    {QZSettings::CRRGain, QZSettings::default_CRRGain},
    {QZSettings::CWGain, QZSettings::default_CWGain},
    {QZSettings::android_notification, QZSettings::default_android_notification},
    {QZSettings::computrainer_serialport, QZSettings::default_computrainer_serialport},
    {QZSettings::toorx_ftms_treadmill, QZSettings::default_toorx_ftms_treadmill},
    {QZSettings::gears_restore_value, QZSettings::default_gears_restore_value},
    {QZSettings::gears_current_value, QZSettings::default_gears_current_value},
    {QZSettings::pafers_treadmill_bh_iboxster_plus, QZSettings::default_pafers_treadmill_bh_iboxster_plus},
    {QZSettings::peloton_workout_ocr, QZSettings::default_peloton_workout_ocr},
    {QZSettings::peloton_bike_ocr, QZSettings::default_peloton_bike_ocr},
    {QZSettings::treadmill_inclination_override_0, QZSettings::default_treadmill_inclination_override_0},
    {QZSettings::treadmill_inclination_override_05, QZSettings::default_treadmill_inclination_override_05},
    {QZSettings::treadmill_inclination_override_10, QZSettings::default_treadmill_inclination_override_10},
    {QZSettings::treadmill_inclination_override_15, QZSettings::default_treadmill_inclination_override_15},
    {QZSettings::treadmill_inclination_override_20, QZSettings::default_treadmill_inclination_override_20},
    {QZSettings::treadmill_inclination_override_25, QZSettings::default_treadmill_inclination_override_25},
    {QZSettings::treadmill_inclination_override_30, QZSettings::default_treadmill_inclination_override_30},
    {QZSettings::treadmill_inclination_override_35, QZSettings::default_treadmill_inclination_override_35},
    {QZSettings::treadmill_inclination_override_40, QZSettings::default_treadmill_inclination_override_40},
    {QZSettings::treadmill_inclination_override_45, QZSettings::default_treadmill_inclination_override_45},
    {QZSettings::treadmill_inclination_override_50, QZSettings::default_treadmill_inclination_override_50},
    {QZSettings::treadmill_inclination_override_55, QZSettings::default_treadmill_inclination_override_55},
    {QZSettings::treadmill_inclination_override_60, QZSettings::default_treadmill_inclination_override_60},
    {QZSettings::treadmill_inclination_override_65, QZSettings::default_treadmill_inclination_override_65},
    {QZSettings::treadmill_inclination_override_70, QZSettings::default_treadmill_inclination_override_70},
    {QZSettings::treadmill_inclination_override_75, QZSettings::default_treadmill_inclination_override_75},
    {QZSettings::treadmill_inclination_override_80, QZSettings::default_treadmill_inclination_override_80},
    {QZSettings::treadmill_inclination_override_85, QZSettings::default_treadmill_inclination_override_85},
    {QZSettings::treadmill_inclination_override_90, QZSettings::default_treadmill_inclination_override_90},
    {QZSettings::treadmill_inclination_override_95, QZSettings::default_treadmill_inclination_override_95},
    {QZSettings::treadmill_inclination_override_100, QZSettings::default_treadmill_inclination_override_100},
    {QZSettings::treadmill_inclination_override_105, QZSettings::default_treadmill_inclination_override_105},
    {QZSettings::treadmill_inclination_override_110, QZSettings::default_treadmill_inclination_override_110},
    {QZSettings::treadmill_inclination_override_115, QZSettings::default_treadmill_inclination_override_115},
    {QZSettings::treadmill_inclination_override_120, QZSettings::default_treadmill_inclination_override_120},
    {QZSettings::treadmill_inclination_override_125, QZSettings::default_treadmill_inclination_override_125},
    {QZSettings::treadmill_inclination_override_130, QZSettings::default_treadmill_inclination_override_130},
    {QZSettings::treadmill_inclination_override_135, QZSettings::default_treadmill_inclination_override_135},
    {QZSettings::treadmill_inclination_override_140, QZSettings::default_treadmill_inclination_override_140},
    {QZSettings::treadmill_inclination_override_145, QZSettings::default_treadmill_inclination_override_145},
    {QZSettings::treadmill_inclination_override_150, QZSettings::default_treadmill_inclination_override_150},
    {QZSettings::horizon_treadmill_force_ftms, QZSettings::default_horizon_treadmill_force_ftms},
    {QZSettings::treadmill_inclination_ovveride_gain, QZSettings::default_treadmill_inclination_ovveride_gain},
    {QZSettings::treadmill_inclination_ovveride_offset, QZSettings::default_treadmill_inclination_ovveride_offset},
    {QZSettings::zwift_ocr, QZSettings::default_zwift_ocr},
    {QZSettings::gem_module_inclination, QZSettings::default_gem_module_inclination},
    {QZSettings::treadmill_simulate_inclination_with_speed,
     QZSettings::default_treadmill_simulate_inclination_with_speed},
    {QZSettings::garmin_companion, QZSettings::default_garmin_companion},
    {QZSettings::iconcept_elliptical, QZSettings::default_iconcept_elliptical},
    {QZSettings::gears_gain, QZSettings::default_gears_gain},
    {QZSettings::csafe_rower, QZSettings::default_csafe_rower},
    {QZSettings::csafe_elliptical_port, QZSettings::default_csafe_elliptical_port},
    {QZSettings::ftms_rower, QZSettings::default_ftms_rower},
    {QZSettings::fakedevice_rower, QZSettings::default_fakedevice_rower},
    {QZSettings::zwift_ocr_climb_portal, QZSettings::default_zwift_ocr_climb_portal},
    {QZSettings::poll_device_time, QZSettings::default_poll_device_time},
    {QZSettings::watt_ignore_builtin, QZSettings::default_watt_ignore_builtin},
    {QZSettings::ftms_bike, QZSettings::default_ftms_bike},
    {QZSettings::ftms_treadmill, QZSettings::default_ftms_treadmill},
    {QZSettings::race_mode, QZSettings::default_race_mode},
    {QZSettings::saris_trainer, QZSettings::default_saris_trainer},
    {QZSettings::iconsole_elliptical, QZSettings::default_iconsole_elliptical},    
    {QZSettings::proformtdf1ip, QZSettings::default_proformtdf1ip},
    {QZSettings::garmin_bluetooth_compatibility, QZSettings::default_garmin_bluetooth_compatibility},
    {QZSettings::treadmill_incline_min, QZSettings::default_treadmill_incline_min},
    {QZSettings::treadmill_incline_max, QZSettings::default_treadmill_incline_max},
    {QZSettings::treadmill_speed_max, QZSettings::default_treadmill_speed_max},
    {QZSettings::treadmill_speed_min, QZSettings::default_treadmill_speed_min},
    {QZSettings::android_documents_folder, QZSettings::default_android_documents_folder},
    {QZSettings::zwift_click, QZSettings::default_zwift_click},
    {QZSettings::thinkrider_controller, QZSettings::default_thinkrider_controller},
    {QZSettings::cycplus_bc2_controller, QZSettings::default_cycplus_bc2_controller},
    {QZSettings::zwift_play, QZSettings::default_zwift_play},
    {QZSettings::zwift_play_vibration, QZSettings::default_zwift_play_vibration},
    {QZSettings::stryd_speed_instead_treadmill, QZSettings::default_stryd_speed_instead_treadmill},
    {QZSettings::ergDataPoints, QZSettings::default_ergDataPoints},
    {QZSettings::ftp_run, QZSettings::default_ftp_run},
    {QZSettings::treadmillDataPoints, QZSettings::default_treadmillDataPoints},
    {QZSettings::stryd_add_inclination_gain, QZSettings::default_stryd_add_inclination_gain},
    {QZSettings::dircon_id, QZSettings::default_dircon_id},
    {QZSettings::rouvy_compatibility, QZSettings::default_rouvy_compatibility},
    {QZSettings::proform_elliptical_ip, QZSettings::default_proform_elliptical_ip},
    {QZSettings::antbike, QZSettings::default_antbike},
    {QZSettings::domyosbike_notfmts, QZSettings::default_domyosbike_notfmts},
    {QZSettings::gears_volume_debouncing, QZSettings::default_gears_volume_debouncing},
    {QZSettings::domyostreadmill_notfmts, QZSettings::default_domyostreadmill_notfmts},
    {QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap},
    {QZSettings::gears_zwift_ratio, QZSettings::default_gears_zwift_ratio},
    {QZSettings::gears_custom_table_enabled, QZSettings::default_gears_custom_table_enabled},
    {QZSettings::gears_custom_table, QZSettings::default_gears_custom_table},
    {QZSettings::gears_offset, QZSettings::default_gears_offset},
    {QZSettings::force_resistance_instead_inclination, QZSettings::default_force_resistance_instead_inclination},
    {QZSettings::zwift_play_emulator, QZSettings::default_zwift_play_emulator},
    {QZSettings::gear_crankset_size, QZSettings::default_gear_crankset_size},
    {QZSettings::gear_cog_size, QZSettings::default_gear_cog_size},
    {QZSettings::gear_circumference, QZSettings::default_gear_circumference},
    {QZSettings::watt_bike_emulator, QZSettings::default_watt_bike_emulator},
    {QZSettings::restore_specific_gear, QZSettings::default_restore_specific_gear},
    {QZSettings::min_inclination, QZSettings::default_min_inclination},
    {QZSettings::proform_performance_300i, QZSettings::default_proform_performance_300i},
    {QZSettings::sram_axs_controller, QZSettings::default_sram_axs_controller},
    {QZSettings::treadmill_follow_wattage, QZSettings::default_treadmill_follow_wattage},








    {QZSettings::real_inclination_to_virtual_treamill_bridge, QZSettings::default_real_inclination_to_virtual_treamill_bridge},
    {QZSettings::stryd_inclination_instead_treadmill, QZSettings::default_stryd_inclination_instead_treadmill},
    {QZSettings::power_sensor_cadence_instead_treadmill, QZSettings::default_power_sensor_cadence_instead_treadmill},




    {QZSettings::zwift_gear_ui_aligned, QZSettings::default_zwift_gear_ui_aligned},



    {QZSettings::inclinationResistancePoints, QZSettings::default_inclinationResistancePoints},
    {QZSettings::ios_live_activity_compact_leading_metric,
     QZSettings::default_ios_live_activity_compact_leading_metric},
    {QZSettings::ios_live_activity_compact_trailing_metric,
     QZSettings::default_ios_live_activity_compact_trailing_metric},
    {QZSettings::rogue_echo_bike, QZSettings::default_rogue_echo_bike},
    {QZSettings::calories_active_only, QZSettings::default_calories_active_only},
    {QZSettings::calories_from_hr, QZSettings::default_calories_from_hr},
    {QZSettings::height, QZSettings::default_height},
    {QZSettings::mywhoosh_link_enabled, QZSettings::default_mywhoosh_link_enabled},
    {QZSettings::mywhoosh_link_override_gears, QZSettings::default_mywhoosh_link_override_gears},
    {QZSettings::mywhoosh_link_left_up, QZSettings::default_mywhoosh_link_left_up},
    {QZSettings::mywhoosh_link_left_down, QZSettings::default_mywhoosh_link_left_down},
    {QZSettings::mywhoosh_link_left_left, QZSettings::default_mywhoosh_link_left_left},
    {QZSettings::mywhoosh_link_left_right, QZSettings::default_mywhoosh_link_left_right},
    {QZSettings::mywhoosh_link_left_shoulder, QZSettings::default_mywhoosh_link_left_shoulder},
    {QZSettings::mywhoosh_link_left_power, QZSettings::default_mywhoosh_link_left_power},
    {QZSettings::mywhoosh_link_right_y, QZSettings::default_mywhoosh_link_right_y},
    {QZSettings::mywhoosh_link_right_a, QZSettings::default_mywhoosh_link_right_a},
    {QZSettings::mywhoosh_link_right_b, QZSettings::default_mywhoosh_link_right_b},
    {QZSettings::mywhoosh_link_right_z, QZSettings::default_mywhoosh_link_right_z},
    {QZSettings::mywhoosh_link_right_shoulder, QZSettings::default_mywhoosh_link_right_shoulder},
    {QZSettings::mywhoosh_link_right_power, QZSettings::default_mywhoosh_link_right_power},
    {QZSettings::mywhoosh_link_camera_value, QZSettings::default_mywhoosh_link_camera_value},
    {QZSettings::mywhoosh_link_emote_value, QZSettings::default_mywhoosh_link_emote_value},
    {QZSettings::step_gain, QZSettings::default_step_gain},
    {QZSettings::applewatch_as_treadmill_speed, QZSettings::default_applewatch_as_treadmill_speed},
    {QZSettings::zwiftplay_gear_ls1, QZSettings::default_zwiftplay_gear_ls1},
    {QZSettings::zwiftplay_gear_ls2, QZSettings::default_zwiftplay_gear_ls2},
    {QZSettings::zwiftplay_gear_rs1, QZSettings::default_zwiftplay_gear_rs1},
    {QZSettings::zwiftplay_gear_rs2, QZSettings::default_zwiftplay_gear_rs2},
    {QZSettings::zwiftplay_gear_paddle_left, QZSettings::default_zwiftplay_gear_paddle_left},
    {QZSettings::zwiftplay_gear_paddle_right, QZSettings::default_zwiftplay_gear_paddle_right},
    {QZSettings::zwiftplay_gear_lb, QZSettings::default_zwiftplay_gear_lb},
    {QZSettings::zwiftplay_gear_rb, QZSettings::default_zwiftplay_gear_rb},
    {QZSettings::resistance_slew_up, QZSettings::default_resistance_slew_up},
    {QZSettings::resistance_slew_down, QZSettings::default_resistance_slew_down},
    {QZSettings::gamepad_enabled, QZSettings::default_gamepad_enabled},
    {QZSettings::gamepad_gear_up, QZSettings::default_gamepad_gear_up},
    {QZSettings::gamepad_gear_down, QZSettings::default_gamepad_gear_down},
    {QZSettings::gamepad_erg_mode, QZSettings::default_gamepad_erg_mode},
    {QZSettings::gamepad_repeat_delay, QZSettings::default_gamepad_repeat_delay},
    {QZSettings::gamepad_repeat_rate, QZSettings::default_gamepad_repeat_rate},
    {QZSettings::gears_neutral_gear, QZSettings::default_gears_neutral_gear},
    {QZSettings::simulated_bike, QZSettings::default_simulated_bike},
    {QZSettings::simulated_bike_ride, QZSettings::default_simulated_bike_ride},
};

void QZSettings::qDebugAllSettings(bool showDefaults) {
    QSettings settings;
    // make a copy of the settings for sorting
    std::vector<QVariant *> sorted;
    for (uint32_t i = 0; i < allSettingsCount; i++) {
        sorted.push_back(allSettings[i]);
    }
    // sort the settings alphabetically
    struct {
        bool operator()(QVariant *a, QVariant *b) { return a[0].toString() < b[0].toString(); }
    } comparer;
    std::sort(sorted.begin(), sorted.end(), comparer);
    for (uint32_t i = 0; i < sorted.size(); i++) {
        QVariant *item = sorted[i];
        QString key = item[0].toString();
        QVariant defaultValue = item[1];
        if (!showDefaults) {
            qDebug() << key << settings.value(key, defaultValue);
        } else {
            qDebug() << "(" << key << ", " << defaultValue << ") = " << settings.value(key, defaultValue);
        }
    }
}

void QZSettings::restoreAll() {
    qDebug() << QStringLiteral("RESTORING SETTINGS!");
    QSettings settings;
    for (uint32_t i = 0; i < allSettingsCount; i++) {
        settings.setValue(allSettings[i][0].toString(), allSettings[i][1]);
    }
}
