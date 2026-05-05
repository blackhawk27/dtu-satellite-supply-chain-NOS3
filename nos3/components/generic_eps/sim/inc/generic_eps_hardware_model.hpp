#ifndef NOS3_GENERIC_EPSHARDWAREMODEL_HPP
#define NOS3_GENERIC_EPSHARDWAREMODEL_HPP

/*
** Includes
*/
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/tuple/tuple.hpp>
#include <boost/property_tree/ptree.hpp>

#include <Client/Bus.hpp>
#include <Client/DataNode.hpp>
#include <I2C/Client/I2CSlave.hpp>

#include <sim_i_data_provider.hpp>
#include <generic_eps_data_point.hpp>
#include <sim_i_hardware_model.hpp>

#include <string>


/*
** Defines
*/
#define GENERIC_EPS_SIM_SUCCESS 0
#define GENERIC_EPS_SIM_ERROR   1


/*
** Namespace
*/
namespace Nos3
{
    /* Standard for a hardware model */
    class Generic_epsHardwareModel : public SimIHardwareModel
    {
    public:
        /* Constructor and destructor */
        Generic_epsHardwareModel(const boost::property_tree::ptree& config);
        ~Generic_epsHardwareModel(void);
        std::uint8_t determine_i2c_response_for_request(const std::vector<uint8_t>& in_data, std::vector<uint8_t>& out_data); 

    private:
        /* Internal switch data */
        struct Init_Switch_State
        {
            std::string   _node_name;
            std::string   _voltage;
            std::string   _current;
            std::string   _state;
        };

        struct EPS_Rail
        {
            std::uint16_t _voltage;
            std::uint16_t _current;
            std::uint16_t _status;
            std::uint16_t _temperature;
            double        _battery_watthrs;
        };

        /* Mode-based per-app load model (see debug/EPS_DESIGN.md). Active mode is
        ** picked by SB-message-rate inference from cfs_god_view.json. */
        struct ModeThreshold
        {
            bool        has_max;
            double      max_rate_hz;
            std::string mode;
        };

        struct AppLoad
        {
            std::string                       name;
            std::string                       default_mode;
            std::string                       current_mode;
            std::vector<std::uint16_t>        mids;
            std::map<std::string, double>     mode_watts;
            std::vector<ModeThreshold>        thresholds;
            std::deque<double>                recent_msg_times;
        };

        /* Private helper methods */
        void command_callback(NosEngine::Common::Message msg); /* Handle backdoor commands and time tick to the simulator */
        void eps_switch_update(const std::uint8_t sw_num, uint8_t sw_status);
        std::uint8_t generic_eps_crc8(const std::vector<uint8_t>& crc_data, std::uint32_t crc_size);
        void create_generic_eps_data(std::vector<uint8_t>& out_data);
        void update_battery_values(void);

        /* Load model helpers */
        void load_app_load_model(const std::string& json_path);
        double compute_p_out_from_apps(void);
        std::string mode_for_rate(const AppLoad& app, double rate_hz) const;
        void god_view_follower_loop(const std::string& path);

        /* Private data members */
        class I2CSlaveConnection*                           _i2c_slave_connection;

        std::string                                         _command_bus_name;
        std::unique_ptr<NosEngine::Client::Bus>             _command_bus; /* Standard */

        SimIDataProvider*                                   _generic_eps_dp;

        /* Time Bus */
        std::unique_ptr<NosEngine::Client::Bus>             _time_bus;

        Init_Switch_State                                   _init_switch[8];
        EPS_Rail                                            _switch[8];
        EPS_Rail                                            _bus[5];
                                                                /*
                                                                0 - Battery
                                                                1 - 3.3v
                                                                2 - 5.0v
                                                                3 - 12.0v
                                                                4 - Solar Array
                                                                */

        std::uint8_t                                        _enabled;
        std::uint8_t                                        _initialized_other_sims;

        double                                              _power_per_main_panel;
        double                                              _power_per_small_panel;
        double                                              _max_battery;
        double                                              _nominal_batt_voltage;
        /* Charging hysteresis as fractions of _max_battery. Solar charging
        ** turns OFF when battery_watthrs >= _charge_stop_frac * _max_battery
        ** and back ON when battery_watthrs <= _charge_resume_frac * _max_battery.
        ** Wider band (e.g. resume=0.90) makes orbital SOC swings visible on the
        ** dashboard instead of clamping to ~100%. */
        double                                              _charge_resume_frac;
        double                                              _charge_stop_frac;

        uint8_t                                              _solar_array_inhibit;

        double                                              _charge_rate_modifer;
        uint8_t                                              _posX_Panel_Inhibit;
        uint8_t                                              _negX_Panel_Inhibit;
        uint8_t                                              _posY_Panel_Inhibit;
        uint8_t                                              _negY_Panel_Inhibit;
        uint8_t                                              _negZ_Panel_Inhibit;

        std::vector<AppLoad>                                _app_loads;
        std::vector<ModeThreshold>                          _default_thresholds;
        std::map<std::string, double>                       _tm_rate_mult;
        std::string                                         _current_tm_rate;
        double                                              _activity_window_s;
        double                                              _follower_start_time;
        std::atomic<bool>                                   _shutdown_follower;
        std::atomic<bool>                                   _follower_attached;
        std::atomic<std::uint64_t>                          _god_view_parse_errors;
        std::thread                                         _god_view_follower;
        mutable std::mutex                                  _activity_mutex;
    };

    class I2CSlaveConnection : public NosEngine::I2C::I2CSlave
    {
    public:
        I2CSlaveConnection(Generic_epsHardwareModel* hm, int bus_address, std::string connection_string, std::string bus_name);
        size_t i2c_read(uint8_t *rbuf, size_t rlen);
        size_t i2c_write(const uint8_t *wbuf, size_t wlen);
    private:
        Generic_epsHardwareModel* _hardware_model;
        std::uint8_t _i2c_read_valid;
        std::vector<uint8_t> _i2c_out_data;
    };

}

#endif
