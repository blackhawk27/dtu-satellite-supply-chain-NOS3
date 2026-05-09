#ifndef NOS3_GENERIC_TT_CHARDWAREMODEL_HPP
#define NOS3_GENERIC_TT_CHARDWAREMODEL_HPP

#include <cstdint>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include <Client/Bus.hpp>

#include <sim_i_data_provider.hpp>
#include <generic_tt_c_data_point.hpp>
#include <sim_i_hardware_model.hpp>

namespace Nos3
{
    /*
     * Generic TT&C hardware model. Subscribes to 42 spacecraft state through
     * the data provider, walks the configured ground-station table on every
     * NOS Engine time tick, and emits a single tagged "[TTC] ..." log line
     * per tick capturing link state, the active ground station, current pass
     * geometry, and downlink throughput counters. The Logstash pipeline then
     * extracts those fields into Elasticsearch.
     */
    class Generic_tt_cHardwareModel : public SimIHardwareModel
    {
    public:
        Generic_tt_cHardwareModel(const boost::property_tree::ptree& config);
        ~Generic_tt_cHardwareModel(void);

    private:
        void command_callback(NosEngine::Common::Message msg);
        void tick_callback(NosEngine::Common::SimTime t);

        std::unique_ptr<NosEngine::Client::Bus> _time_bus;
        SimIDataProvider* _generic_tt_c_dp;

        std::uint8_t  _enabled;
        std::uint32_t _tick_count;

        // Pass-tracking state
        bool          _in_pass;
        std::uint32_t _pass_number;
        std::uint32_t _pass_count;
        double        _last_slant_range_m;
        bool          _have_last_range;

        // Throughput accumulators (advance only while in pass)
        std::uint32_t _packets_downlinked;
        std::uint32_t _bytes_downlinked;
        std::uint32_t _packets_dropped;

        // Configuration
        double _elev_mask_deg;
        double _carrier_hz;
        std::uint32_t _ticks_per_emission;
    };
}

#endif
